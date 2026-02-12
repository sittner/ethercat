/******************************************************************************
 *
 *  Copyright (C) 2006-2024  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

/**
 * \file
 * XDP transport implementation using AF_XDP sockets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <linux/if_link.h>
#include <xdp/xsk.h>
#include <bpf/bpf.h>

#include "ec_transport.h"

/****************************************************************************/

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define INVALID_UMEM_FRAME UINT64_MAX

/** Private data for XDP transport */
typedef struct {
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem *umem;
    struct xsk_socket *xsk;
    void *umem_buffer;
    uint64_t umem_frame_addr[NUM_FRAMES];
    uint32_t umem_frame_free;

    int if_index;                      /**< Interface index */
    uint8_t mac_addr[6];               /**< Interface MAC address */
} ec_transport_xdp_t;

/****************************************************************************/

/**
 * Allocate a UMEM frame.
 */
static uint64_t xsk_alloc_umem_frame(ec_transport_xdp_t *xdp)
{
    uint64_t frame;
    if (xdp->umem_frame_free == 0) {
        return INVALID_UMEM_FRAME;
    }

    frame = xdp->umem_frame_addr[--xdp->umem_frame_free];
    xdp->umem_frame_addr[xdp->umem_frame_free] = INVALID_UMEM_FRAME;
    return frame;
}

/****************************************************************************/

/**
 * Free a UMEM frame.
 */
static void xsk_free_umem_frame(ec_transport_xdp_t *xdp, uint64_t frame)
{
    if (xdp->umem_frame_free >= NUM_FRAMES) {
        fprintf(stderr, "Warning: UMEM frame pool overflow\n");
        return;
    }
    xdp->umem_frame_addr[xdp->umem_frame_free++] = frame;
}

/****************************************************************************/

/**
 * Open XDP transport on interface.
 */
static int xdp_open(ec_transport_t *transport, const char *interface)
{
    ec_transport_xdp_t *xdp;
    struct ifreq ifr;
    struct xsk_socket_config cfg;
    uint32_t idx;
    uint64_t addr;
    int ret;
    int sock_fd;
    int i;

    /* Allocate private data */
    xdp = calloc(1, sizeof(ec_transport_xdp_t));
    if (!xdp) {
        return -ENOMEM;
    }

    transport->priv = xdp;

    /* Create temporary socket to get interface info */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        ret = -errno;
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        goto err_free;
    }

    /* Get interface index */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(sock_fd, SIOCGIFINDEX, &ifr) < 0) {
        ret = -errno;
        fprintf(stderr, "Failed to get interface index for %s: %s\n",
                interface, strerror(errno));
        close(sock_fd);
        goto err_free;
    }
    xdp->if_index = ifr.ifr_ifindex;

    /* Get MAC address */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(sock_fd, SIOCGIFHWADDR, &ifr) < 0) {
        ret = -errno;
        fprintf(stderr, "Failed to get MAC address for %s: %s\n",
                interface, strerror(errno));
        close(sock_fd);
        goto err_free;
    }
    memcpy(xdp->mac_addr, ifr.ifr_hwaddr.sa_data, 6);

    close(sock_fd);

    /* Allocate UMEM buffer */
    xdp->umem_buffer = aligned_alloc(getpagesize(), NUM_FRAMES * FRAME_SIZE);
    if (!xdp->umem_buffer) {
        fprintf(stderr, "Failed to allocate UMEM buffer\n");
        ret = -ENOMEM;
        goto err_free;
    }

    /* Initialize UMEM frame pool */
    for (i = 0; i < NUM_FRAMES; i++) {
        xdp->umem_frame_addr[i] = i * FRAME_SIZE;
    }
    xdp->umem_frame_free = NUM_FRAMES;

    /* Create UMEM */
    ret = xsk_umem__create(&xdp->umem, xdp->umem_buffer, NUM_FRAMES * FRAME_SIZE,
                          &xdp->fq, &xdp->cq, NULL);
    if (ret) {
        fprintf(stderr, "Failed to create UMEM: %s\n", strerror(-ret));
        goto err_free_buffer;
    }

    /* Configure socket */
    memset(&cfg, 0, sizeof(cfg));
    cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    cfg.xdp_flags = XDP_FLAGS_SKB_MODE;
    cfg.bind_flags = XDP_COPY;
    cfg.libbpf_flags = 0;

    /* Create XSK socket (queue 0) */
    ret = xsk_socket__create(&xdp->xsk, interface, 0, xdp->umem,
                            &xdp->rx, &xdp->tx, &cfg);
    if (ret) {
        fprintf(stderr, "Failed to create XSK socket: %s\n", strerror(-ret));
        goto err_free_umem;
    }

    /* Populate fill queue */
    ret = xsk_ring_prod__reserve(&xdp->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);
    if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS) {
        fprintf(stderr, "Failed to reserve fill queue\n");
        goto err_free_socket;
    }

    for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++) {
        addr = xsk_alloc_umem_frame(xdp);
        *xsk_ring_prod__fill_addr(&xdp->fq, idx++) = addr;
    }

    xsk_ring_prod__submit(&xdp->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return 0;

err_free_socket:
    xsk_socket__delete(xdp->xsk);
err_free_umem:
    xsk_umem__delete(xdp->umem);
err_free_buffer:
    free(xdp->umem_buffer);
err_free:
    free(xdp);
    transport->priv = NULL;
    return ret;
}

/****************************************************************************/

/**
 * Close XDP transport.
 */
static void xdp_close(ec_transport_t *transport)
{
    ec_transport_xdp_t *xdp = transport->priv;

    if (xdp) {
        if (xdp->xsk) {
            xsk_socket__delete(xdp->xsk);
        }
        if (xdp->umem) {
            xsk_umem__delete(xdp->umem);
        }
        if (xdp->umem_buffer) {
            free(xdp->umem_buffer);
        }
        free(xdp);
        transport->priv = NULL;
    }
}

/****************************************************************************/

/**
 * Get TX buffer pointer.
 */
static uint8_t *xdp_get_tx_buffer(ec_transport_t *transport)
{
    return transport->tx_buffer;
}

/****************************************************************************/

/**
 * Send frame.
 */
static int xdp_send(ec_transport_t *transport, size_t size)
{
    ec_transport_xdp_t *xdp = transport->priv;
    uint64_t addr;
    uint32_t idx;
    uint32_t idx_cq;
    unsigned int rcvd;
    int ret;
    unsigned int i;

    if (!xdp || !xdp->xsk) {
        return -ENODEV;
    }

    if (size > EC_TRANSPORT_MAX_FRAME_SIZE) {
        return -EINVAL;
    }

    /* Allocate UMEM frame first */
    addr = xsk_alloc_umem_frame(xdp);
    if (addr == INVALID_UMEM_FRAME) {
        /* Try to process completions to free some frames */
        idx_cq = 0;

        rcvd = xsk_ring_cons__peek(&xdp->cq, XSK_RING_CONS__DEFAULT_NUM_DESCS, &idx_cq);
        if (rcvd > 0) {
            for (i = 0; i < rcvd; i++) {
                uint64_t cq_addr = *xsk_ring_cons__comp_addr(&xdp->cq, idx_cq++);
                xsk_free_umem_frame(xdp, cq_addr);
            }
            xsk_ring_cons__release(&xdp->cq, rcvd);
        }

        /* Try again to allocate */
        addr = xsk_alloc_umem_frame(xdp);
        if (addr == INVALID_UMEM_FRAME) {
            return -ENOMEM;
        }
    }

    /* Reserve TX slot */
    ret = xsk_ring_prod__reserve(&xdp->tx, 1, &idx);
    if (ret != 1) {
        /* TX ring is full, cannot send */
        xsk_free_umem_frame(xdp, addr);
        return -EBUSY;
    }

    /* Copy data to UMEM */
    memcpy(xsk_umem__get_data(xdp->umem_buffer, addr),
           transport->tx_buffer, size);

    /* Submit TX descriptor */
    xsk_ring_prod__tx_desc(&xdp->tx, idx)->addr = addr;
    xsk_ring_prod__tx_desc(&xdp->tx, idx)->len = size;
    xsk_ring_prod__submit(&xdp->tx, 1);

    /* Trigger send */
    ret = sendto(xsk_socket__fd(xdp->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    if (ret < 0 && errno != ENOBUFS && errno != EAGAIN) {
        return -errno;
    }

    /* Process completion queue to free frames */
    rcvd = xsk_ring_cons__peek(&xdp->cq, 1, &idx_cq);
    if (rcvd > 0) {
        for (i = 0; i < rcvd; i++) {
            uint64_t cq_addr = *xsk_ring_cons__comp_addr(&xdp->cq, idx_cq++);
            xsk_free_umem_frame(xdp, cq_addr);
        }
        xsk_ring_cons__release(&xdp->cq, rcvd);
    }

    return 0;
}

/****************************************************************************/

/**
 * Receive frame (non-blocking).
 */
static int xdp_receive(ec_transport_t *transport, uint8_t *buffer, size_t max_size)
{
    ec_transport_xdp_t *xdp = transport->priv;
    uint32_t idx_rx = 0;
    uint32_t idx_fq = 0;
    unsigned int rcvd;
    int ret;

    if (!xdp || !xdp->xsk) {
        return -ENODEV;
    }

    /* Check for received packets */
    rcvd = xsk_ring_cons__peek(&xdp->rx, 1, &idx_rx);
    if (!rcvd) {
        return 0;  /* No data available */
    }

    /* Get received packet */
    uint64_t addr = xsk_ring_cons__rx_desc(&xdp->rx, idx_rx)->addr;
    uint32_t len = xsk_ring_cons__rx_desc(&xdp->rx, idx_rx)->len;

    if (len > max_size) {
        len = max_size;
    }

    /* Copy data from UMEM */
    memcpy(buffer, xsk_umem__get_data(xdp->umem_buffer, addr), len);

    /* Release RX descriptor and refill */
    xsk_ring_cons__release(&xdp->rx, 1);

    /* Refill fill queue */
    ret = xsk_ring_prod__reserve(&xdp->fq, 1, &idx_fq);
    if (ret == 1) {
        *xsk_ring_prod__fill_addr(&xdp->fq, idx_fq) = addr;
        xsk_ring_prod__submit(&xdp->fq, 1);
    } else {
        /* Fill queue full, free the frame */
        xsk_free_umem_frame(xdp, addr);
    }

    return (int)len;
}

/****************************************************************************/

/**
 * Get link state.
 */
static int xdp_get_link_state(ec_transport_t *transport)
{
    ec_transport_xdp_t *xdp = transport->priv;
    struct ifreq ifr;
    int sock_fd;
    int ret;

    if (!xdp) {
        return -ENODEV;
    }

    /* Create temporary socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        return -errno;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, transport->interface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(sock_fd, SIOCGIFFLAGS, &ifr) < 0) {
        ret = -errno;
        close(sock_fd);
        return ret;
    }

    close(sock_fd);

    /* Check if interface is up and running */
    if ((ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING)) {
        return 1;  /* Link up */
    }

    return 0;  /* Link down */
}

/****************************************************************************/

/**
 * Get MAC address.
 */
static int xdp_get_mac(ec_transport_t *transport, uint8_t mac[6])
{
    ec_transport_xdp_t *xdp = transport->priv;

    if (!xdp) {
        return -ENODEV;
    }

    memcpy(mac, xdp->mac_addr, 6);
    return 0;
}

/****************************************************************************/

/**
 * Get file descriptor for polling.
 */
static int xdp_get_fd(ec_transport_t *transport)
{
    ec_transport_xdp_t *xdp = transport->priv;

    if (!xdp || !xdp->xsk) {
        return -1;
    }

    return xsk_socket__fd(xdp->xsk);
}

/****************************************************************************/

/** XDP transport operations */
const ec_transport_ops_t ec_transport_xdp_ops = {
    .name = "xdp",
    .open = xdp_open,
    .close = xdp_close,
    .get_tx_buffer = xdp_get_tx_buffer,
    .send = xdp_send,
    .receive = xdp_receive,
    .get_link_state = xdp_get_link_state,
    .get_mac = xdp_get_mac,
    .get_fd = xdp_get_fd,
};

/****************************************************************************/
