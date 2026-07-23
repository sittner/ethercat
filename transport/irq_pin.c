/******************************************************************************
 *
 *  Copyright (C) 2026  Sascha Ittner <sascha.ittner@modusoft.de>
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
 *****************************************************************************/

/**
 * \file
 * NIC IRQ affinity pinning implementation.
 */

#include "irq_pin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/****************************************************************************/

/**
 * Read a single integer from a sysfs file.
 */
static int read_sysfs_int(const char *path)
{
    char buf[32];
    int fd, n, val;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';

    val = atoi(buf);
    return val;
}

/****************************************************************************/

/**
 * Collect all MSI-X/MSI IRQs of a network interface, ascending.
 *
 * Looks in /sys/class/net/$iface/device/msi_irqs/ for IRQ entries.
 * All vectors are collected: which vector serves which queue (or none,
 * like igb's link/misc vector) is driver-specific, and on a dedicated
 * EtherCAT NIC the non-traffic vectors are near-silent anyway.
 */
static int find_msix_irqs(const char *iface, ec_irq_set_t *set)
{
    char path[PATH_MAX];
    DIR *dir;
    struct dirent *ent;

    snprintf(path, sizeof(path), "/sys/class/net/%s/device/msi_irqs", iface);
    dir = opendir(path);
    if (!dir) {
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        int irq, i, j;
        if (ent->d_name[0] == '.') {
            continue;
        }
        irq = atoi(ent->d_name);
        if (irq <= 0) {
            continue;
        }
        if (set->count >= EC_IRQ_MAX_VECTORS) {
            break;
        }
        /* Insert in ascending order (directory order is arbitrary) */
        for (i = 0; i < set->count && set->irq[i] < irq; i++);
        for (j = set->count; j > i; j--) {
            set->irq[j] = set->irq[j - 1];
        }
        set->irq[i] = irq;
        set->count++;
    }
    closedir(dir);

    return set->count > 0 ? set->count : -1;
}

/****************************************************************************/

int ec_irq_discover(const char *iface, ec_irq_set_t *set)
{
    char path[PATH_MAX];
    int irq;

    if (!iface || !set) {
        return -1;
    }

    set->count = 0;

    /* Try MSI-X first (multi-queue NICs) -- collect all vectors */
    if (find_msix_irqs(iface, set) > 0) {
        return set->count;
    }

    /* Fall back to legacy/MSI single IRQ */
    snprintf(path, sizeof(path), "/sys/class/net/%s/device/irq", iface);
    irq = read_sysfs_int(path);
    if (irq > 0) {
        set->irq[0] = irq;
        set->count = 1;
        return set->count;
    }

    return -1;
}

/****************************************************************************/

/**
 * Pin a single IRQ to a CPU via /proc/irq/$irq/smp_affinity.
 */
static int set_one_affinity(int irq, int cpu)
{
    char path[PATH_MAX];
    char mask[32];
    int fd, n, ret;

    snprintf(path, sizeof(path), "/proc/irq/%d/smp_affinity", irq);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        return -errno;
    }

    /* Write hex CPU mask: bit N set for the target CPU */
    if (cpu < 32) {
        snprintf(mask, sizeof(mask), "%x", 1U << cpu);
    } else {
        /* CPUs 32-63: need two 32-bit words separated by comma */
        snprintf(mask, sizeof(mask), "%x,00000000", 1U << (cpu - 32));
    }

    n = strlen(mask);
    ret = write(fd, mask, n);
    close(fd);

    if (ret != n) {
        return ret < 0 ? -errno : -EIO;
    }

    return 0;
}

/****************************************************************************/

int ec_irq_set_affinity(const ec_irq_set_t *set, int cpu)
{
    int i, pinned = 0, err = -ENODEV;

    if (!set || set->count <= 0 || cpu < 0 || cpu >= 64) {
        return -EINVAL;
    }

    for (i = 0; i < set->count; i++) {
        int ret = set_one_affinity(set->irq[i], cpu);
        if (ret == 0) {
            pinned++;
        } else {
            err = ret;
        }
    }

    return pinned > 0 ? pinned : err;
}

/****************************************************************************/
