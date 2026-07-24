/*****************************************************************************
 *
 *  Copyright (C) 2026  Sascha Ittner <sascha.ittner@modusoft.de>
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

/**
 * \file
 * Tests for the /proc/interrupts scanner behind the platform-device IRQ
 * discovery fallback (SoC NICs like the Raspberry Pi's bcmgenet have no
 * PCI msi_irqs/irq sysfs attributes), plus ec_irq_set_affinity argument
 * validation.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "irq_pin.h"
#include "test.h"

#define FIXTURE "test_irq_pin_fixture.txt"

/* A Raspberry-Pi-4-shaped /proc/interrupts: GIC rows named by OF node,
 * a second NIC named by interface, a shared row with comma-separated
 * actions, and the header/summary rows the parser must skip. Row 32 is
 * listed before row 31 so ascending insertion is exercised, and the
 * "eth1-rx-0" row guards full-token matching (an MSI-style name must
 * not be hit by "eth1"). */
static const char *fixture_content =
    "           CPU0       CPU1       CPU2       CPU3\n"
    "  9:       1234          0          0          0     GICv2  25 Level     vgic\n"
    " 32:        543          0          0          0     GICv2 190 Level     fd580000.ethernet\n"
    " 31:      99999          0          0          0     GICv2 189 Level     fd580000.ethernet\n"
    " 38:         77          0          0          0     GICv2  66 Level     eth1\n"
    " 40:         12          0          0          0     GICv2  70 Level     eth1-rx-0\n"
    " 45:          5          0          0          0     GICv2  90 Level     mmc0, mmc1\n"
    "IPI0:        12         13         14         15     Rescheduling interrupts\n"
    "Err:          0\n";

int main(void)
{
    ec_irq_set_t set;
    FILE *f;

    f = fopen(FIXTURE, "w");
    if (!f) {
        fprintf(stderr, "cannot write fixture\n");
        return 1;
    }
    fputs(fixture_content, f);
    fclose(f);

    /* OF-node name: both GIC rows, ascending despite file order. */
    set.count = 0;
    TEST_CHECK_EQ(2, ec_irq_scan_proc_interrupts(FIXTURE,
        "fd580000.ethernet", "eth0", &set));
    TEST_CHECK_EQ(2, set.count);
    TEST_CHECK_EQ(31, set.irq[0]);
    TEST_CHECK_EQ(32, set.irq[1]);

    /* Interface name alone; the MSI-style "eth1-rx-0" row must NOT
     * match "eth1" (full-token comparison). */
    set.count = 0;
    TEST_CHECK_EQ(1, ec_irq_scan_proc_interrupts(FIXTURE, NULL, "eth1", &set));
    TEST_CHECK_EQ(38, set.irq[0]);

    /* Shared interrupt: the comma-separated second action matches. */
    set.count = 0;
    TEST_CHECK_EQ(1, ec_irq_scan_proc_interrupts(FIXTURE, "mmc1", NULL, &set));
    TEST_CHECK_EQ(45, set.irq[0]);

    /* Both names naming the same rows adds each IRQ once. */
    set.count = 0;
    TEST_CHECK_EQ(2, ec_irq_scan_proc_interrupts(FIXTURE,
        "fd580000.ethernet", "fd580000.ethernet", &set));

    /* No match: -1, set untouched. */
    set.count = 0;
    TEST_CHECK_EQ(-1, ec_irq_scan_proc_interrupts(FIXTURE,
        "enp3s0", "wlan0", &set));
    TEST_CHECK_EQ(0, set.count);

    /* Unreadable file / both names NULL: -1. */
    TEST_CHECK_EQ(-1, ec_irq_scan_proc_interrupts(
        "test_irq_pin_no_such_file", "eth0", NULL, &set));
    TEST_CHECK_EQ(-1, ec_irq_scan_proc_interrupts(FIXTURE, NULL, NULL, &set));

    /* Appends to a pre-populated set (the discover flow starts empty,
     * but the contract is append). */
    set.count = 1;
    set.irq[0] = 7;
    TEST_CHECK_EQ(1, ec_irq_scan_proc_interrupts(FIXTURE, NULL, "eth1", &set));
    TEST_CHECK_EQ(2, set.count);
    TEST_CHECK_EQ(7, set.irq[0]);
    TEST_CHECK_EQ(38, set.irq[1]);

    /* ec_irq_set_affinity argument validation (the pinning itself needs
     * a real /proc/irq and root). */
    set.count = 0;
    TEST_CHECK_EQ(-EINVAL, ec_irq_set_affinity(&set, 3));
    set.count = 1;
    set.irq[0] = 31;
    TEST_CHECK_EQ(-EINVAL, ec_irq_set_affinity(NULL, 3));
    TEST_CHECK_EQ(-EINVAL, ec_irq_set_affinity(&set, -1));
    TEST_CHECK_EQ(-EINVAL, ec_irq_set_affinity(&set, 64));

    unlink(FIXTURE);
    return test_done("test_irq_pin");
}
