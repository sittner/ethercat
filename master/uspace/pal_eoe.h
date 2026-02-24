/* master/uspace/pal_eoe.h */

#ifndef __EC_USPACE_PAL_EOE_H__
#define __EC_USPACE_PAL_EOE_H__

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <linux/if.h>
#include <linux/if_ether.h>

/****************************************************************************/
/* Network Device Abstraction */
/****************************************************************************/

/** Userspace network device (wraps TAP device) */
typedef struct ec_netdev {
    int fd;                     /**< TAP device file descriptor */
    char name[IFNAMSIZ];        /**< Interface name (e.g., "eoe0s1") */
    uint8_t dev_addr[ETH_ALEN]; /**< MAC address */
    int ifindex;                /**< Interface index */
    unsigned int mtu;           /**< MTU size */
    int opened;                 /**< Device is open */
    
    /* TX queue management */
    pthread_mutex_t tx_lock;    /**< TX queue lock */
    int tx_queue_active;        /**< TX queue is active */
    
    /* Statistics */
    struct {
        unsigned long rx_packets;
        unsigned long tx_packets;
        unsigned long rx_bytes;
        unsigned long tx_bytes;
        unsigned long rx_errors;
        unsigned long tx_errors;
        unsigned long rx_dropped;
        unsigned long tx_dropped;
    } stats;
    
    /* Private data storage */
    void *priv;                 /**< Private data pointer */
} ec_netdev_t;

/** Allocate and initialize a network device
 * @param name Device name (e.g., "eoe0s1")
 * @param priv_size Size of private data to allocate
 * @return Pointer to network device, or NULL on failure
 */
ec_netdev_t *ec_netdev_alloc(const char *name, size_t priv_size);

/** Register network device (creates TAP interface)
 * @param dev Network device
 * @return 0 on success, negative error code on failure
 */
int ec_netdev_register(ec_netdev_t *dev);

/** Unregister network device (destroys TAP interface)
 * @param dev Network device
 */
void ec_netdev_unregister(ec_netdev_t *dev);

/** Free network device
 * @param dev Network device
 */
void ec_netdev_free(ec_netdev_t *dev);

/** Get private data pointer
 * @param dev Network device
 * @return Private data pointer
 */
static inline void *ec_netdev_priv(ec_netdev_t *dev) {
    return dev->priv;
}

/** Set MAC address
 * @param dev Network device
 * @param mac MAC address (6 bytes)
 * @return 0 on success, negative error code on failure
 */
int ec_netdev_set_mac(ec_netdev_t *dev, const uint8_t mac[ETH_ALEN]);

/** Get file descriptor for polling
 * @param dev Network device
 * @return File descriptor, or -1 if not available
 */
static inline int ec_netdev_get_fd(ec_netdev_t *dev) {
    return dev ? dev->fd : -1;
}

/****************************************************************************/
/* Socket Buffer Abstraction */
/****************************************************************************/

/** Userspace socket buffer */
typedef struct ec_skb {
    uint8_t *data;              /**< Current data pointer */
    uint8_t *head;              /**< Start of buffer */
    uint8_t *tail;              /**< End of data */
    uint8_t *end;               /**< End of buffer */
    unsigned int len;           /**< Data length */
    uint16_t protocol;          /**< Protocol (ETH_P_*) */
    ec_netdev_t *dev;           /**< Associated device */
    int ip_summed;              /**< Checksum status */
} ec_skb_t;

#define EC_CHECKSUM_NONE        0
#define EC_CHECKSUM_UNNECESSARY 1

/** Allocate a socket buffer
 * @param size Buffer size
 * @return Pointer to socket buffer, or NULL on failure
 */
ec_skb_t *ec_skb_alloc(unsigned int size);

/** Free a socket buffer
 * @param skb Socket buffer
 */
void ec_skb_free(ec_skb_t *skb);

/** Reserve space at end of buffer and return pointer to new data area
 * @param skb Socket buffer
 * @param len Number of bytes to add
 * @return Pointer to new data area
 */
uint8_t *ec_skb_put(ec_skb_t *skb, unsigned int len);

/** Parse Ethernet header and extract protocol
 * @param skb Socket buffer (data pointer is advanced past Ethernet header)
 * @param dev Network device
 * @return Protocol in host byte order
 */
uint16_t ec_eth_type_trans(ec_skb_t *skb, ec_netdev_t *dev);

/****************************************************************************/
/* TX Queue Management */
/****************************************************************************/

/** Start TX queue (allow transmissions)
 * @param dev Network device
 */
static inline void ec_netdev_start_queue(ec_netdev_t *dev) {
    dev->tx_queue_active = 1;
}

/** Stop TX queue (block new transmissions)
 * @param dev Network device
 */
static inline void ec_netdev_stop_queue(ec_netdev_t *dev) {
    dev->tx_queue_active = 0;
}

/** Wake TX queue (resume after stop)
 * @param dev Network device
 */
static inline void ec_netdev_wake_queue(ec_netdev_t *dev) {
    dev->tx_queue_active = 1;
}

/** Check if TX queue is active
 * @param dev Network device
 * @return 1 if active, 0 if stopped
 */
static inline int ec_netdev_queue_active(ec_netdev_t *dev) {
    return dev->tx_queue_active;
}

/** Lock TX queue
 * @param dev Network device
 */
static inline void ec_netdev_tx_lock(ec_netdev_t *dev) {
    pthread_mutex_lock(&dev->tx_lock);
}

/** Unlock TX queue
 * @param dev Network device
 */
static inline void ec_netdev_tx_unlock(ec_netdev_t *dev) {
    pthread_mutex_unlock(&dev->tx_lock);
}

/****************************************************************************/
/* Network RX/TX Operations */
/****************************************************************************/

/** Pass received packet to network stack (write to TAP device)
 * @param skb Socket buffer containing received frame
 * @return 0 on success, non-zero on failure
 */
int ec_netif_rx(ec_skb_t *skb);

/** Receive packet from TAP device (for transmission via EoE)
 * @param dev Network device
 * @return Socket buffer with received frame, or NULL if none available
 *
 * Note: This is non-blocking. Returns NULL if no data available.
 */
ec_skb_t *ec_netdev_rx_from_tap(ec_netdev_t *dev);

/****************************************************************************/
/* EoE PAL Typedefs */
/****************************************************************************/

typedef ec_netdev_t * ec_eoe_netdev_t;
typedef ec_skb_t * ec_eoe_buf_t;
typedef struct {
    unsigned long rx_packets;
    unsigned long tx_packets;
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_errors;
    unsigned long tx_errors;
    unsigned long rx_dropped;
    unsigned long tx_dropped;
} ec_eoe_stats_t;

static inline const char *ec_eoe_netdev_name(ec_eoe_netdev_t dev) {
    return dev->name;
}

static inline int ec_eoe_netdev_ifindex(ec_eoe_netdev_t dev) {
    return dev->ifindex;
}

/* Forward declaration */
struct ec_eoe;

/* Net_device lifecycle */
int ec_eoe_netdev_create(struct ec_eoe *eoe, const char *name);
void ec_eoe_netdev_destroy(struct ec_eoe *eoe);

/* Net_device queue operations */
static inline void ec_eoe_netdev_tx_lock(ec_eoe_netdev_t dev) {
    ec_netdev_tx_lock(dev);
}
static inline void ec_eoe_netdev_tx_unlock(ec_eoe_netdev_t dev) {
    ec_netdev_tx_unlock(dev);
}
static inline void ec_eoe_netdev_start_queue(ec_eoe_netdev_t dev) {
    ec_netdev_start_queue(dev);
}
static inline void ec_eoe_netdev_stop_queue(ec_eoe_netdev_t dev) {
    ec_netdev_stop_queue(dev);
}
static inline void ec_eoe_netdev_wake_queue(ec_eoe_netdev_t dev) {
    ec_netdev_wake_queue(dev);
}

/****************************************************************************/
/* EoE Buffer PAL Wrappers                                                   */
/****************************************************************************/

static inline ec_eoe_buf_t ec_eoe_buf_alloc(unsigned int size) {
    return ec_skb_alloc(size);
}

static inline void ec_eoe_buf_free(ec_eoe_buf_t buf) {
    ec_skb_free(buf);
}

static inline uint8_t *ec_eoe_buf_put(ec_eoe_buf_t buf, unsigned int len) {
    return ec_skb_put(buf, len);
}

static inline uint16_t ec_eoe_buf_eth_type_trans(ec_eoe_buf_t buf,
        ec_eoe_netdev_t dev) {
    return ec_eth_type_trans(buf, dev);
}

static inline int ec_eoe_buf_deliver(ec_eoe_buf_t buf) {
    return ec_netif_rx(buf);
}

#define EC_EOE_CHECKSUM_UNNECESSARY EC_CHECKSUM_UNNECESSARY

/****************************************************************************/
/* Utility Functions */
/****************************************************************************/

/** Check if rate limiting should apply (for warning messages)
 * @return 1 if message should be printed, 0 if rate limited
 */
int ec_printk_ratelimit(void);

#endif /* __EC_USPACE_PAL_EOE_H__ */
