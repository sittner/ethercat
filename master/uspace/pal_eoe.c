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
/* Socket Buffer Implementation                                             */
/*                                                                          */
/* Frame buffers come from a preallocated, prefaulted and mlocked pool     */
/* instead of per-frame heap allocation (issue #175: unbounded malloc in   */
/* the EoE thread). The pool bounds are known: the TX queue is limited to  */
/* EC_EOE_TX_QUEUE_SIZE frames per handler and RX has one frame in flight, */
/* so exhaustion only occurs with many concurrent EoE handlers under full  */
/* load — then frames are dropped (correct for Ethernet) and counted.     */
/****************************************************************************/

#define EC_SKB_POOL_SLOTS 128
#define EC_SKB_POOL_BUF_SIZE 2048 /* covers ETH_FRAME_LEN and the largest
                                     EoE reassembly (63 * 32 bytes) */

typedef struct ec_skb_slot {
    ec_skb_t skb;
    struct ec_skb_slot *next; /**< Freelist link. */
    uint8_t buf[EC_SKB_POOL_BUF_SIZE];
} ec_skb_slot_t;

static ec_skb_slot_t *skb_pool; /**< Slot array (ec_rt_zalloc'd). */
static ec_skb_slot_t *skb_pool_free; /**< Freelist head. */
static pthread_mutex_t skb_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long skb_pool_dropped;
static pthread_once_t skb_pool_once = PTHREAD_ONCE_INIT;

static void ec_skb_pool_init(void)
{
    unsigned int i;

    skb_pool = ec_rt_zalloc(EC_SKB_POOL_SLOTS * sizeof(ec_skb_slot_t));
    if (!skb_pool) {
        return; /* allocation stays disabled; frames are dropped */
    }
    for (i = 0; i < EC_SKB_POOL_SLOTS - 1; i++) {
        skb_pool[i].next = &skb_pool[i + 1];
    }
    skb_pool[EC_SKB_POOL_SLOTS - 1].next = NULL;
    skb_pool_free = &skb_pool[0];
}

ec_skb_t *ec_skb_alloc(unsigned int size)
{
    ec_skb_slot_t *slot;

    if (size > EC_SKB_POOL_BUF_SIZE) {
        return NULL;
    }

    pthread_once(&skb_pool_once, ec_skb_pool_init);

    pthread_mutex_lock(&skb_pool_lock);
    slot = skb_pool_free;
    if (slot) {
        skb_pool_free = slot->next;
    } else {
        skb_pool_dropped++;
    }
    pthread_mutex_unlock(&skb_pool_lock);

    if (!slot) {
        if (ec_log_ratelimit()) {
            ec_log(EC_LOG_WARNING, "EoE: frame buffer pool exhausted"
                    " (%lu frame(s) dropped)\n", skb_pool_dropped);
        }
        return NULL;
    }

    slot->skb.head = slot->buf;
    slot->skb.data = slot->buf;
    slot->skb.tail = slot->buf;
    slot->skb.end = slot->buf + size;
    slot->skb.len = 0;
    slot->skb.protocol = 0;
    slot->skb.dev = NULL;
    slot->skb.ip_summed = EC_CHECKSUM_NONE;

    return &slot->skb;
}

void ec_skb_free(ec_skb_t *skb)
{
    ec_skb_slot_t *slot;

    if (!skb) {
        return;
    }

    slot = (ec_skb_slot_t *) skb; /* skb is the slot's first member */
    pthread_mutex_lock(&skb_pool_lock);
    slot->next = skb_pool_free;
    skb_pool_free = slot;
    pthread_mutex_unlock(&skb_pool_lock);
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
    int err = 0;

    if (!skb) {
        return -EINVAL;
    }

    /* Like the kernel's netif_rx(), this function CONSUMES the buffer
     * on every path — the caller must not free it (the delivered
     * buffers leaked before this was enforced). */

    dev = skb->dev;
    if (!dev || dev->fd < 0) {
        ec_skb_free(skb);
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
            err = -errno;
        } else {
            /* Would block - frame dropped */
            dev->stats.rx_dropped++;
            err = -EAGAIN;
        }
    } else {
        /* Update statistics for successful RX */
        dev->stats.rx_packets++;
        dev->stats.rx_bytes += ret;
    }

    ec_skb_free(skb);
    return err;
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

    /* In userspace, the TAP device is always "open" once created.
     * There is no ifconfig/ip-link callback, so we mark it open
     * immediately to allow the EoE state machine to process frames.
     */
    eoe->opened = 1;
    eoe->tx_queue_active = 1;

    return 0;
}

void ec_eoe_netdev_destroy(struct ec_eoe *eoe)
{
    if (eoe->dev) {
        eoe->opened = 0;
        eoe->tx_queue_active = 0;
        ec_netdev_unregister(eoe->dev);
        ec_netdev_free(eoe->dev);
        eoe->dev = NULL;
    }
}

/****************************************************************************/
/* EoE TX Polling                                                            */
/****************************************************************************/

/** Poll TAP device for outgoing frames and enqueue them for EoE transmission.
 *
 * In kernel space, the network stack calls ndo_start_xmit (push model).
 * In userspace, we poll the TAP fd (pull model) from the EoE thread.
 */
void ec_eoe_poll_tx(ec_eoe_t *eoe)
{
    ec_skb_t *skb;
    ec_eoe_frame_t *frame;

    if (!eoe->opened || !eoe->dev || !eoe->tx_queue_active) {
        return;
    }

    while (eoe->tx_queued_frames < eoe->tx_queue_size) {
        skb = ec_netdev_rx_from_tap(eoe->dev);
        if (!skb) {
            break;
        }

        frame = malloc(sizeof(ec_eoe_frame_t));
        if (!frame) {
            ec_skb_free(skb);
            break;
        }

        frame->skb = skb;
        INIT_LIST_HEAD(&frame->queue);
        list_add_tail(&frame->queue, &eoe->tx_queue);
        eoe->tx_queued_frames++;
    }

    /* Stop accepting if queue is full */
    if (eoe->tx_queued_frames >= eoe->tx_queue_size) {
        ec_eoe_netdev_stop_queue(eoe->dev);
    }
}

