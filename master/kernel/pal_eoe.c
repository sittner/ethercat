/*****************************************************************************
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
 ****************************************************************************/
/**
   \file
   Platform-specific EoE net_device lifecycle and callback functions (kernel).
*/
/****************************************************************************/
#include "pal.h"
#include "../globals.h"
#include "../master.h"
#include "../ethernet.h"
/****************************************************************************/
/** Defines the debug level of EoE processing.
 *
 * 0 = No debug messages.
 * 1 = Output warnings.
 * 2 = Output actions.
 * 3 = Output actions and frame data.
 */
#define EOE_DEBUG_LEVEL 1
/****************************************************************************/
// net_device callback forward declarations
static int ec_eoedev_open(struct net_device *);
static int ec_eoedev_stop(struct net_device *);
static int ec_eoedev_tx(struct sk_buff *, struct net_device *);
static struct net_device_stats *ec_eoedev_stats(struct net_device *);
/** Device operations for EoE interfaces.
 */
static const struct net_device_ops ec_eoe_netdev_ops =
{
    .ndo_open = ec_eoedev_open,
    .ndo_stop = ec_eoedev_stop,
    .ndo_start_xmit = ec_eoedev_tx,
    .ndo_get_stats = ec_eoedev_stats,
};
/****************************************************************************/
/** Opens the virtual network device.
 *
 * \return Always zero (success).
 */
static int ec_eoedev_open(struct net_device *dev /**< EoE net_device */)
{
    ec_eoe_t *eoe = *((ec_eoe_t **) netdev_priv(dev));
    ec_eoe_flush(eoe);
    eoe->opened = 1;
    eoe->rx_idle = 0;
    eoe->tx_idle = 0;
    netif_start_queue(dev);
    eoe->tx_queue_active = 1;
#if EOE_DEBUG_LEVEL >= 2
    EC_SLAVE_DBG(eoe->slave, 0, "%s opened.\n", ec_eoedev_name(dev));
#endif
    return 0;
}
/****************************************************************************/
/** Stops the virtual network device.
 *
 * \return Always zero (success).
 */
static int ec_eoedev_stop(struct net_device *dev /**< EoE net_device */)
{
    ec_eoe_t *eoe = *((ec_eoe_t **) netdev_priv(dev));
    netif_stop_queue(dev);
    eoe->rx_idle = 1;
    eoe->tx_idle = 1;
    eoe->tx_queue_active = 0;
    eoe->opened = 0;
    ec_eoe_flush(eoe);
#if EOE_DEBUG_LEVEL >= 2
    EC_SLAVE_DBG(eoe->slave, 0, "%s stopped.\n", ec_eoedev_name(dev));
#endif
    return 0;
}
/****************************************************************************/
/** Transmits data via the virtual network device.
 *
 * \return Zero on success, non-zero on failure.
 */
static int ec_eoedev_tx(struct sk_buff *skb, /**< transmit socket buffer */
                        struct net_device *dev /**< EoE net_device */
                       )
{
    ec_eoe_t *eoe = *((ec_eoe_t **) netdev_priv(dev));
    ec_eoe_frame_t *frame;
#if 0
    if (skb->len > eoe->slave->configured_tx_mailbox_size - 10) {
        EC_SLAVE_WARN(eoe->slave, "EoE TX frame (%u octets)"
                " exceeds MTU. dropping.\n", skb->len);
        dev_kfree_skb(skb);
        eoe->stats.tx_dropped++;
        return 0;
    }
#endif
    WARN_ON_ONCE(skb_get_queue_mapping(skb) != 0);
    lockdep_assert_held(&netdev_get_tx_queue(dev, 0)->_xmit_lock);
    if (!(frame = ec_alloc_atomic(sizeof(ec_eoe_frame_t)))) {
        if (ec_log_ratelimit())
            EC_SLAVE_WARN(eoe->slave, "EoE TX: low on mem. frame dropped.\n");
        return 1;
    }
    frame->skb = skb;
    list_add_tail(&frame->queue, &eoe->tx_queue);
    eoe->tx_queued_frames++;
    if (eoe->tx_queued_frames == eoe->tx_queue_size) {
        netif_stop_queue(dev);
        eoe->tx_queue_active = 0;
    }
#if EOE_DEBUG_LEVEL >= 2
    EC_SLAVE_DBG(eoe->slave, 0, "EoE %s TX queued frame"
            " with %u octets (%u frames queued).\n",
            ec_eoedev_name(eoe->dev), skb->len, eoe->tx_queued_frames);
    if (!eoe->tx_queue_active)
        EC_SLAVE_WARN(eoe->slave, "EoE TX queue is now full.\n");
#endif
    return 0;
}
/****************************************************************************/
/** Gets statistics about the virtual network device.
 *
 * \return Statistics.
 */
static struct net_device_stats *ec_eoedev_stats(
        struct net_device *dev /**< EoE net_device */
        )
{
    ec_eoe_t *eoe = *((ec_eoe_t **) netdev_priv(dev));
    return &eoe->stats;
}
/****************************************************************************/
/** Creates and registers the EoE net_device.
 *
 * Allocates the net_device, sets up the MAC address, assigns the device
 * operations, registers it with the kernel network stack, and makes the
 * last MAC octet unique using the interface index.
 *
 * \return Zero on success, otherwise a negative error code.
 */
int ec_eoe_netdev_create(struct ec_eoe *eoe, const char *name)
{
    ec_eoe_t **priv;
    int ret;
    uint8_t mac_addr[ETH_ALEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    eoe->dev = alloc_netdev(sizeof(ec_eoe_t *), name, NET_NAME_UNKNOWN,
            ether_setup);
    if (!eoe->dev) {
        EC_SLAVE_ERR(eoe->slave, "Unable to allocate net_device %s"
                " for EoE handler!\n", name);
        return -ENODEV;
    }
    // initialize net_device
    eth_hw_addr_set(eoe->dev, mac_addr);
    // initialize private data
    priv = netdev_priv(eoe->dev);
    *priv = eoe;
    eoe->dev->netdev_ops = &ec_eoe_netdev_ops;
    // connect the net_device to the kernel
    ret = register_netdev(eoe->dev);
    if (ret) {
        EC_SLAVE_ERR(eoe->slave, "Unable to register net_device:"
                " error %i\n", ret);
        free_netdev(eoe->dev);
        eoe->dev = NULL;
        return ret;
    }
    // make the last address octet unique
    mac_addr[ETH_ALEN - 1] = (uint8_t) ec_eoe_netdev_ifindex(eoe->dev);
    eth_hw_addr_set(eoe->dev, mac_addr);
    return 0;
}
/****************************************************************************/
/** Unregisters and frees the EoE net_device.
 */
void ec_eoe_netdev_destroy(struct ec_eoe *eoe)
{
    if (eoe->dev) {
        unregister_netdev(eoe->dev);
        free_netdev(eoe->dev);
        eoe->dev = NULL;
    }
}