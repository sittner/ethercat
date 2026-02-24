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
   Userspace EtherCAT master library lifecycle implementation.
*/

/****************************************************************************/

#include "pal.h"

#include "../device.h"
#include "../master.h"
#include "transport/ec_transport.h"

#include <stdlib.h>
#include <string.h>

/****************************************************************************/

/** Per-master context for userspace master library. */
typedef struct {
    ec_master_t master;           /**< The actual master struct (first member) */
    ec_transport_t *transport;    /**< Transport instance */
    int transport_owned;          /**< 1 if library owns transport, 0 if caller owns */
    uint8_t main_mac[ETH_ALEN];   /**< Copied MAC address (owned by this context) */
    uint8_t backup_mac[ETH_ALEN]; /**< Copied backup MAC address */
    char *interface_name;         /**< Copied interface name string (strdup, owned) */
} ec_master_uspace_ctx_t;

/****************************************************************************/

int ecrt_lib_init(void)
{
    ec_master_init_static();

    if (ec_pal_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create system workqueue\n");
        return -1;
    }

    if (ec_pal_irq_work_init() != 0) {
        ec_log(EC_LOG_ERR, "Failed to create IRQ work queue\n");
        ec_pal_work_cleanup();
        return -1;
    }

    return 0;
}

/****************************************************************************/

/** Common startup helper: initializes master from an already-assigned transport.
 *
 * Assumes ctx->transport, ctx->transport_owned, and ctx->interface_name
 * are already set.
 *
 * \return &ctx->master on success, NULL on error (ctx is freed on error).
 */
static ec_master_t *ecrt_startup_master_common(ec_master_uspace_ctx_t *ctx)
{
    int ret;

    /* Get MAC address from transport */
    ret = ec_transport_get_mac(ctx->transport, ctx->main_mac);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to get MAC address: %d\n", ret);
        goto out_free_interface;
    }

    /* Initialize master */
    ret = ec_master_init(&ctx->master, 0, ctx->main_mac, ctx->backup_mac, 1, 0);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to initialize master: %d\n", ret);
        goto out_free_interface;
    }

    /* Store transport reference in device */
    ctx->master.devices[EC_DEVICE_MAIN].pal.transport = ctx->transport;
    ctx->master.devices[EC_DEVICE_MAIN].name = ctx->interface_name;

    /* Open device */
    ret = ec_device_open(&ctx->master.devices[EC_DEVICE_MAIN]);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open device: %d\n", ret);
        goto out_clear_master;
    }

    /* Enter idle phase */
    ret = ec_master_enter_idle_phase(&ctx->master);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to enter idle phase: %d\n", ret);
        goto out_close_device;
    }

    return &ctx->master;

out_close_device:
    ec_device_close(&ctx->master.devices[EC_DEVICE_MAIN]);
out_clear_master:
    ec_master_clear(&ctx->master);
out_free_interface:
    if (ctx->interface_name) {
        free(ctx->interface_name);
        ctx->interface_name = NULL;
    }
    free(ctx);
    return NULL;
}

/****************************************************************************/

ec_master_t *ecrt_startup_master(ec_transport_type_t transport_type,
        const char *interface)
{
    ec_master_uspace_ctx_t *ctx;
    int ret;

    ctx = malloc(sizeof(ec_master_uspace_ctx_t));
    if (!ctx) {
        ec_log(EC_LOG_ERR, "Failed to allocate master context\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(ec_master_uspace_ctx_t));
    ctx->transport_owned = 1;

    ctx->interface_name = strdup(interface);
    if (!ctx->interface_name) {
        ec_log(EC_LOG_ERR, "Failed to copy interface name\n");
        free(ctx);
        return NULL;
    }

    ctx->transport = ec_transport_create(transport_type);
    if (!ctx->transport) {
        ec_log(EC_LOG_ERR, "Failed to create transport\n");
        free(ctx->interface_name);
        free(ctx);
        return NULL;
    }

    ret = ec_transport_open(ctx->transport, interface);
    if (ret < 0) {
        ec_log(EC_LOG_ERR, "Failed to open transport on %s: %d\n", interface, ret);
        ec_transport_destroy(ctx->transport);
        free(ctx->interface_name);
        free(ctx);
        return NULL;
    }

    return ecrt_startup_master_common(ctx);
}

/****************************************************************************/

ec_master_t *ecrt_startup_master_custom(ec_transport_t *transport,
        const char *interface)
{
    ec_master_uspace_ctx_t *ctx;

    ctx = malloc(sizeof(ec_master_uspace_ctx_t));
    if (!ctx) {
        ec_log(EC_LOG_ERR, "Failed to allocate master context\n");
        return NULL;
    }
    memset(ctx, 0, sizeof(ec_master_uspace_ctx_t));
    ctx->transport_owned = 0;
    ctx->transport = transport;

    ctx->interface_name = strdup(interface);
    if (!ctx->interface_name) {
        ec_log(EC_LOG_ERR, "Failed to copy interface name\n");
        free(ctx);
        return NULL;
    }

    return ecrt_startup_master_common(ctx);
}

/****************************************************************************/

void ecrt_release_master(ec_master_t *master)
{
    /* Safe: master is the FIRST member of ec_master_uspace_ctx_t, so the
     * pointer to master equals the pointer to the containing context.
     * This invariant MUST be maintained if the struct layout changes. */
    ec_master_uspace_ctx_t *ctx = (ec_master_uspace_ctx_t *)master;

    if (master->phase != EC_ORPHANED) {
        if (master->active) {
            ec_master_leave_operation_phase(master);
        }
        ec_master_leave_idle_phase(master);
    }

    ec_device_close(&master->devices[EC_DEVICE_MAIN]);
    ec_master_clear(master);

    if (ctx->transport) {
        ec_transport_close(ctx->transport);
        if (ctx->transport_owned) {
            ec_transport_destroy(ctx->transport);
        }
    }

    if (ctx->interface_name) {
        free(ctx->interface_name);
    }

    free(ctx);
}

/****************************************************************************/

void ecrt_lib_cleanup(void)
{
    ec_pal_irq_work_cleanup();
    ec_pal_work_cleanup();
}

/****************************************************************************/
