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

#endif
