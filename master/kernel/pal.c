#include "pal.h"
#include "priv.h"

#include "../device.h"
#include "../master.h"


enum {
    /* genet driver needs extra headroom in skb for status block */
    EXTRA_HEADROOM = 64,
};

/** Constructor.
 *
 * \return 0 in case of success, else < 0
 */
int ec_device_init(
        ec_device_t *device, /**< EtherCAT device */
        ec_master_t *master /**< master owning the device */
        )
{
    int ret;
    unsigned int i;
    struct ethhdr *eth;
#ifdef EC_DEBUG_IF
    char ifname[10];
    char mb = 'x';
#endif

    // TODO: shared init
    device->master = master;
    device->pal.dev = NULL;
    device->name = NULL;
    device->pal.poll = NULL;
    device->pal.module = NULL;
    device->open = 0;
    device->link_state = 0;
    for (i = 0; i < EC_TX_RING_SIZE; i++) {
        device->pal.tx_skb[i] = NULL;
    }
    device->pal.tx_ring_index = 0;
#ifdef EC_HAVE_CYCLES
    device->cycles_poll = 0;
#endif
#ifdef EC_DEBUG_RING
    device->timeval_poll.tv_sec = 0;
    device->timeval_poll.tv_usec = 0;
#endif
    device->jiffies_poll = 0;

    ec_device_clear_stats(device);

#ifdef EC_DEBUG_RING
    for (i = 0; i < EC_DEBUG_RING_SIZE; i++) {
        ec_debug_frame_t *df = &device->debug_frames[i];
        df->dir = TX;
        df->t.tv_sec = 0;
        df->t.tv_usec = 0;
        memset(df->data, 0, EC_MAX_DATA_SIZE);
        df->data_size = 0;
    }
#endif
#ifdef EC_DEBUG_RING
    device->debug_frame_index = 0;
    device->debug_frame_count = 0;
#endif

#ifdef EC_DEBUG_IF
    if (device == &master->devices[EC_DEVICE_MAIN]) {
        mb = 'm';
    }
    else {
        mb = 'b';
    }

    sprintf(ifname, "ecdbg%c%u", mb, master->index);

    ret = ec_debug_init(&device->dbg, device, ifname);
    if (ret < 0) {
        EC_MASTER_ERR(master, "Failed to init debug device!\n");
        goto out_return;
    }
#endif

    for (i = 0; i < EC_TX_RING_SIZE; i++) {
        if (!(device->pal.tx_skb[i] = dev_alloc_skb(ETH_FRAME_LEN + EXTRA_HEADROOM))) {
            EC_MASTER_ERR(master, "Error allocating device socket buffer!\n");
            ret = -ENOMEM;
            goto out_tx_ring;
        }

        // add Ethernet-II-header
        skb_reserve(device->pal.tx_skb[i], ETH_HLEN + EXTRA_HEADROOM);
        eth = (struct ethhdr *) skb_push(device->pal.tx_skb[i], ETH_HLEN);
        eth->h_proto = htons(0x88A4);
        memset(eth->h_dest, 0xFF, ETH_ALEN);
    }

    return 0;

out_tx_ring:
    for (i = 0; i < EC_TX_RING_SIZE; i++) {
        if (device->pal.tx_skb[i]) {
            dev_kfree_skb(device->pal.tx_skb[i]);
        }
    }
#ifdef EC_DEBUG_IF
    ec_debug_clear(&device->dbg);
out_return:
#endif
    return ret;
}

/****************************************************************************/

/** Destructor.
 */
void ec_device_clear(
        ec_device_t *device /**< EtherCAT device */
        )
{
    unsigned int i;

    if (device->open) {
        ec_device_close(device);
    }
    for (i = 0; i < EC_TX_RING_SIZE; i++)
        dev_kfree_skb(device->pal.tx_skb[i]);
#ifdef EC_DEBUG_IF
    ec_debug_clear(&device->dbg);
#endif
}

/****************************************************************************/

/** Associate with net_device.
 */
void ec_device_attach(
        ec_device_t *device, /**< EtherCAT device */
        struct net_device *net_dev, /**< net_device structure */
        ec_pollfunc_t poll, /**< pointer to device's poll function */
        struct module *module /**< the device's module */
        )
{
    unsigned int i;
    struct ethhdr *eth;

    ec_device_detach(device); // resets fields

    device->pal.dev = net_dev;
    device->name = net_dev->name;
    device->pal.poll = poll;
    device->pal.module = module;

    for (i = 0; i < EC_TX_RING_SIZE; i++) {
        device->pal.tx_skb[i]->dev = net_dev;
        eth = (struct ethhdr *) (device->pal.tx_skb[i]->data);
        memcpy(eth->h_source, net_dev->dev_addr, ETH_ALEN);
    }

#ifdef EC_DEBUG_IF
    ec_debug_register(&device->dbg, net_dev);
#endif
}

/****************************************************************************/

/** Disconnect from net_device.
 */
void ec_device_detach(
        ec_device_t *device /**< EtherCAT device */
        )
{
    unsigned int i;

#ifdef EC_DEBUG_IF
    ec_debug_unregister(&device->dbg);
#endif

    device->pal.dev = NULL;
    device->name = NULL;
    device->pal.poll = NULL;
    device->pal.module = NULL;
    device->open = 0;
    device->link_state = 0; // down

    ec_device_clear_stats(device);

    for (i = 0; i < EC_TX_RING_SIZE; i++) {
        device->pal.tx_skb[i]->dev = NULL;
    }
}

/****************************************************************************/

/** Opens the EtherCAT device.
 *
 * \return 0 in case of success, else < 0
 */
int ec_device_open(
        ec_device_t *device /**< EtherCAT device */
        )
{
    int ret;

    if (!device->pal.dev) {
        EC_MASTER_ERR(device->master, "No net_device to open!\n");
        return -ENODEV;
    }

    if (device->open) {
        EC_MASTER_WARN(device->master, "Device already opened!\n");
        return 0;
    }

    device->link_state = 0;

    ec_device_clear_stats(device);

    ret = device->pal.dev->netdev_ops->ndo_open(device->pal.dev);
    if (!ret)
        device->open = 1;

    return ret;
}

/****************************************************************************/

/** Stops the EtherCAT device.
 *
 * \return 0 in case of success, else < 0
 */
int ec_device_close(
        ec_device_t *device /**< EtherCAT device */
        )
{
    int ret;

    if (!device->pal.dev) {
        EC_MASTER_ERR(device->master, "No device to close!\n");
        return -ENODEV;
    }

    if (!device->open) {
        EC_MASTER_WARN(device->master, "Device already closed!\n");
        return 0;
    }

    ret = device->pal.dev->netdev_ops->ndo_stop(device->pal.dev);
    if (!ret)
        device->open = 0;

    return ret;
}

/****************************************************************************/

/** Returns a pointer to the device's transmit memory.
 *
 * \return pointer to the TX socket buffer
 */
uint8_t *ec_device_tx_data(
        ec_device_t *device /**< EtherCAT device */
        )
{
    /* cycle through socket buffers, because otherwise there is a race
     * condition, if multiple frames are sent and the DMA is not scheduled in
     * between. */
    device->pal.tx_ring_index++;
    device->pal.tx_ring_index %= EC_TX_RING_SIZE;
    return device->pal.tx_skb[device->pal.tx_ring_index]->data + ETH_HLEN;
}

/****************************************************************************/

/** Sends the content of the transmit socket buffer.
 *
 * Cuts the socket buffer content to the (now known) size, and calls the
 * start_xmit() function of the assigned net_device.
 */
void ec_device_send(
        ec_device_t *device, /**< EtherCAT device */
        size_t size /**< number of bytes to send */
        )
{
    struct sk_buff *skb = device->pal.tx_skb[device->pal.tx_ring_index];

    // set the right length for the data
    skb->len = ETH_HLEN + size;

    if (unlikely(device->master->debug_level > 1)) {
        EC_MASTER_DBG(device->master, 2, "Sending frame:\n");
        ec_print_data(skb->data, ETH_HLEN + size);
    }

    // start sending
    if (device->pal.dev->netdev_ops->ndo_start_xmit(skb, device->pal.dev) ==
            NETDEV_TX_OK)
    {
    	// TODO: need function in device.c
        device->tx_count++;
        device->master->device_stats.tx_count++;
        device->tx_bytes += ETH_HLEN + size;
        device->master->device_stats.tx_bytes += ETH_HLEN + size;
#ifdef EC_DEBUG_IF
        ec_debug_send(&device->dbg, skb->data, ETH_HLEN + size);
#endif
#ifdef EC_DEBUG_RING
        ec_device_debug_ring_append(
                device, TX, skb->data + ETH_HLEN, size);
#endif
    } else {
        device->tx_errors++;
    }
}

/****************************************************************************/

/** Calls the poll function of the assigned net_device.
 *
 * The master itself works without using interrupts. Therefore the processing
 * of received data and status changes of the network device has to be
 * done by the master calling the ISR "manually".
 */
void ec_device_poll(
        ec_device_t *device /**< EtherCAT device */
        )
{
#ifdef EC_HAVE_CYCLES
    device->cycles_poll = get_cycles();
#endif
    device->jiffies_poll = jiffies;
#ifdef EC_DEBUG_RING
    do_gettimeofday(&device->timeval_poll);
#endif
    device->pal.poll(device->pal.dev);
}


EXPORT_SYMBOL(ecdev_withdraw);
EXPORT_SYMBOL(ecdev_open);
EXPORT_SYMBOL(ecdev_close);
EXPORT_SYMBOL(ecdev_receive);
EXPORT_SYMBOL(ecdev_get_link);
EXPORT_SYMBOL(ecdev_set_link);

