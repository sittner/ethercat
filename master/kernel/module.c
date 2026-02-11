/*****************************************************************************
 *
 *  Copyright (C) 2006-2008  Florian Pose, Ingenieurgemeinschaft IgH
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

/** \file
 * EtherCAT master driver module.
 */

/****************************************************************************/


#include "pal.h"
#include "priv.h"

#include "../globals.h"
#include "../master.h"
#include "../device.h"

/****************************************************************************/

/****************************************************************************/

int __init ec_init_module(void);
void __exit ec_cleanup_module(void);

// prototypes for private functions
int ec_mac_equal(const uint8_t *, const uint8_t *);
int ec_mac_is_broadcast(const uint8_t *);

int ec_master_pal_init(ec_master_t *, unsigned int, const uint8_t *,
    const uint8_t *, dev_t, struct class *, unsigned int, unsigned int);
void ec_master_pal_clear(ec_master_t *);

/****************************************************************************/

static char *main_devices[EC_MAX_MASTERS]; /**< Main devices parameter. */
static unsigned int master_count; /**< Number of masters. */
static char *backup_devices[EC_MAX_MASTERS]; /**< Backup devices parameter. */
static unsigned int backup_count; /**< Number of backup devices. */
static unsigned int debug_level;  /**< Debug level parameter. */
static unsigned int run_on_cpu = 0xffffffff; /**< Bind created kernel threads
                                               to a cpu. Default do not bind.
                                              */

static ec_master_t *masters; /**< Array of masters. */
static struct semaphore master_sem; /**< Master semaphore. */

dev_t device_number; /**< Device number for master cdevs. */
struct class *class; /**< Device class. */

static uint8_t macs[EC_MAX_MASTERS][2][ETH_ALEN]; /**< MAC addresses. */

/****************************************************************************/

/** \cond */

MODULE_AUTHOR("Florian Pose <fp@igh.de>");
MODULE_DESCRIPTION("EtherCAT master driver module");
MODULE_LICENSE("GPL");
MODULE_VERSION(EC_MASTER_VERSION);

module_param_array(main_devices, charp, &master_count, S_IRUGO);
MODULE_PARM_DESC(main_devices, "MAC addresses of main devices");
module_param_array(backup_devices, charp, &backup_count, S_IRUGO);
MODULE_PARM_DESC(backup_devices, "MAC addresses of backup devices");
module_param_named(debug_level, debug_level, uint, S_IRUGO);
MODULE_PARM_DESC(debug_level, "Debug level");
module_param_named(run_on_cpu, run_on_cpu, uint, S_IRUGO);
MODULE_PARM_DESC(run_on_cpu, "Bind kthreads to a specific cpu");

/** \endcond */

/****************************************************************************/

/** Module initialization.
 *
 * Initializes \a master_count masters.
 * \return 0 on success, else < 0
 */
int __init ec_init_module(void)
{
    int i, ret = 0;

    EC_INFO("Master driver %s\n", EC_MASTER_VERSION);

    sema_init(&master_sem, 1);

    if (master_count) {
        if (alloc_chrdev_region(&device_number,
                    0, master_count, "EtherCAT")) {
            EC_ERR("Failed to obtain device number(s)!\n");
            ret = -EBUSY;
            goto out_return;
        }
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    class = class_create(THIS_MODULE, "EtherCAT");
#else
    class = class_create("EtherCAT");
#endif
    if (IS_ERR(class)) {
        EC_ERR("Failed to create device class.\n");
        ret = PTR_ERR(class);
        goto out_cdev;
    }

    // zero MAC addresses
    memset(macs, 0x00, sizeof(uint8_t) * EC_MAX_MASTERS * 2 * ETH_ALEN);

    // process MAC parameters
    for (i = 0; i < master_count; i++) {
        ret = ec_mac_parse(macs[i][0], main_devices[i], 0);
        if (ret)
            goto out_class;

        if (i < backup_count) {
            ret = ec_mac_parse(macs[i][1], backup_devices[i], 1);
            if (ret)
                goto out_class;
        }
    }

    // initialize static master variables
    ec_master_init_static();

    if (master_count) {
        if (!(masters = kmalloc(sizeof(ec_master_t) * master_count,
                        GFP_KERNEL))) {
            EC_ERR("Failed to allocate memory"
                    " for EtherCAT masters.\n");
            ret = -ENOMEM;
            goto out_class;
        }
    }

    for (i = 0; i < master_count; i++) {
        ret = ec_master_pal_init(&masters[i], i, macs[i][0], macs[i][1],
                    device_number, class, debug_level, run_on_cpu);
        if (ret)
            goto out_free_masters;
    }

    EC_INFO("%u master%s waiting for devices.\n",
            master_count, (master_count == 1 ? "" : "s"));
    return ret;

out_free_masters:
    for (i--; i >= 0; i--)
        ec_master_pal_clear(&masters[i]);
    kfree(masters);
out_class:
    class_destroy(class);
out_cdev:
    if (master_count)
        unregister_chrdev_region(device_number, master_count);
out_return:
    return ret;
}

/****************************************************************************/

/** Module cleanup.
 *
 * Clears all master instances.
 */
void __exit ec_cleanup_module(void)
{
    unsigned int i;

    for (i = 0; i < master_count; i++) {
        ec_master_pal_clear(&masters[i]);
    }

    if (master_count)
        kfree(masters);

    class_destroy(class);

    if (master_count)
        unregister_chrdev_region(device_number, master_count);

    EC_INFO("Master module cleaned up.\n");
}

/****************************************************************************/

/** Get the number of masters.
 */
unsigned int ec_master_count(void)
{
    return master_count;
}

/****************************************************************************/

/** Platform specific init.
 */
int ec_master_pal_init(ec_master_t *master, /**< EtherCAT master */
        unsigned int index, /**< master index */
        const uint8_t *main_mac, /**< MAC address of main device */
        const uint8_t *backup_mac, /**< MAC address of backup device */
        dev_t device_number, /**< Character device number. */
        struct class *class, /**< Device class. */
        unsigned int debug_level, /**< Debug level (module parameter). */
        unsigned int run_on_cpu /**< bind created kernel threads to a cpu */
        )
{
    int ret;

    // init master
    ret = ec_master_init(master, index, main_mac, backup_mac,
               debug_level, run_on_cpu);
    if (ret)
        goto out_return;

    // init character device
    ret = ec_cdev_init(&master->pal.cdev, master, device_number);
    if (ret)
        goto out_free_master;

    master->pal.class_device = device_create(class, NULL,
            MKDEV(MAJOR(device_number), master->index), NULL,
            "EtherCAT%u", master->index);
    if (IS_ERR(master->pal.class_device)) {
        EC_MASTER_ERR(master, "Failed to create class device!\n");
        ret = PTR_ERR(master->pal.class_device);
        goto out_clear_cdev;
    }

#ifdef EC_RTDM
    // init RTDM device
    ret = ec_rtdm_dev_init(&master->pal.rtdm_dev, master);
    if (ret) {
        goto out_unregister_class_device;
    }
#endif
    return 0;

#ifdef EC_RTDM
out_unregister_class_device:
    device_unregister(master->pal.class_device);
#endif
out_clear_cdev:
    ec_cdev_clear(&master->pal.cdev);
out_free_master:
    ec_master_clear(master);
out_return:
    return ret;
}

/** Platform specific cleanup.
 */
void ec_master_pal_clear(
        ec_master_t *master /**< EtherCAT master */
        )
{
#ifdef EC_RTDM
    ec_rtdm_dev_clear(&master->pal.rtdm_dev);
#endif

    device_unregister(master->pal.class_device);

    ec_cdev_clear(&master->pal.cdev);

    ec_master_clear(master);
}

/*****************************************************************************
 *  Device interface
 ****************************************************************************/

/** Offers an EtherCAT device to a certain master.
 *
 * The master decides, if it wants to use the device for EtherCAT operation,
 * or not. It is important, that the offered net_device is not used by the
 * kernel IP stack. If the master, accepted the offer, the address of the
 * newly created EtherCAT device is returned, else \a NULL is returned.
 *
 * \return Pointer to device, if accepted, or NULL if declined.
 * \ingroup DeviceInterface
 */
ec_device_t *ecdev_offer(
        struct net_device *net_dev, /**< net_device to offer */
        ec_pollfunc_t poll, /**< device poll function */
        struct module *module /**< pointer to the module */
        )
{
    ec_master_t *master;
    char str[EC_MAX_MAC_STRING_SIZE];
    unsigned int i, dev_idx;

    for (i = 0; i < master_count; i++) {
        master = &masters[i];
        ec_mac_print(net_dev->dev_addr, str);

        if (down_interruptible(&master->device_sem)) {
            EC_MASTER_WARN(master, "%s() interrupted!\n", __func__);
            return NULL;
        }

        for (dev_idx = EC_DEVICE_MAIN;
                dev_idx < ec_master_num_devices(master); dev_idx++) {
            if (!master->devices[dev_idx].pal.dev
                && (ec_mac_equal(master->macs[dev_idx], net_dev->dev_addr)
                    || ec_mac_is_broadcast(master->macs[dev_idx]))) {

                EC_INFO("Accepting %s as %s device for master %u.\n",
                        str, ec_device_names[dev_idx != 0], master->index);

                ec_device_attach(&master->devices[dev_idx],
                        net_dev, poll, module);
                up(&master->device_sem);

                snprintf(net_dev->name, IFNAMSIZ, "ec%c%u",
                        ec_device_names[dev_idx != 0][0], master->index);

                return &master->devices[dev_idx]; // offer accepted
            }
        }

        up(&master->device_sem);

        EC_MASTER_DBG(master, 1, "Master declined device %s.\n", str);
    }

    return NULL; // offer declined
}

/*****************************************************************************
 * Application interface
 ****************************************************************************/

/** Request a master.
 *
 * Same as ecrt_request_master(), but with ERR_PTR() return value.
 *
 * \return Requested master.
 */
ec_master_t *ecrt_request_master_err(
        unsigned int master_index /**< Master index. */
        )
{
    ec_master_t *master, *errptr = NULL;
    unsigned int dev_idx = EC_DEVICE_MAIN;

    EC_INFO("Requesting master %u...\n", master_index);

    if (master_index >= master_count) {
        EC_ERR("Invalid master index %u.\n", master_index);
        errptr = ERR_PTR(-EINVAL);
        goto out_return;
    }
    master = &masters[master_index];

    if (down_interruptible(&master_sem)) {
        errptr = ERR_PTR(-EINTR);
        goto out_return;
    }

    if (master->reserved) {
        up(&master_sem);
        EC_MASTER_ERR(master, "Master already in use!\n");
        errptr = ERR_PTR(-EBUSY);
        goto out_return;
    }
    master->reserved = 1;
    up(&master_sem);

    if (down_interruptible(&master->device_sem)) {
        errptr = ERR_PTR(-EINTR);
        goto out_release;
    }

    if (master->phase != EC_IDLE) {
        up(&master->device_sem);
        EC_MASTER_ERR(master, "Master still waiting for devices!\n");
        errptr = ERR_PTR(-ENODEV);
        goto out_release;
    }

    for (; dev_idx < ec_master_num_devices(master); dev_idx++) {
        ec_device_t *device = &master->devices[dev_idx];
        if (!try_module_get(device->pal.module)) {
            up(&master->device_sem);
            EC_MASTER_ERR(master, "Device module is unloading!\n");
            errptr = ERR_PTR(-ENODEV);
            goto out_module_put;
        }
    }

    up(&master->device_sem);

    if (ec_master_enter_operation_phase(master)) {
        EC_MASTER_ERR(master, "Failed to enter OPERATION phase!\n");
        errptr = ERR_PTR(-EIO);
        goto out_module_put;
    }

    EC_INFO("Successfully requested master %u.\n", master_index);
    return master;

 out_module_put:
    for (; dev_idx > 0; dev_idx--) {
        ec_device_t *device = &master->devices[dev_idx - 1];
        module_put(device->pal.module);
    }
 out_release:
    master->reserved = 0;
 out_return:
    return errptr;
}

/****************************************************************************/

ec_master_t *ecrt_request_master(unsigned int master_index)
{
    ec_master_t *master = ecrt_request_master_err(master_index);
    return IS_ERR(master) ? NULL : master;
}

/****************************************************************************/

void ecrt_release_master(ec_master_t *master)
{
    unsigned int dev_idx;

    EC_MASTER_INFO(master, "Releasing master...\n");

    if (!master->reserved) {
        EC_MASTER_WARN(master, "%s(): Master was was not requested!\n",
                __func__);
        return;
    }

    ec_master_leave_operation_phase(master);

    for (dev_idx = EC_DEVICE_MAIN; dev_idx < ec_master_num_devices(master);
            dev_idx++) {
        module_put(master->devices[dev_idx].pal.module);
    }

    master->reserved = 0;

    EC_MASTER_INFO(master, "Released.\n");
}

/****************************************************************************/

unsigned int ecrt_version_magic(void)
{
    return ECRT_VERSION_MAGIC;
}

/****************************************************************************/

/** \cond */

module_init(ec_init_module);
module_exit(ec_cleanup_module);

EXPORT_SYMBOL(ecdev_offer);

EXPORT_SYMBOL(ecrt_request_master);
EXPORT_SYMBOL(ecrt_release_master);
EXPORT_SYMBOL(ecrt_version_magic);

/** \endcond */

/****************************************************************************/
