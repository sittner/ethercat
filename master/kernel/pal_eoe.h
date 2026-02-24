#ifndef __EC_KERNEL_PAL_EOE_H__
#define __EC_KERNEL_PAL_EOE_H__

typedef struct net_device * ec_eoe_netdev_t;
typedef struct sk_buff * ec_eoe_buf_t;
typedef struct net_device_stats ec_eoe_stats_t;

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
    netif_tx_lock_bh(dev);
}
static inline void ec_eoe_netdev_tx_unlock(ec_eoe_netdev_t dev) {
    netif_tx_unlock_bh(dev);
}
static inline void ec_eoe_netdev_start_queue(ec_eoe_netdev_t dev) {
    netif_start_queue(dev);
}
static inline void ec_eoe_netdev_stop_queue(ec_eoe_netdev_t dev) {
    netif_stop_queue(dev);
}
static inline void ec_eoe_netdev_wake_queue(ec_eoe_netdev_t dev) {
    netif_wake_queue(dev);
}

#endif
