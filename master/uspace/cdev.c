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
   Userspace EtherCAT master IPC character device server.

   Implements a Unix domain socket server that allows the \a ethercat
   command-line tool to communicate with a userspace master running inside
   an application process.  The protocol matches the \c ec_ipc_request_t /
   \c ec_ipc_response_t wire format defined in \c master/ec_ioctl_data.h.
*/

/****************************************************************************/

#include "pal.h"

#include "../master.h"
#include "../slave.h"
#include "../slave_config.h"
#include "../domain.h"
#include "../sdo.h"
#include "../sdo_entry.h"
#include "../pdo.h"
#include "../pdo_list.h"
#include "../pdo_entry.h"
#include "../master_globals.h"
#include "../ec_ioctl_data.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX (sizeof(((struct sockaddr_un *)0)->sun_path))
#endif

/****************************************************************************/

/** Global singleton IPC server state. */
typedef struct {
    int              sock_fd;        /**< Listening socket fd (-1 if inactive). */
    char             sock_path[UNIX_PATH_MAX]; /**< Unix socket filesystem path. */
    ec_thread_t     *thread;         /**< Listener thread handle. */
    atomic_int       shutdown;       /**< Non-zero to request shutdown. */
} ec_cdev_t;

static ec_cdev_t g_cdev = { .sock_fd = -1 };

/****************************************************************************/

/** Global master registry — protected by registry_rwlock. */
static ec_master_t *master_registry[EC_MAX_MASTERS];
static unsigned int registry_master_count;
static pthread_rwlock_t registry_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/****************************************************************************/

void ec_master_registry_add(ec_master_t *master)
{
    if (!master || master->index >= EC_MAX_MASTERS)
        return;
    pthread_rwlock_wrlock(&registry_rwlock);
    if (master_registry[master->index] == NULL)
        registry_master_count++;
    master_registry[master->index] = master;
    pthread_rwlock_unlock(&registry_rwlock);
}

void ec_master_registry_remove(ec_master_t *master)
{
    if (!master || master->index >= EC_MAX_MASTERS)
        return;
    pthread_rwlock_wrlock(&registry_rwlock);
    if (master_registry[master->index] == master) {
        master_registry[master->index] = NULL;
        if (registry_master_count > 0)
            registry_master_count--;
    }
    pthread_rwlock_unlock(&registry_rwlock);
}

ec_master_t *ec_master_registry_find(unsigned int index)
{
    ec_master_t *master;
    if (index >= EC_MAX_MASTERS)
        return NULL;
    pthread_rwlock_rdlock(&registry_rwlock);
    master = master_registry[index];
    pthread_rwlock_unlock(&registry_rwlock);
    return master;
}

/** Acquire the registry read lock and return the master pointer.
 * The read lock is held on return and must be released by
 * ec_master_registry_put().  Returns NULL (with lock released) if not found. */
static ec_master_t *ec_master_registry_get(unsigned int index)
{
    ec_master_t *master;
    if (index >= EC_MAX_MASTERS)
        return NULL;
    pthread_rwlock_rdlock(&registry_rwlock);
    master = master_registry[index];
    if (!master)
        pthread_rwlock_unlock(&registry_rwlock);
    return master;
}

/** Release the registry read lock acquired by ec_master_registry_get(). */
static void ec_master_registry_put(ec_master_t *master)
{
    if (master)
        pthread_rwlock_unlock(&registry_rwlock);
}

unsigned int ec_master_registry_count(void)
{
    unsigned int count;
    pthread_rwlock_rdlock(&registry_rwlock);
    count = registry_master_count;
    pthread_rwlock_unlock(&registry_rwlock);
    return count;
}

/****************************************************************************/
/* Helper: copy string into fixed-size ioctl buffer (NUL-terminated).        */
/****************************************************************************/

static void ipc_strcpy(char *target, const char *source)
{
    if (source) {
        strncpy(target, source, EC_IOCTL_STRING_SIZE);
        target[EC_IOCTL_STRING_SIZE - 1] = '\0';
    } else {
        target[0] = '\0';
    }
}

/****************************************************************************/
/* Helper: send exactly \a len bytes; retry on EINTR.                         */
/****************************************************************************/

static int send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = send(fd, p, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0)
                return -errno;
            return -ECONNRESET;
        }
        p += n;
        remaining -= (size_t)n;
    }
    return 0;
}

/****************************************************************************/
/* Helper: receive exactly \a len bytes; return 0 on success, -errno on error,
 * -ECONNRESET on clean EOF.                                                   */
/****************************************************************************/

static int recv_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = recv(fd, p, remaining, MSG_WAITALL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        if (n == 0)
            return -ECONNRESET;
        p += n;
        remaining -= (size_t)n;
    }
    return 0;
}

/****************************************************************************/
/* Helper: send a framed IPC response.                                        */
/****************************************************************************/

static int send_response(int fd, int32_t ret_val,
        const void *data, uint32_t data_size)
{
    ec_ipc_response_t resp;
    int ret;

    resp.ret       = ret_val;
    resp.data_size = data_size;

    ret = send_all(fd, &resp, sizeof(resp));
    if (ret < 0)
        return ret;

    if (data && data_size > 0)
        ret = send_all(fd, data, data_size);

    return ret;
}

/** Send a response with both struct data and a trailing data blob. */
static int send_response_with_trailing(int fd, int32_t ret_val,
        const void *data, uint32_t data_size,
        const void *trailing, uint32_t trailing_size)
{
    ec_ipc_response_t resp;
    int ret;

    resp.ret       = ret_val;
    resp.data_size = data_size + trailing_size;

    ret = send_all(fd, &resp, sizeof(resp));
    if (ret < 0)
        return ret;

    if (data && data_size > 0) {
        ret = send_all(fd, data, data_size);
        if (ret < 0)
            return ret;
    }

    if (trailing && trailing_size > 0)
        ret = send_all(fd, trailing, trailing_size);

    return ret;
}

/****************************************************************************/
/* Command dispatch                                                            */
/****************************************************************************/

/** EC_CMD_MODULE — return version magic and master count. */
static int dispatch_module(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_module_t io;
    (void)master;
    (void)req;
    (void)req_size;

    io.ioctl_version_magic = EC_IOCTL_VERSION_MAGIC;
    io.master_count = (uint32_t)ec_master_registry_count();
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_MASTER — return master status. */
static int dispatch_master(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_master_t io;
    unsigned int dev_idx, j;
    (void)req;
    (void)req_size;

    memset(&io, 0, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    io.slave_count  = master->slave_count;
    io.scan_index   = master->scan_index;
    io.config_count = ec_master_config_count(master);
    io.domain_count = ec_master_domain_count(master);
#ifdef EC_EOE
    io.eoe_handler_count = ec_master_eoe_handler_count(master);
#else
    io.eoe_handler_count = 0;
#endif
    io.phase     = (uint8_t)master->phase;
    io.active    = (uint8_t)master->active;
    io.scan_busy = master->scan_busy;

    ec_sem_up(&master->master_sem);

    if (ec_sem_down_interruptible(&master->device_sem))
        return send_response(fd, -EINTR, NULL, 0);

    for (dev_idx = EC_DEVICE_MAIN;
            dev_idx < ec_master_num_devices(master); dev_idx++) {
        ec_device_t *device = &master->devices[dev_idx];

        /* In userspace there is no net_device; use stored MAC address. */
        memcpy(io.devices[dev_idx].address, master->macs[dev_idx], ETH_ALEN);
        io.devices[dev_idx].attached   = 1;
        io.devices[dev_idx].link_state = device->link_state ? 1 : 0;
        io.devices[dev_idx].tx_count   = device->tx_count;
        io.devices[dev_idx].rx_count   = device->rx_count;
        io.devices[dev_idx].tx_bytes   = device->tx_bytes;
        io.devices[dev_idx].rx_bytes   = device->rx_bytes;
        io.devices[dev_idx].tx_errors  = device->tx_errors;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            io.devices[dev_idx].tx_frame_rates[j] = device->tx_frame_rates[j];
            io.devices[dev_idx].rx_frame_rates[j] = device->rx_frame_rates[j];
            io.devices[dev_idx].tx_byte_rates[j]  = device->tx_byte_rates[j];
            io.devices[dev_idx].rx_byte_rates[j]  = device->rx_byte_rates[j];
        }
    }
    io.num_devices = ec_master_num_devices(master);

    io.tx_count = master->device_stats.tx_count;
    io.rx_count = master->device_stats.rx_count;
    io.tx_bytes = master->device_stats.tx_bytes;
    io.rx_bytes = master->device_stats.rx_bytes;
    for (j = 0; j < EC_RATE_COUNT; j++) {
        io.tx_frame_rates[j] = master->device_stats.tx_frame_rates[j];
        io.rx_frame_rates[j] = master->device_stats.rx_frame_rates[j];
        io.tx_byte_rates[j]  = master->device_stats.tx_byte_rates[j];
        io.rx_byte_rates[j]  = master->device_stats.rx_byte_rates[j];
        io.loss_rates[j]     = master->device_stats.loss_rates[j];
    }

    ec_sem_up(&master->device_sem);

    io.app_time    = master->app_time;
    io.dc_ref_time = master->dc_ref_time;
    io.ref_clock   = master->dc_ref_clock
        ? master->dc_ref_clock->ring_position : 0xffff;

    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_SLAVE — return slave information (input: slave position). */
static int dispatch_slave(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_t io;
    const ec_slave_t *slave;
    int i;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave_const(master, 0, io.position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n", io.position);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.device_index             = slave->device_index;
    io.vendor_id                = slave->sii.vendor_id;
    io.product_code             = slave->sii.product_code;
    io.revision_number          = slave->sii.revision_number;
    io.serial_number            = slave->sii.serial_number;
    io.alias                    = slave->effective_alias;
    io.boot_rx_mailbox_offset   = slave->sii.boot_rx_mailbox_offset;
    io.boot_rx_mailbox_size     = slave->sii.boot_rx_mailbox_size;
    io.boot_tx_mailbox_offset   = slave->sii.boot_tx_mailbox_offset;
    io.boot_tx_mailbox_size     = slave->sii.boot_tx_mailbox_size;
    io.std_rx_mailbox_offset    = slave->sii.std_rx_mailbox_offset;
    io.std_rx_mailbox_size      = slave->sii.std_rx_mailbox_size;
    io.std_tx_mailbox_offset    = slave->sii.std_tx_mailbox_offset;
    io.std_tx_mailbox_size      = slave->sii.std_tx_mailbox_size;
    io.mailbox_protocols        = slave->sii.mailbox_protocols;
    io.has_general_category     = slave->sii.has_general;
    io.coe_details              = slave->sii.coe_details;
    io.general_flags            = slave->sii.general_flags;
    io.current_on_ebus          = slave->sii.current_on_ebus;

    for (i = 0; i < EC_MAX_PORTS; i++) {
        io.ports[i].desc                    = slave->ports[i].desc;
        io.ports[i].link.link_up            = slave->ports[i].link.link_up;
        io.ports[i].link.loop_closed        = slave->ports[i].link.loop_closed;
        io.ports[i].link.signal_detected    =
            slave->ports[i].link.signal_detected;
        io.ports[i].receive_time            = slave->ports[i].receive_time;
        io.ports[i].next_slave              = slave->ports[i].next_slave
            ? slave->ports[i].next_slave->ring_position : 0xffff;
        io.ports[i].delay_to_next_dc        = slave->ports[i].delay_to_next_dc;
    }

    io.fmmu_bit              = slave->base_fmmu_bit_operation;
    io.dc_supported          = slave->base_dc_supported;
    io.dc_range              = slave->base_dc_range;
    io.has_dc_system_time    = slave->has_dc_system_time;
    io.transmission_delay    = slave->transmission_delay;
    io.al_state              = slave->current_state;
    io.error_flag            = slave->error_flag;
    io.sync_count            = slave->sii.sync_count;
    io.sdo_count             = ec_slave_sdo_count(slave);
    io.sii_nwords            = slave->sii_nwords;
    ipc_strcpy(io.group,  slave->sii.group);
    ipc_strcpy(io.image,  slave->sii.image);
    ipc_strcpy(io.order,  slave->sii.order);
    ipc_strcpy(io.name,   slave->sii.name);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_SLAVE_SYNC — slave sync manager information. */
static int dispatch_slave_sync(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sync_t io;
    const ec_slave_t *slave;
    const ec_sync_t *sync;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave_const(master, 0, io.slave_position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    if (io.sync_index >= slave->sii.sync_count) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    sync = &slave->sii.syncs[io.sync_index];
    io.physical_start_address = sync->physical_start_address;
    io.default_size           = sync->default_length;
    io.control_register       = sync->control_register;
    io.enable                 = sync->enable;
    io.pdo_count              = ec_pdo_list_count(&sync->pdos);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_SLAVE_SYNC_PDO — slave sync manager PDO information. */
static int dispatch_slave_sync_pdo(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sync_pdo_t io;
    const ec_slave_t *slave;
    const ec_sync_t *sync;
    const ec_pdo_t *pdo;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave_const(master, 0, io.slave_position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    if (io.sync_index >= slave->sii.sync_count) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    sync = &slave->sii.syncs[io.sync_index];
    pdo  = ec_pdo_list_find_pdo_by_pos_const(&sync->pdos, io.pdo_pos);
    if (!pdo) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.index       = pdo->index;
    io.entry_count = ec_pdo_entry_count(pdo);
    ipc_strcpy(io.name, pdo->name);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_SLAVE_SYNC_PDO_ENTRY — slave sync manager PDO entry information. */
static int dispatch_slave_sync_pdo_entry(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sync_pdo_entry_t io;
    const ec_slave_t *slave;
    const ec_sync_t *sync;
    const ec_pdo_t *pdo;
    const ec_pdo_entry_t *entry;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave_const(master, 0, io.slave_position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    if (io.sync_index >= slave->sii.sync_count) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    sync  = &slave->sii.syncs[io.sync_index];
    pdo   = ec_pdo_list_find_pdo_by_pos_const(&sync->pdos, io.pdo_pos);
    if (!pdo) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    entry = ec_pdo_find_entry_by_pos_const(pdo, io.entry_pos);
    if (!entry) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.index      = entry->index;
    io.subindex   = entry->subindex;
    io.bit_length = entry->bit_length;
    ipc_strcpy(io.name, entry->name);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_DOMAIN — domain information. */
static int dispatch_domain(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_domain_t io;
    const ec_domain_t *domain;
    unsigned int dev_idx;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    domain = ec_master_find_domain_const(master, io.index);
    if (!domain) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Domain %u does not exist!\n", io.index);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.data_size     = domain->data_size;
    io.logical_base_address = domain->logical_base_address;
    for (dev_idx = EC_DEVICE_MAIN;
            dev_idx < ec_master_num_devices(domain->master); dev_idx++) {
        io.working_counter[dev_idx] = domain->working_counter[dev_idx];
    }
    io.expected_working_counter = domain->expected_working_counter;
    io.fmmu_count               = ec_domain_fmmu_count(domain);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_DOMAIN_FMMU — domain FMMU information. */
static int dispatch_domain_fmmu(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_domain_fmmu_t io;
    const ec_domain_t *domain;
    const ec_fmmu_config_t *fmmu;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    domain = ec_master_find_domain_const(master, io.domain_index);
    if (!domain) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    fmmu = ec_domain_find_fmmu(domain, io.fmmu_index);
    if (!fmmu) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.slave_config_alias    = fmmu->sc->alias;
    io.slave_config_position = fmmu->sc->position;
    io.sync_index            = fmmu->sync_index;
    io.dir                   = fmmu->dir;
    io.logical_address       = fmmu->logical_start_address;
    io.data_size             = fmmu->data_size;

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_MASTER_DEBUG — set master debug level. */
static int dispatch_master_debug(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    uint32_t level;
    int ret;

    if (req_size != sizeof(uint32_t))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&level, req, sizeof(uint32_t));

    ret = ec_master_debug_level(master, (unsigned int)level);
    return send_response(fd, ret, NULL, 0);
}

/** EC_CMD_MASTER_RESCAN — trigger a bus rescan. */
static int dispatch_master_rescan(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    (void)req;
    (void)req_size;

    EC_MASTER_DBG(master, 1, "Got rescan command via IPC.\n");
    master->fsm.rescan_required = 1;
    return send_response(fd, 0, NULL, 0);
}

/** EC_CMD_SLAVE_STATE — request a slave state change. */
static int dispatch_slave_state(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_state_t io;
    ec_slave_t *slave;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave(master, 0, io.slave_position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                io.slave_position);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    ec_slave_request_state(slave, io.al_state);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, NULL, 0);
}

/** EC_CMD_SLAVE_SDO — slave SDO information. */
static int dispatch_slave_sdo(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sdo_t io;
    const ec_slave_t *slave;
    const ec_sdo_t *sdo;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave_const(master, 0, io.slave_position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    sdo = ec_slave_get_sdo_by_pos_const(slave, io.sdo_position);
    if (!sdo) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.sdo_index     = sdo->index;
    io.max_subindex  = sdo->max_subindex;
    ipc_strcpy(io.name, sdo->name);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_SLAVE_SDO_ENTRY — slave SDO entry information. */
static int dispatch_slave_sdo_entry(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sdo_entry_t io;
    const ec_slave_t *slave;
    const ec_sdo_t *sdo;
    const ec_sdo_entry_t *entry;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    slave = ec_master_find_slave_const(master, 0, io.slave_position);
    if (!slave) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    if (io.sdo_spec <= 0) {
        sdo = ec_slave_get_sdo_by_pos_const(slave,
                (uint16_t)(-io.sdo_spec));
    } else {
        sdo = ec_slave_get_sdo_const(slave, (uint16_t)io.sdo_spec);
    }
    if (!sdo) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    entry = ec_sdo_get_entry_const(sdo, io.sdo_entry_subindex);
    if (!entry) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.data_type    = entry->data_type;
    io.bit_length   = entry->bit_length;
    io.read_access[EC_SDO_ENTRY_ACCESS_PREOP]  =
        entry->read_access[EC_SDO_ENTRY_ACCESS_PREOP];
    io.read_access[EC_SDO_ENTRY_ACCESS_SAFEOP] =
        entry->read_access[EC_SDO_ENTRY_ACCESS_SAFEOP];
    io.read_access[EC_SDO_ENTRY_ACCESS_OP]     =
        entry->read_access[EC_SDO_ENTRY_ACCESS_OP];
    io.write_access[EC_SDO_ENTRY_ACCESS_PREOP]  =
        entry->write_access[EC_SDO_ENTRY_ACCESS_PREOP];
    io.write_access[EC_SDO_ENTRY_ACCESS_SAFEOP] =
        entry->write_access[EC_SDO_ENTRY_ACCESS_SAFEOP];
    io.write_access[EC_SDO_ENTRY_ACCESS_OP]     =
        entry->write_access[EC_SDO_ENTRY_ACCESS_OP];
    ipc_strcpy(io.description, entry->description);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_CONFIG — slave configuration information. */
static int dispatch_config(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_config_t io;
    const ec_slave_config_t *sc;
    uint8_t i;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    sc = ec_master_get_config_const(master, io.config_index);
    if (!sc) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave config %u does not exist!\n",
                io.config_index);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.alias        = sc->alias;
    io.position     = sc->position;
    io.vendor_id    = sc->vendor_id;
    io.product_code = sc->product_code;

    for (i = 0; i < EC_MAX_SYNC_MANAGERS; i++) {
        io.syncs[i].dir           = sc->sync_configs[i].dir;
        io.syncs[i].watchdog_mode = sc->sync_configs[i].watchdog_mode;
        io.syncs[i].pdo_count     =
            ec_pdo_list_count(&sc->sync_configs[i].pdos);
    }

    io.watchdog_divider   = sc->watchdog_divider;
    io.watchdog_intervals = sc->watchdog_intervals;
    io.sdo_count          = ec_slave_config_sdo_count(sc);
    io.idn_count          = ec_slave_config_idn_count(sc);
    io.flag_count         = ec_slave_config_flag_count(sc);
    io.slave_position     = sc->slave ? sc->slave->ring_position : -1;
    io.dc_assign_activate = sc->dc_assign_activate;
    for (i = 0; i < EC_SYNC_SIGNAL_COUNT; i++)
        io.dc_sync[i] = sc->dc_sync[i];

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_CONFIG_PDO — slave configuration PDO information. */
static int dispatch_config_pdo(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_config_pdo_t io;
    const ec_slave_config_t *sc;
    const ec_pdo_t *pdo;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (io.sync_index >= EC_MAX_SYNC_MANAGERS)
        return send_response(fd, -EINVAL, NULL, 0);

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    sc = ec_master_get_config_const(master, io.config_index);
    if (!sc) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    pdo = ec_pdo_list_find_pdo_by_pos_const(
            &sc->sync_configs[io.sync_index].pdos, io.pdo_pos);
    if (!pdo) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.index       = pdo->index;
    io.entry_count = ec_pdo_entry_count(pdo);
    ipc_strcpy(io.name, pdo->name);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_CONFIG_PDO_ENTRY — slave configuration PDO entry information. */
static int dispatch_config_pdo_entry(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_config_pdo_entry_t io;
    const ec_slave_config_t *sc;
    const ec_pdo_t *pdo;
    const ec_pdo_entry_t *entry;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (io.sync_index >= EC_MAX_SYNC_MANAGERS)
        return send_response(fd, -EINVAL, NULL, 0);

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    sc = ec_master_get_config_const(master, io.config_index);
    if (!sc) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    pdo = ec_pdo_list_find_pdo_by_pos_const(
            &sc->sync_configs[io.sync_index].pdos, io.pdo_pos);
    if (!pdo) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    entry = ec_pdo_find_entry_by_pos_const(pdo, io.entry_pos);
    if (!entry) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.index      = entry->index;
    io.subindex   = entry->subindex;
    io.bit_length = entry->bit_length;
    ipc_strcpy(io.name, entry->name);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_CONFIG_SDO — slave configuration SDO information. */
static int dispatch_config_sdo(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_config_sdo_t io;
    const ec_slave_config_t *sc;
    const ec_sdo_request_t *sdo_req;
    uint32_t copy_size;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    sc = ec_master_get_config_const(master, io.config_index);
    if (!sc) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    sdo_req = ec_slave_config_get_sdo_by_pos_const(sc, io.sdo_pos);
    if (!sdo_req) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.index    = sdo_req->index;
    io.subindex = sdo_req->subindex;
    io.size     = sdo_req->data_size;
    copy_size   = io.size < EC_MAX_SDO_DATA_SIZE
                  ? (uint32_t)io.size : EC_MAX_SDO_DATA_SIZE;
    memcpy(io.data, sdo_req->data, copy_size);
    io.complete_access = sdo_req->complete_access;

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_CONFIG_IDN — slave configuration IDN information. */
static int dispatch_config_idn(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_config_idn_t io;
    const ec_slave_config_t *sc;
    const ec_soe_request_t *idn_req;
    uint32_t copy_size;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    sc = ec_master_get_config_const(master, io.config_index);
    if (!sc) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    idn_req = ec_slave_config_get_idn_by_pos_const(sc, io.idn_pos);
    if (!idn_req) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.drive_no = idn_req->drive_no;
    io.idn      = idn_req->idn;
    io.state    = idn_req->al_state;
    io.size     = idn_req->data_size;
    copy_size   = io.size < EC_MAX_IDN_DATA_SIZE
                  ? (uint32_t)io.size : EC_MAX_IDN_DATA_SIZE;
    memcpy(io.data, idn_req->data, copy_size);

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_CONFIG_FLAG — slave configuration feature flag information. */
static int dispatch_config_flag(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_config_flag_t io;
    const ec_slave_config_t *sc;
    const ec_flag_t *flag;
    size_t key_len;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    sc = ec_master_get_config_const(master, io.config_index);
    if (!sc) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    flag = ec_slave_config_get_flag_by_pos_const(sc, io.flag_pos);
    if (!flag) {
        ec_sem_up(&master->master_sem);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    key_len = strlen(flag->key);
    if (key_len >= EC_MAX_FLAG_KEY_SIZE)
        key_len = EC_MAX_FLAG_KEY_SIZE - 1;
    memcpy(io.key, flag->key, key_len);
    io.key[key_len] = '\0';
    io.value = flag->value;

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

#ifdef EC_EOE

/** EC_CMD_EOE_HANDLER — EoE handler information. */
static int dispatch_eoe_handler(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_eoe_handler_t io;
    const ec_eoe_t *eoe;

    if (req_size != sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    eoe = ec_master_get_eoe_handler_const(master, io.eoe_index);
    if (!eoe) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "EoE handler %u does not exist!\n",
                io.eoe_index);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    io.slave_position     = eoe->slave
        ? eoe->slave->ring_position : 0xffff;
    snprintf(io.name, EC_DATAGRAM_NAME_SIZE, "%s", eoe->dev->name);
    io.open               = eoe->opened;
    /* EoE statistics are reported from the EtherCAT bus perspective:
     * tool "Rx" = data received from bus = net_device tx (interface sends to bus)
     * tool "Tx" = data sent to bus = net_device rx (interface receives from bus)
     */
    io.rx_bytes           = eoe->stats.tx_bytes;
    io.rx_rate            = eoe->tx_rate;
    io.tx_bytes           = eoe->stats.rx_bytes;
    io.tx_rate            = eoe->rx_rate;
    io.tx_queued_frames   = eoe->tx_queued_frames;
    io.tx_queue_size      = eoe->tx_queue_size;

    ec_sem_up(&master->master_sem);
    return send_response(fd, 0, &io, sizeof(io));
}

/** EC_CMD_SLAVE_EOE_IP_PARAM / EC_CMD_CONFIG_EOE_IP_PARAM — not supported. */
static int dispatch_eoe_ip_unsupported(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    (void)master;
    (void)req;
    (void)req_size;
    return send_response(fd, -ENOSYS, NULL, 0);
}

#endif /* EC_EOE */

/****************************************************************************/
/* Trailing-data dispatch functions                                            */
/****************************************************************************/

/** EC_CMD_DOMAIN_DATA — return domain process data. */
static int dispatch_domain_data(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_domain_data_t io;
    const ec_domain_t *domain;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (ec_sem_down_interruptible(&master->master_sem))
        return send_response(fd, -EINTR, NULL, 0);

    domain = ec_master_find_domain_const(master, io.domain_index);
    if (!domain) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Domain %u does not exist!\n", io.domain_index);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    if (domain->data_size != io.data_size) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Data size mismatch %u/%zu!\n",
                io.data_size, domain->data_size);
        return send_response(fd, -EFAULT, NULL, 0);
    }

    {
        int ret = send_response_with_trailing(fd, 0,
                &io, sizeof(io),
                domain->data, io.data_size);
        ec_sem_up(&master->master_sem);
        return ret;
    }
}

/** EC_CMD_SLAVE_SDO_UPLOAD — read an SDO entry from a slave. */
static int dispatch_slave_sdo_upload(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sdo_upload_t io;
    uint8_t *target;
    size_t result_size = 0;
    int ret;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (!io.target_size)
        return send_response(fd, -EINVAL, NULL, 0);

    target = malloc(io.target_size);
    if (!target) {
        EC_MASTER_ERR(master, "Failed to allocate %u bytes for SDO upload.\n",
                io.target_size);
        return send_response(fd, -ENOMEM, NULL, 0);
    }

    ret = ecrt_master_sdo_upload(master, io.slave_position,
            io.sdo_index, io.sdo_entry_subindex, target,
            io.target_size, &result_size, &io.abort_code);
    io.data_size = (uint32_t)result_size;

    if (!ret) {
        int send_ret = send_response_with_trailing(fd, 0,
                &io, sizeof(io),
                target, io.data_size);
        free(target);
        return send_ret;
    } else {
        free(target);
        return send_response(fd, ret, &io, sizeof(io));
    }
}

/** EC_CMD_SLAVE_SDO_DOWNLOAD — write an SDO entry to a slave. */
static int dispatch_slave_sdo_download(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sdo_download_t io;
    const uint8_t *sdo_data;
    int retval;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    /* Trailing SDO data follows the struct in the request payload. */
    if (req_size < sizeof(io) + io.data_size)
        return send_response(fd, -EINVAL, NULL, 0);
    sdo_data = req + sizeof(io);

    if (io.complete_access) {
        retval = ecrt_master_sdo_download_complete(master, io.slave_position,
                io.sdo_index, sdo_data, io.data_size, &io.abort_code);
    } else {
        retval = ecrt_master_sdo_download(master, io.slave_position,
                io.sdo_index, io.sdo_entry_subindex, sdo_data,
                io.data_size, &io.abort_code);
    }

    return send_response(fd, retval, &io, sizeof(io));
}

/** EC_CMD_SLAVE_SII_READ — read SII words from a slave's EEPROM. */
static int dispatch_slave_sii_read(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sii_t io;
    const ec_slave_t *slave;
    uint16_t *words;
    int ret;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (!io.nwords)
        return send_response(fd, -EINVAL, NULL, 0);

    words = malloc(io.nwords * 2);
    if (!words) {
        EC_MASTER_ERR(master, "Failed to allocate %u bytes for SII read.\n",
                io.nwords * 2);
        return send_response(fd, -ENOMEM, NULL, 0);
    }

    if (ec_sem_down_interruptible(&master->master_sem)) {
        free(words);
        return send_response(fd, -EINTR, NULL, 0);
    }

    if (!(slave = ec_master_find_slave_const(master, 0, io.slave_position))) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                io.slave_position);
        free(words);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    if (io.offset + io.nwords > slave->sii_nwords) {
        ec_sem_up(&master->master_sem);
        EC_SLAVE_ERR(slave, "Invalid SII read offset/size %u/%u for slave SII"
                " size %zu!\n", io.offset, io.nwords, slave->sii_nwords);
        free(words);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    memcpy(words, slave->sii_words + io.offset, io.nwords * 2);
    ec_sem_up(&master->master_sem);

    ret = send_response_with_trailing(fd, 0,
            &io, sizeof(io),
            words, io.nwords * 2);
    free(words);
    return ret;
}

/** EC_CMD_SLAVE_SII_WRITE — write SII words to a slave's EEPROM. */
static int dispatch_slave_sii_write(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_sii_t io;
    ec_slave_t *slave;
    uint16_t *words;
    ec_sii_write_request_t request;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (!io.nwords)
        return send_response(fd, 0, NULL, 0);

    /* Trailing word data follows the struct in the request payload. */
    if (req_size < sizeof(io) + (uint32_t)io.nwords * 2)
        return send_response(fd, -EINVAL, NULL, 0);

    /* Copy word data to heap so it remains valid during FSM processing. */
    words = malloc((size_t)io.nwords * 2);
    if (!words) {
        EC_MASTER_ERR(master, "Failed to allocate %u bytes for SII write.\n",
                io.nwords * 2);
        return send_response(fd, -ENOMEM, NULL, 0);
    }
    memcpy(words, req + sizeof(io), (size_t)io.nwords * 2);

    if (ec_sem_down_interruptible(&master->master_sem)) {
        free(words);
        return send_response(fd, -EINTR, NULL, 0);
    }

    if (!(slave = ec_master_find_slave(master, 0, io.slave_position))) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                io.slave_position);
        free(words);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    /* Init SII write request. */
    INIT_LIST_HEAD(&request.list);
    request.slave = slave;
    request.words = words;
    request.offset = io.offset;
    request.nwords = io.nwords;
    request.state = EC_INT_REQUEST_QUEUED;

    /* Schedule SII write request. */
    list_add_tail(&request.list, &master->sii_requests);
    ec_sem_up(&master->master_sem);

    /* Wait for processing through FSM. */
    if (ec_wq_wait_interruptible(master->request_queue,
                request.state != EC_INT_REQUEST_QUEUED)) {
        ec_sem_down(&master->master_sem);
        if (request.state == EC_INT_REQUEST_QUEUED) {
            list_del(&request.list);
            ec_sem_up(&master->master_sem);
            free(words);
            return send_response(fd, -EINTR, NULL, 0);
        }
        ec_sem_up(&master->master_sem);
    }

    /* Wait until master FSM has finished processing. */
    ec_wq_wait(master->request_queue,
            request.state != EC_INT_REQUEST_BUSY);

    free(words);

    return send_response(fd,
            request.state == EC_INT_REQUEST_SUCCESS ? 0 : -EIO,
            NULL, 0);
}

/** EC_CMD_SLAVE_REG_READ — read slave registers. */
static int dispatch_slave_reg_read(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_reg_t io;
    ec_slave_t *slave;
    ec_reg_request_t request;
    int ret;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (!io.size)
        return send_response(fd, 0, NULL, 0);

    ret = ec_reg_request_init(&request, io.size);
    if (ret)
        return send_response(fd, ret, NULL, 0);

    ret = ecrt_reg_request_read(&request, io.address, io.size);
    if (ret) {
        ec_reg_request_clear(&request);
        return send_response(fd, ret, NULL, 0);
    }

    if (ec_sem_down_interruptible(&master->master_sem)) {
        ec_reg_request_clear(&request);
        return send_response(fd, -EINTR, NULL, 0);
    }

    if (!(slave = ec_master_find_slave(master, 0, io.slave_position))) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                io.slave_position);
        ec_reg_request_clear(&request);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    /* Schedule request. */
    list_add_tail(&request.list, &slave->reg_requests);
    ec_sem_up(&master->master_sem);

    /* Wait for processing through FSM. */
    if (ec_wq_wait_interruptible(master->request_queue,
                request.state != EC_INT_REQUEST_QUEUED)) {
        ec_sem_down(&master->master_sem);
        if (request.state == EC_INT_REQUEST_QUEUED) {
            list_del(&request.list);
            ec_sem_up(&master->master_sem);
            ec_reg_request_clear(&request);
            return send_response(fd, -EINTR, NULL, 0);
        }
        ec_sem_up(&master->master_sem);
    }

    /* Wait until master FSM has finished processing. */
    ec_wq_wait(master->request_queue, request.state != EC_INT_REQUEST_BUSY);

    if (request.state == EC_INT_REQUEST_SUCCESS) {
        ret = send_response_with_trailing(fd, 0,
                &io, sizeof(io),
                request.data, io.size);
        ec_reg_request_clear(&request);
        return ret;
    }
    ec_reg_request_clear(&request);
    return send_response(fd, -EIO, NULL, 0);
}

/** EC_CMD_SLAVE_REG_WRITE — write slave registers. */
static int dispatch_slave_reg_write(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_reg_t io;
    ec_slave_t *slave;
    ec_reg_request_t request;
    int ret;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (!io.size)
        return send_response(fd, 0, NULL, 0);

    if (req_size < sizeof(io) + io.size)
        return send_response(fd, -EINVAL, NULL, 0);

    ret = ec_reg_request_init(&request, io.size);
    if (ret)
        return send_response(fd, ret, NULL, 0);

    memcpy(request.data, req + sizeof(io), io.size);

    ret = ecrt_reg_request_write(&request, io.address, io.size);
    if (ret) {
        ec_reg_request_clear(&request);
        return send_response(fd, ret, NULL, 0);
    }

    if (ec_sem_down_interruptible(&master->master_sem)) {
        ec_reg_request_clear(&request);
        return send_response(fd, -EINTR, NULL, 0);
    }

    if (io.emergency) {
        request.ring_position = io.slave_position;
        /* Schedule emergency register request. */
        list_add_tail(&request.list, &master->emerg_reg_requests);
    } else {
        if (!(slave = ec_master_find_slave(master, 0, io.slave_position))) {
            ec_sem_up(&master->master_sem);
            EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                    io.slave_position);
            ec_reg_request_clear(&request);
            return send_response(fd, -EINVAL, NULL, 0);
        }
        /* Schedule request. */
        list_add_tail(&request.list, &slave->reg_requests);
    }
    ec_sem_up(&master->master_sem);

    /* Wait for processing through FSM. */
    if (ec_wq_wait_interruptible(master->request_queue,
                request.state != EC_INT_REQUEST_QUEUED)) {
        ec_sem_down(&master->master_sem);
        if (request.state == EC_INT_REQUEST_QUEUED) {
            list_del(&request.list);
            ec_sem_up(&master->master_sem);
            ec_reg_request_clear(&request);
            return send_response(fd, -EINTR, NULL, 0);
        }
        ec_sem_up(&master->master_sem);
    }

    /* Wait until master FSM has finished processing. */
    ec_wq_wait(master->request_queue, request.state != EC_INT_REQUEST_BUSY);

    ret = request.state == EC_INT_REQUEST_SUCCESS ? 0 : -EIO;
    ec_reg_request_clear(&request);
    return send_response(fd, ret, NULL, 0);
}

/** EC_CMD_SLAVE_FOE_READ — read a file from a slave via FoE. */
static int dispatch_slave_foe_read(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_foe_t io;
    ec_foe_request_t request;
    ec_slave_t *slave;
    int ret;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    ec_foe_request_init(&request, io.file_name);
    ret = ec_foe_request_alloc(&request, 10000); // FIXME
    if (ret) {
        ec_foe_request_clear(&request);
        return send_response(fd, ret, NULL, 0);
    }

    ec_foe_request_read(&request);

    if (ec_sem_down_interruptible(&master->master_sem)) {
        ec_foe_request_clear(&request);
        return send_response(fd, -EINTR, NULL, 0);
    }

    if (!(slave = ec_master_find_slave(master, 0, io.slave_position))) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                io.slave_position);
        ec_foe_request_clear(&request);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    EC_SLAVE_DBG(slave, 1, "Scheduling FoE read request.\n");

    /* Schedule request. */
    list_add_tail(&request.list, &slave->foe_requests);
    ec_sem_up(&master->master_sem);

    /* Wait for processing through FSM. */
    if (ec_wq_wait_interruptible(master->request_queue,
                request.state != EC_INT_REQUEST_QUEUED)) {
        ec_sem_down(&master->master_sem);
        if (request.state == EC_INT_REQUEST_QUEUED) {
            list_del(&request.list);
            ec_sem_up(&master->master_sem);
            ec_foe_request_clear(&request);
            return send_response(fd, -EINTR, NULL, 0);
        }
        /* Request already processing: interrupt not possible. */
        ec_sem_up(&master->master_sem);
    }

    /* Wait until master FSM has finished processing. */
    ec_wq_wait(master->request_queue, request.state != EC_INT_REQUEST_BUSY);

    io.result = request.result;
    io.error_code = request.error_code;

    if (request.state != EC_INT_REQUEST_SUCCESS) {
        io.data_size = 0;
        ec_foe_request_clear(&request);
        return send_response(fd, -EIO, &io, sizeof(io));
    }

    io.data_size = (uint32_t)request.data_size;
    ret = send_response_with_trailing(fd, 0,
            &io, sizeof(io),
            request.buffer, io.data_size);
    ec_foe_request_clear(&request);
    return ret;
}

/** EC_CMD_SLAVE_FOE_WRITE — write a file to a slave via FoE. */
static int dispatch_slave_foe_write(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_foe_t io;
    ec_foe_request_t request;
    ec_slave_t *slave;
    int ret;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (req_size < sizeof(io) + io.buffer_size)
        return send_response(fd, -EINVAL, NULL, 0);

    ec_foe_request_init(&request, io.file_name);
    ret = ec_foe_request_alloc(&request, io.buffer_size);
    if (ret) {
        ec_foe_request_clear(&request);
        return send_response(fd, ret, NULL, 0);
    }

    memcpy(request.buffer, req + sizeof(io), io.buffer_size);
    request.data_size = io.buffer_size;
    ec_foe_request_write(&request);

    if (ec_sem_down_interruptible(&master->master_sem)) {
        ec_foe_request_clear(&request);
        return send_response(fd, -EINTR, NULL, 0);
    }

    if (!(slave = ec_master_find_slave(master, 0, io.slave_position))) {
        ec_sem_up(&master->master_sem);
        EC_MASTER_ERR(master, "Slave %u does not exist!\n",
                io.slave_position);
        ec_foe_request_clear(&request);
        return send_response(fd, -EINVAL, NULL, 0);
    }

    EC_SLAVE_DBG(slave, 1, "Scheduling FoE write request.\n");

    /* Schedule FoE write request. */
    list_add_tail(&request.list, &slave->foe_requests);
    ec_sem_up(&master->master_sem);

    /* Wait for processing through FSM. */
    if (ec_wq_wait_interruptible(master->request_queue,
                request.state != EC_INT_REQUEST_QUEUED)) {
        ec_sem_down(&master->master_sem);
        if (request.state == EC_INT_REQUEST_QUEUED) {
            list_del(&request.list);
            ec_sem_up(&master->master_sem);
            ec_foe_request_clear(&request);
            return send_response(fd, -EINTR, NULL, 0);
        }
        ec_sem_up(&master->master_sem);
    }

    /* Wait until master FSM has finished processing. */
    ec_wq_wait(master->request_queue, request.state != EC_INT_REQUEST_BUSY);

    io.result = request.result;
    io.error_code = request.error_code;

    ret = request.state == EC_INT_REQUEST_SUCCESS ? 0 : -EIO;
    ec_foe_request_clear(&request);
    return send_response(fd, ret, &io, sizeof(io));
}

/** EC_CMD_SLAVE_SOE_READ — read an SoE IDN from a slave. */
static int dispatch_slave_soe_read(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_soe_read_t io;
    uint8_t *data;
    size_t result_size = 0;
    int retval;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (!io.mem_size)
        return send_response(fd, -EINVAL, NULL, 0);

    data = malloc(io.mem_size);
    if (!data) {
        EC_MASTER_ERR(master, "Failed to allocate %u bytes of IDN data.\n",
                io.mem_size);
        return send_response(fd, -ENOMEM, NULL, 0);
    }

    retval = ecrt_master_read_idn(master, io.slave_position,
            io.drive_no, io.idn, data, io.mem_size, &result_size,
            &io.error_code);
    io.data_size = (uint32_t)result_size;
    if (retval) {
        free(data);
        return send_response(fd, retval, &io, sizeof(io));
    }

    retval = send_response_with_trailing(fd, 0,
            &io, sizeof(io),
            data, io.data_size);
    free(data);
    return retval;
}

/** EC_CMD_SLAVE_SOE_WRITE — write an SoE IDN to a slave. */
static int dispatch_slave_soe_write(int fd, ec_master_t *master,
        const uint8_t *req, uint32_t req_size)
{
    ec_ioctl_slave_soe_write_t io;
    int retval;

    if (req_size < sizeof(io))
        return send_response(fd, -EINVAL, NULL, 0);
    memcpy(&io, req, sizeof(io));

    if (req_size < sizeof(io) + io.data_size)
        return send_response(fd, -EINVAL, NULL, 0);

    retval = ecrt_master_write_idn(master, io.slave_position,
            io.drive_no, io.idn, req + sizeof(io), io.data_size,
            &io.error_code);
    return send_response(fd, retval, &io, sizeof(io));
}

/****************************************************************************/
/* Poll-based event loop                                                       */
/****************************************************************************/

/** Maximum payload size we are willing to receive (protect against OOM). */
#define EC_IPC_MAX_PAYLOAD 65536U

/** Maximum number of concurrent tool connections. */
#define EC_IPC_MAX_CLIENTS 16

/**
 * Handle exactly one request from a connected client fd.
 *
 * Reads the request header and payload, dispatches the command, and sends
 * the response.  Uses \a payload / \a payload_cap as a reusable buffer
 * owned by the caller.
 *
 * \return 0 to keep the connection open, non-zero to close it.
 */
static int handle_client_request(int fd, uint8_t **payload,
        uint32_t *payload_cap)
{
    ec_ipc_request_t req;
    ec_master_t *master;
    int ret;

    ret = recv_all(fd, &req, sizeof(req));
    if (ret < 0)
        return 1; /* client disconnected or error */

    /* Validate version magic. */
    if (req.version_magic != EC_IOCTL_VERSION_MAGIC) {
        ec_log(EC_LOG_WARNING,
                "IPC: version magic mismatch (%u vs %u); dropping client\n",
                req.version_magic, EC_IOCTL_VERSION_MAGIC);
        send_response(fd, -EINVAL, NULL, 0);
        return 1;
    }

    /* Reject oversized payloads. */
    if (req.data_size > EC_IPC_MAX_PAYLOAD) {
        ec_log(EC_LOG_WARNING,
                "IPC: payload too large (%u); dropping client\n",
                req.data_size);
        send_response(fd, -EINVAL, NULL, 0);
        return 1;
    }

    /* Grow the shared payload buffer if needed. */
    if (req.data_size > *payload_cap) {
        free(*payload);
        *payload_cap = 0;
        *payload = malloc(req.data_size);
        if (!*payload) {
            send_response(fd, -ENOMEM, NULL, 0);
            return 1;
        }
        *payload_cap = req.data_size;
    }

    if (req.data_size > 0) {
        ret = recv_all(fd, *payload, req.data_size);
        if (ret < 0)
            return 1;
    }

    /* EC_CMD_MODULE does not require a valid master. */
    if ((enum ec_tool_cmd)req.cmd == EC_CMD_MODULE) {
        dispatch_module(fd, NULL, *payload, req.data_size);
        return 0;
    }

    /* All other commands require a valid master. */
    master = ec_master_registry_get(req.master_index);
    if (!master) {
        send_response(fd, -EINVAL, NULL, 0);
        return 0;
    }

    /* Dispatch command. */
    switch ((enum ec_tool_cmd)req.cmd) {
        case EC_CMD_MODULE:
            /* handled above */
            break;
        case EC_CMD_MASTER:
            dispatch_master(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE:
            dispatch_slave(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SYNC:
            dispatch_slave_sync(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SYNC_PDO:
            dispatch_slave_sync_pdo(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SYNC_PDO_ENTRY:
            dispatch_slave_sync_pdo_entry(fd, master, *payload,
                    req.data_size);
            break;
        case EC_CMD_DOMAIN:
            dispatch_domain(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_DOMAIN_FMMU:
            dispatch_domain_fmmu(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_DOMAIN_DATA:
            dispatch_domain_data(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_MASTER_DEBUG:
            dispatch_master_debug(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_MASTER_RESCAN:
            dispatch_master_rescan(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_STATE:
            dispatch_slave_state(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SDO:
            dispatch_slave_sdo(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SDO_ENTRY:
            dispatch_slave_sdo_entry(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SDO_UPLOAD:
            dispatch_slave_sdo_upload(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SDO_DOWNLOAD:
            dispatch_slave_sdo_download(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SII_READ:
            dispatch_slave_sii_read(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SII_WRITE:
            dispatch_slave_sii_write(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_REG_READ:
            dispatch_slave_reg_read(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_REG_WRITE:
            dispatch_slave_reg_write(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_FOE_READ:
            dispatch_slave_foe_read(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_FOE_WRITE:
            dispatch_slave_foe_write(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SOE_READ:
            dispatch_slave_soe_read(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_SLAVE_SOE_WRITE:
            dispatch_slave_soe_write(fd, master, *payload, req.data_size);
            break;
#ifdef EC_EOE
        case EC_CMD_SLAVE_EOE_IP_PARAM:
            dispatch_eoe_ip_unsupported(fd, master, *payload,
                    req.data_size);
            break;
#endif
        case EC_CMD_CONFIG:
            dispatch_config(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_CONFIG_PDO:
            dispatch_config_pdo(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_CONFIG_PDO_ENTRY:
            dispatch_config_pdo_entry(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_CONFIG_SDO:
            dispatch_config_sdo(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_CONFIG_IDN:
            dispatch_config_idn(fd, master, *payload, req.data_size);
            break;
        case EC_CMD_CONFIG_FLAG:
            dispatch_config_flag(fd, master, *payload, req.data_size);
            break;
#ifdef EC_EOE
        case EC_CMD_CONFIG_EOE_IP_PARAM:
            dispatch_eoe_ip_unsupported(fd, master, *payload,
                    req.data_size);
            break;
        case EC_CMD_EOE_HANDLER:
            dispatch_eoe_handler(fd, master, *payload, req.data_size);
            break;
#endif
        default:
            send_response(fd, -ENOTTY, NULL, 0);
            break;
    }

    ec_master_registry_put(master);
    return 0;
}

/****************************************************************************/
/* Listener thread                                                             */
/****************************************************************************/

/**
 * Listener thread: poll()-based event loop that multiplexes the listening
 * socket and all connected client sockets in a single thread.
 *
 * On new connection: accept(), set SO_SNDTIMEO and SO_RCVTIMEO, add to poll array.
 * On client data:    read one full request, dispatch, send response.
 * On client error:   close fd and remove from poll array.
 * On shutdown:       exit the loop; close remaining client fds and listening socket.
 */
static int listener_fn(void *arg)
{
    ec_cdev_t *cdev = (ec_cdev_t *)arg;
    /* fds[0] = listening socket; fds[1..nfds-1] = connected clients */
    struct pollfd fds[1 + EC_IPC_MAX_CLIENTS];
    int nfds = 1;
    uint8_t *payload = NULL;
    uint32_t payload_cap = 0;
    int i;

    fds[0].fd     = cdev->sock_fd;
    fds[0].events = POLLIN;

    while (!atomic_load(&cdev->shutdown)) {
        int ret = poll(fds, (nfds_t)nfds, 1000);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            if (!atomic_load(&cdev->shutdown))
                ec_log(EC_LOG_WARNING,
                        "IPC: poll() failed: %s\n", strerror(errno));
            break;
        }
        if (ret == 0)
            continue; /* timeout — recheck shutdown flag */

        /* New connection on the listening socket. */
        if (fds[0].revents & POLLIN) {
            struct sockaddr_un client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(cdev->sock_fd,
                    (struct sockaddr *)&client_addr, &client_len);
            if (client_fd >= 0) {
                if (nfds < 1 + EC_IPC_MAX_CLIENTS) {
                    struct timeval tv;
                    tv.tv_sec  = 5;
                    tv.tv_usec = 0;
                    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO,
                            &tv, sizeof(tv)) < 0)
                        ec_log(EC_LOG_WARNING,
                                "IPC: SO_SNDTIMEO failed: %s\n",
                                strerror(errno));
                    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                            &tv, sizeof(tv)) < 0)
                        ec_log(EC_LOG_WARNING,
                                "IPC: SO_RCVTIMEO failed: %s\n",
                                strerror(errno));
                    fds[nfds].fd      = client_fd;
                    fds[nfds].events  = POLLIN;
                    fds[nfds].revents = 0;
                    nfds++;
                } else {
                    ec_log(EC_LOG_WARNING,
                            "IPC: too many connections, rejecting\n");
                    close(client_fd);
                }
            } else if (!atomic_load(&cdev->shutdown)) {
                if (errno != EINTR && errno != EAGAIN &&
                        errno != EWOULDBLOCK)
                    ec_log(EC_LOG_WARNING,
                            "IPC: accept() failed: %s\n", strerror(errno));
            }
        }

        /* Service existing client connections. */
        for (i = 1; i < nfds; ) {
            if (!fds[i].revents) {
                i++;
                continue;
            }

            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close(fds[i].fd);
                fds[i] = fds[--nfds];
                fds[i].revents = 0; /* don't re-process moved entry's stale events */
                continue; /* recheck same index (now holds last entry) */
            }

            if (fds[i].revents & POLLIN) {
                int drop = handle_client_request(fds[i].fd,
                        &payload, &payload_cap);
                if (drop) {
                    close(fds[i].fd);
                    fds[i] = fds[--nfds];
                    fds[i].revents = 0; /* don't re-process moved entry's stale events */
                    continue; /* recheck same index */
                }
            }

            i++;
        }
    }

    /* Close all remaining client connections. */
    for (i = 1; i < nfds; i++)
        close(fds[i].fd);

    /* Close the listening socket here so ec_ipc_server_stop() does not race
     * with accept() on the same fd.  ec_ipc_server_stop() only calls
     * shutdown() to wake poll() and then joins this thread. */
    close(cdev->sock_fd);
    cdev->sock_fd = -1;

    free(payload);
    return 0;
}

/****************************************************************************/
/* Public API                                                                  */
/****************************************************************************/

/**
 * ec_ipc_server_start - start the global IPC server.
 *
 * Creates a Unix-domain listening socket at \a socket_path and starts the
 * listener thread.  Called by ecrt_lib_init() when socket_path is not NULL.
 *
 * \return 0 on success, negative error code on failure.
 */
int ec_ipc_server_start(const char *socket_path)
{
    ec_cdev_t *cdev = &g_cdev;
    struct sockaddr_un addr;
    int sock_fd;
    int ret;

    if (cdev->sock_fd != -1)
        return 0; /* already running */

    snprintf(cdev->sock_path, sizeof(cdev->sock_path), "%s", socket_path);
    cdev->thread   = NULL;
    atomic_store(&cdev->shutdown, 0);

    /* Create the listening socket. */
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        ec_log(EC_LOG_ERR, "IPC: socket() failed: %s\n", strerror(errno));
        return -errno;
    }

    /* Remove any stale socket file. */
    unlink(cdev->sock_path);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, cdev->sock_path, sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ret = -errno;
        ec_log(EC_LOG_ERR, "IPC: bind() to %s failed: %s\n",
                cdev->sock_path, strerror(errno));
        close(sock_fd);
        return ret;
    }

    if (listen(sock_fd, 8) < 0) {
        ret = -errno;
        ec_log(EC_LOG_ERR, "IPC: listen() failed: %s\n", strerror(errno));
        close(sock_fd);
        unlink(cdev->sock_path);
        return ret;
    }

    cdev->sock_fd = sock_fd;

    /* Start listener thread. */
    cdev->thread = ec_thread_run(listener_fn, cdev, "ec_ipc_listen");
    if (IS_ERR(cdev->thread)) {
        ret = (int)PTR_ERR(cdev->thread);
        cdev->thread = NULL;
        ec_log(EC_LOG_ERR, "IPC: failed to start listener thread: %d\n", ret);
        close(cdev->sock_fd);
        cdev->sock_fd = -1;
        unlink(cdev->sock_path);
        return ret;
    }

    ec_log(EC_LOG_INFO, "IPC server listening on %s\n", cdev->sock_path);
    return 0;
}

/**
 * ec_ipc_server_stop - shut down the global IPC server.
 *
 * Signals the listener thread to stop by setting the shutdown flag and calling
 * shutdown() on the listening socket (which wakes poll()), then waits for the
 * single listener thread to exit.  The listener thread closes all client fds
 * and the listening socket itself before it exits, avoiding any race between
 * close() and accept() on the same fd.  The socket file is removed from the
 * filesystem after the thread has joined.
 */
void ec_ipc_server_stop(void)
{
    ec_cdev_t *cdev = &g_cdev;

    if (!cdev->thread)
        return; /* was never started or already stopped */

    /* Signal shutdown to the listener thread. */
    atomic_store(&cdev->shutdown, 1);

    /* Wake poll() on the listening socket so the listener thread sees the
     * shutdown flag quickly.  Do NOT close() here — the listener thread
     * closes sock_fd itself to avoid a race with accept().
     * Guard against sock_fd already being -1 if the listener exited early. */
    if (cdev->sock_fd != -1)
        shutdown(cdev->sock_fd, SHUT_RDWR);

    /* Wait for the listener thread to exit (it closes client fds itself). */
    ec_thread_stop(cdev->thread);
    cdev->thread = NULL;

    /* Remove the socket file. */
    unlink(cdev->sock_path);

    ec_log(EC_LOG_INFO, "IPC server on %s stopped\n", cdev->sock_path);
}

/****************************************************************************/
