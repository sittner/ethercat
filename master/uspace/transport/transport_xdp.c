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
    int ioctl_sock;                    /**< Socket for ioctl operations (link state, etc.) */
    uint64_t tx_frame_addr;            /**< Pre-allocated TX frame for zero-copy */
    uint32_t xdp_flags;                /**< XDP flags used during open (needed for detach) */
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
        fprintf(stderr, "Warning: UMEM frame pool overflow - frame will be leaked\n");
        return;
    }
    xdp->umem_frame_addr[xdp->umem_frame_free++] = frame;
}

/****************************************************************************/

/**
 * Process completion queue and free completed frames.
 */
static void process_completion_queue(ec_transport_xdp_t *xdp, unsigned int max_frames)
{
    uint32_t idx_cq = 0;
    unsigned int rcvd;
    unsigned int i;

    rcvd = xsk_ring_cons__peek(&xdp->cq, max_frames, &idx_cq);
    if (rcvd > 0) {
        for (i = 0; i < rcvd; i++) {
            uint64_t addr = *xsk_ring_cons__comp_addr(&xdp->cq, idx_cq++);
            xsk_free_umem_frame(xdp, addr);
        }
        xsk_ring_cons__release(&xdp->cq, rcvd);
    }
}

/****************************************************************************/

/**
 * Open XDP transport on interface.
 */
static int xdp_open(ec_transport_t *transport, const char *interface,
    uint32_t xdp_flags, uint16_t bind_flags)
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

    xdp->ioctl_sock = -1;
    xdp->tx_frame_addr = INVALID_UMEM_FRAME;
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

    /* Store socket for later ioctl operations */
    xdp->ioctl_sock = sock_fd;

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

    /* Configure socket
     * Use SKB mode and copy mode for maximum compatibility across different
     * network drivers and kernel versions. While native XDP mode with zero-copy
     * would provide better performance, SKB mode ensures the transport works
     * on all network interfaces without requiring driver-specific XDP support.
     */
    memset(&cfg, 0, sizeof(cfg));
    cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    cfg.xdp_flags = xdp_flags;
    cfg.bind_flags = bind_flags;
    cfg.libbpf_flags = 0;

    /* Create XSK socket (queue 0) */
    ret = xsk_socket__create(&xdp->xsk, interface, 0, xdp->umem,
                            &xdp->rx, &xdp->tx, &cfg);
    if (ret) {
        fprintf(stderr, "Failed to create XSK socket: %s\n", strerror(-ret));
        goto err_free_umem;
    }
    xdp->xdp_flags = xdp_flags;

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
    if (xdp->ioctl_sock >= 0) {
        close(xdp->ioctl_sock);
    }
    free(xdp);
    transport->priv = NULL;
    return ret;
}

/**
 * Open XDP transport on interface (SKB mode).
 */
static int xdp_open_skb(ec_transport_t *transport, const char *interface)
{
    return xdp_open(transport, interface, XDP_FLAGS_SKB_MODE,
                    XDP_COPY | XDP_USE_NEED_WAKEUP);
}

/**
 * Open XDP transport on interface (Native mode).
 */
static int xdp_open_native(ec_transport_t *transport, const char *interface)
{
    return xdp_open(transport, interface, XDP_FLAGS_DRV_MODE,
                    XDP_COPY | XDP_USE_NEED_WAKEUP);
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
            if (xdp->if_index > 0) {
                /* Detach XDP program before deleting socket.
                 * Try with original flags first, fall back to flags=0
                 * (force detach) if that fails (e.g. lost capabilities).
                 */
                if (bpf_xdp_detach(xdp->if_index, xdp->xdp_flags, NULL) < 0) {
                    bpf_xdp_detach(xdp->if_index, 0, NULL);
                }
            }
            xsk_socket__delete(xdp->xsk);
        }
        if (xdp->umem) {
            xsk_umem__delete(xdp->umem);
        }
        if (xdp->umem_buffer) {
            free(xdp->umem_buffer);
        }
        if (xdp->ioctl_sock >= 0) {
            close(xdp->ioctl_sock);
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
    ec_transport_xdp_t *xdp = transport->priv;
    uint64_t addr;

    if (!xdp) {
        return transport->tx_buffer;
    }

    /* Process completion queue to reclaim frames before allocating new one
     * This moves variable-time operation out of the TX critical path */
    process_completion_queue(xdp, XSK_RING_CONS__DEFAULT_NUM_DESCS);

    /* If we don't have a pre-allocated frame, try to get one */
    if (xdp->tx_frame_addr == INVALID_UMEM_FRAME) {
        addr = xsk_alloc_umem_frame(xdp);
        if (addr != INVALID_UMEM_FRAME) {
            /* Successfully allocated a frame - return direct UMEM pointer for zero-copy */
            xdp->tx_frame_addr = addr;
            return (uint8_t *)xsk_umem__get_data(xdp->umem_buffer, addr);
        }
        /* Fall through to fallback buffer if allocation failed */
    } else {
        /* Already have a pre-allocated frame - return direct UMEM pointer */
        return (uint8_t *)xsk_umem__get_data(xdp->umem_buffer, xdp->tx_frame_addr);
    }

    /* Fallback to transport->tx_buffer if UMEM allocation failed */
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
    int ret;
    int is_zero_copy = 0;

    if (!xdp || !xdp->xsk) {
        return -ENODEV;
    }

    if (size > EC_TRANSPORT_MAX_FRAME_SIZE) {
        return -EINVAL;
    }

    /* Check if we have a pre-allocated frame (zero-copy path) */
    if (xdp->tx_frame_addr != INVALID_UMEM_FRAME) {
        /* Zero-copy path: use the pre-allocated frame directly */
        addr = xdp->tx_frame_addr;
        is_zero_copy = 1;
    } else {
        /* Fallback path: allocate frame and copy data from transport->tx_buffer */
        addr = xsk_alloc_umem_frame(xdp);
        if (addr == INVALID_UMEM_FRAME) {
            return -ENOMEM;
        }

        /* Copy data to UMEM */
        memcpy(xsk_umem__get_data(xdp->umem_buffer, addr),
               transport->tx_buffer, size);
    }

    /* Reserve TX slot */
    ret = xsk_ring_prod__reserve(&xdp->tx, 1, &idx);
    if (ret != 1) {
        /* TX ring is full, cannot send */
        if (!is_zero_copy) {
            /* We just allocated this frame in fallback path, free it */
            xsk_free_umem_frame(xdp, addr);
        }
        /* For zero-copy, keep tx_frame_addr valid so it can be reused */
        return -EBUSY;
    }

    /* Submit TX descriptor */
    xsk_ring_prod__tx_desc(&xdp->tx, idx)->addr = addr;
    xsk_ring_prod__tx_desc(&xdp->tx, idx)->len = size;
    xsk_ring_prod__submit(&xdp->tx, 1);

    /* Mark frame as consumed (will be reclaimed from completion queue later) */
    if (is_zero_copy) {
        xdp->tx_frame_addr = INVALID_UMEM_FRAME;
    }

    /* Trigger send - critical for immediate transmission in EtherCAT */
    ret = sendto(xsk_socket__fd(xdp->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    if (ret < 0 && errno != ENOBUFS && errno != EAGAIN) {
        return -errno;
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
    uint64_t addr;
    uint32_t len;
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
    addr = xsk_ring_cons__rx_desc(&xdp->rx, idx_rx)->addr;
    len = xsk_ring_cons__rx_desc(&xdp->rx, idx_rx)->len;

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

    if (!xdp || xdp->ioctl_sock < 0) {
        return -ENODEV;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, transport->interface, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(xdp->ioctl_sock, SIOCGIFFLAGS, &ifr) < 0) {
        return -errno;
    }

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
const ec_transport_ops_t ec_transport_xdp_skb_ops = {
    .name = "xdp-skb",
    .open = xdp_open_skb,
    .close = xdp_close,
    .get_tx_buffer = xdp_get_tx_buffer,
    .send = xdp_send,
    .receive = xdp_receive,
    .get_link_state = xdp_get_link_state,
    .get_mac = xdp_get_mac,
    .get_fd = xdp_get_fd,
};

const ec_transport_ops_t ec_transport_xdp_native_ops = {
    .name = "xdp-native",
    .open = xdp_open_native,
    .close = xdp_close,
    .get_tx_buffer = xdp_get_tx_buffer,
    .send = xdp_send,
    .receive = xdp_receive,
    .get_link_state = xdp_get_link_state,
    .get_mac = xdp_get_mac,
    .get_fd = xdp_get_fd,
};

/****************************************************************************/
