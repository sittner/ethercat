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
 * Find the lowest-numbered MSI-X IRQ for a network interface.
 *
 * Looks in /sys/class/net/$iface/device/msi_irqs/ for IRQ entries.
 * Returns the lowest numbered IRQ (which corresponds to queue 0 on
 * most drivers).
 */
static int find_msix_irq(const char *iface)
{
    char path[PATH_MAX];
    DIR *dir;
    struct dirent *ent;
    int lowest_irq = -1;

    snprintf(path, sizeof(path), "/sys/class/net/%s/device/msi_irqs", iface);
    dir = opendir(path);
    if (!dir) {
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        int irq;
        if (ent->d_name[0] == '.') {
            continue;
        }
        irq = atoi(ent->d_name);
        if (irq > 0 && (lowest_irq < 0 || irq < lowest_irq)) {
            lowest_irq = irq;
        }
    }
    closedir(dir);

    return lowest_irq;
}

/****************************************************************************/

int ec_irq_discover(const char *iface)
{
    char path[PATH_MAX];
    int irq;

    /* Try MSI-X first (multi-queue NICs) -- get queue-0 IRQ */
    irq = find_msix_irq(iface);
    if (irq > 0) {
        return irq;
    }

    /* Fall back to legacy/MSI single IRQ */
    snprintf(path, sizeof(path), "/sys/class/net/%s/device/irq", iface);
    irq = read_sysfs_int(path);
    if (irq > 0) {
        return irq;
    }

    return -1;
}

/****************************************************************************/

int ec_irq_set_affinity(int irq, int cpu)
{
    char path[PATH_MAX];
    char mask[32];
    int fd, n, ret;

    if (irq <= 0 || cpu < 0 || cpu >= 64) {
        return -EINVAL;
    }

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
