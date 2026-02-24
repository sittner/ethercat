/* master/uspace/pal_eoe.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <net/if_arp.h>

#include "pal_eoe.h"
#include "../ethernet.h"

/****************************************************************************/
/* Network Device Implementation */
/****************************************************************************/

ec_netdev_t *ec_netdev_alloc(const char *name, size_t priv_size)
{
    ec_netdev_t *dev;
    
    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }
    
    if (priv_size > 0) {
        dev->priv = calloc(1, priv_size);
        if (!dev->priv) {
            free(dev);
            return NULL;
        }
    }
    
    strncpy(dev->name, name, IFNAMSIZ - 1);
    dev->name[IFNAMSIZ - 1] = '\0';
    dev->fd = -1;
    dev->mtu = 1500;
    dev->opened = 0;
    dev->tx_queue_active = 0;
    
    pthread_mutex_init(&dev->tx_lock, NULL);
    
    return dev;
}

int ec_netdev_register(ec_netdev_t *dev)
{
    struct ifreq ifr;
    int fd, err;
    int sock;
    
    if (!dev) {
        return -EINVAL;
    }
    
    /* Open TUN/TAP clone device */
    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "EoE: Failed to open /dev/net/tun: %s\n", 
                strerror(errno));
        return -errno;
    }
    
    /* Configure as TAP device (layer 2) with no packet info header */
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, dev->name, IFNAMSIZ - 1);
    
    err = ioctl(fd, TUNSETIFF, &ifr);
    if (err < 0) {
        fprintf(stderr, "EoE: Failed to create TAP device %s: %s\n",
                dev->name, strerror(errno));
        close(fd);
        return -errno;
    }
    
    /* Update device name (kernel may have modified it) */
    strncpy(dev->name, ifr.ifr_name, IFNAMSIZ - 1);
    dev->name[IFNAMSIZ - 1] = '\0';
    dev->fd = fd;
    
    /* Get interface index */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, dev->name, IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFINDEX, &ifr) == 0) {
            dev->ifindex = ifr.ifr_ifindex;
        }
        close(sock);
    }
    
    fprintf(stderr, "EoE: Created TAP device %s (index %d, fd %d)\n",
            dev->name, dev->ifindex, dev->fd);
    
    return 0;
}

void ec_netdev_unregister(ec_netdev_t *dev)
{
    if (!dev) {
        return;
    }
    
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
        fprintf(stderr, "EoE: Destroyed TAP device %s\n", dev->name);
    }
}

void ec_netdev_free(ec_netdev_t *dev)
{
    if (!dev) {
        return;
    }
    
    /* Ensure TAP is closed */
    if (dev->fd >= 0) {
        close(dev->fd);
    }
    
    pthread_mutex_destroy(&dev->tx_lock);
    
    if (dev->priv) {
        free(dev->priv);
    }
    
    free(dev);
}

int ec_netdev_set_mac(ec_netdev_t *dev, const uint8_t mac[ETH_ALEN])
{
    struct ifreq ifr;
    int sock, ret = 0;
    
    if (!dev || dev->fd < 0) {
        return -EINVAL;
    }
    
    /* Store MAC in device structure */
    memcpy(dev->dev_addr, mac, ETH_ALEN);
    
    /* Set MAC on TAP interface */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -errno;
    }
    
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, dev->name, IFNAMSIZ - 1);
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, ETH_ALEN);
    
    if (ioctl(sock, SIOCSIFHWADDR, &ifr) < 0) {
        ret = -errno;
        fprintf(stderr, "EoE: Failed to set MAC on %s: %s\n",
                dev->name, strerror(errno));
    }
    
    close(sock);
    return ret;
}

/****************************************************************************/
/* Socket Buffer Implementation */
/****************************************************************************/

ec_skb_t *ec_skb_alloc(unsigned int size)
{
    ec_skb_t *skb;
    
    skb = calloc(1, sizeof(*skb));
    if (!skb) {
        return NULL;
    }
    
    skb->head = malloc(size);
    if (!skb->head) {
        free(skb);
        return NULL;
    }
    
    skb->data = skb->head;
    skb->tail = skb->head;
    skb->end = skb->head + size;
    skb->len = 0;
    skb->protocol = 0;
    skb->dev = NULL;
    skb->ip_summed = EC_CHECKSUM_NONE;
    
    return skb;
}

void ec_skb_free(ec_skb_t *skb)
{
    if (skb) {
        free(skb->head);
        free(skb);
    }
}

uint8_t *ec_skb_put(ec_skb_t *skb, unsigned int len)
{
    uint8_t *tmp;
    
    if (!skb || skb->tail + len > skb->end) {
        return NULL;
    }
    
    tmp = skb->tail;
    skb->tail += len;
    skb->len += len;
    return tmp;
}

uint16_t ec_eth_type_trans(ec_skb_t *skb, ec_netdev_t *dev)
{
    struct ethhdr *eth;
    
    if (!skb || skb->len < ETH_HLEN) {
        return 0;
    }
    
    eth = (struct ethhdr *)skb->data;
    skb->data += ETH_HLEN;
    skb->len -= ETH_HLEN;
    skb->dev = dev;
    
    return ntohs(eth->h_proto);
}

/****************************************************************************/
/* Network RX/TX Implementation */
/****************************************************************************/

int ec_netif_rx(ec_skb_t *skb)
{
    ec_netdev_t *dev;
    ssize_t ret;
    
    if (!skb || !skb->dev) {
        return -EINVAL;
    }
    
    dev = skb->dev;
    
    if (dev->fd < 0) {
        return -ENODEV;
    }
    
    /* Write complete Ethernet frame to TAP device.
     * The frame includes the Ethernet header at skb->head.
     */
    ret = write(dev->fd, skb->head, skb->tail - skb->head);
    if (ret < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            fprintf(stderr, "EoE: TAP write error on %s: %s\n",
                    dev->name, strerror(errno));
            dev->stats.rx_errors++;
            return -errno;
        }
        /* Would block - frame dropped */
        dev->stats.rx_dropped++;
        return -EAGAIN;
    }
    
    /* Update statistics for successful RX */
    dev->stats.rx_packets++;
    dev->stats.rx_bytes += ret;
    
    return 0;
}

ec_skb_t *ec_netdev_rx_from_tap(ec_netdev_t *dev)
{
    ec_skb_t *skb;
    ssize_t len;
    
    if (!dev || dev->fd < 0) {
        return NULL;
    }
    
    /* Allocate buffer for maximum Ethernet frame */
    skb = ec_skb_alloc(ETH_FRAME_LEN);
    if (!skb) {
        return NULL;
    }
    
    /* Non-blocking read from TAP device */
    len = read(dev->fd, skb->head, ETH_FRAME_LEN);
    if (len <= 0) {
        ec_skb_free(skb);
        if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            dev->stats.tx_errors++;
        }
        return NULL;
    }
    
    skb->tail = skb->head + len;
    skb->len = len;
    skb->dev = dev;
    
    /* Update statistics for successful TX */
    dev->stats.tx_packets++;
    dev->stats.tx_bytes += len;
    
    return skb;
}

/****************************************************************************/
/* EoE Lifecycle API */
/****************************************************************************/

int ec_eoe_netdev_create(struct ec_eoe *eoe, const char *name)
{
    ec_eoe_t **priv;
    uint8_t mac_addr[ETH_ALEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    int ret;

    eoe->dev = ec_netdev_alloc(name, sizeof(ec_eoe_t *));
    if (!eoe->dev) {
        return -ENOMEM;
    }

    ec_netdev_set_mac(eoe->dev, mac_addr);

    priv = ec_netdev_priv(eoe->dev);
    *priv = eoe;

    ret = ec_netdev_register(eoe->dev);
    if (ret) {
        ec_netdev_free(eoe->dev);
        eoe->dev = NULL;
        return ret;
    }

    /* Make last MAC octet unique using interface index */
    mac_addr[ETH_ALEN - 1] = (uint8_t) ec_eoe_netdev_ifindex(eoe->dev);
    ec_netdev_set_mac(eoe->dev, mac_addr);

    return 0;
}

void ec_eoe_netdev_destroy(struct ec_eoe *eoe)
{
    if (eoe->dev) {
        ec_netdev_unregister(eoe->dev);
        ec_netdev_free(eoe->dev);
        eoe->dev = NULL;
    }
}

