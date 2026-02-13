/* master/uspace/pal_eoe_compat.h */

#ifndef __EC_USPACE_PAL_EOE_COMPAT_H__
#define __EC_USPACE_PAL_EOE_COMPAT_H__

/**
 * Kernel API compatibility macros for EoE module.
 * 
 * These macros allow ethernet.c to compile with minimal changes
 * by mapping kernel APIs to userspace PAL equivalents.
 */

#include "pal_eoe.h"

/* Type mappings */
#define net_device          ec_netdev_t
#define sk_buff             ec_skb_t
#define net_device_stats    /* embedded in ec_netdev_t */

/* net_device functions */
#define alloc_netdev(priv_size, name, name_type, setup) \
    ec_netdev_alloc(name, priv_size)
#define register_netdev(dev)        ec_netdev_register(dev)
#define unregister_netdev(dev)      ec_netdev_unregister(dev)
#define free_netdev(dev)            ec_netdev_free(dev)
#define netdev_priv(dev)            ec_netdev_priv(dev)

/* sk_buff functions */
#define dev_alloc_skb(size)         ec_skb_alloc(size)
#define dev_kfree_skb(skb)          ec_skb_free(skb)
#define skb_put(skb, len)           ec_skb_put(skb, len)
#define eth_type_trans(skb, dev)    ec_eth_type_trans(skb, dev)

/* Queue management */
#define netif_start_queue(dev)      ec_netdev_start_queue(dev)
#define netif_stop_queue(dev)       ec_netdev_stop_queue(dev)
#define netif_wake_queue(dev)       ec_netdev_wake_queue(dev)
#define netif_tx_lock_bh(dev)       ec_netdev_tx_lock(dev)
#define netif_tx_unlock_bh(dev)     ec_netdev_tx_unlock(dev)

/* RX path */
#define netif_rx(skb)               ec_netif_rx(skb)

/* Checksum */
#define CHECKSUM_UNNECESSARY        EC_CHECKSUM_UNNECESSARY

/* Rate limiting */
#define printk_ratelimit()          ec_printk_ratelimit()

/* Memory (direct mapping) */
#define kmalloc(size, flags)        malloc(size)
#define kfree(ptr)                  free(ptr)
#define GFP_ATOMIC                  0

/* Stubs for kernel-only APIs */
#define WARN_ON_ONCE(x)             ((void)(x))
#define lockdep_assert_held(x)      ((void)0)
#define skb_get_queue_mapping(skb)  0
#define netdev_get_tx_queue(dev, n) NULL

/* MAC address setting */
#define eth_hw_addr_set(dev, mac)   ec_netdev_set_mac(dev, mac)

/* Ethernet setup callback (no-op in userspace) */
static inline void ether_setup(ec_netdev_t *dev) {
    dev->mtu = 1500;
}

/* Error pointer macros */
#define IS_ERR(ptr)     ((unsigned long)(ptr) >= (unsigned long)-4096)
#define PTR_ERR(ptr)    ((long)(ptr))
#define ERR_PTR(err)    ((void *)((long)(err)))

#endif /* __EC_USPACE_PAL_EOE_COMPAT_H__ */
