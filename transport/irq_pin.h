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
 * NIC IRQ affinity pinning helpers for transport implementations.
 *
 * In EtherCAT's request-response cycle, the RT thread sends a frame and then
 * waits for the slaves' response (which arrives as a NIC IRQ -> NAPI softirq
 * -> socket/AF_XDP RX ring).  If the IRQ is handled on a different CPU than
 * the RT thread, the received frame data must be transferred across cores via
 * the cache-coherence protocol, adding 40-80 ns of latency and jitter per
 * cycle.
 *
 * By pinning the NIC IRQs to the same CPU that calls ecrt_master_receive(),
 * the NAPI poll fills the RX ring in the local L1/L2 cache, and the RT thread
 * consumes it immediately -- achieving phase-locked, cache-local I/O with
 * minimal and deterministic wakeup latency.
 *
 * ALL of the interface's MSI-X vectors are pinned, not just one: the mapping
 * from vector to queue is driver-specific (on igb the lowest-numbered vector
 * is the link/misc interrupt and carries no traffic; e1000e splits rx/tx;
 * others differ), so picking a single "queue 0" vector reliably would need
 * per-driver knowledge.  On a dedicated EtherCAT NIC the non-traffic vectors
 * are near-silent, so pinning all of them is harmless and driver-agnostic.
 *
 * This is only beneficial for dedicated, isolated NICs used exclusively for
 * EtherCAT fieldbus traffic (not for general-purpose networking).
 */

#ifndef __EC_TRANSPORT_IRQ_PIN_H__
#define __EC_TRANSPORT_IRQ_PIN_H__

/** Maximum number of MSI-X vectors tracked per interface. */
#define EC_IRQ_MAX_VECTORS 64

/**
 * Set of IRQ numbers belonging to one network interface.
 */
typedef struct {
    int count;                     /**< Number of valid entries in irq[]. */
    int irq[EC_IRQ_MAX_VECTORS];   /**< IRQ numbers, ascending. */
} ec_irq_set_t;

/**
 * Discover all IRQ numbers for a network interface.
 *
 * Collects every MSI-X/MSI vector from
 * /sys/class/net/$iface/device/msi_irqs/, falling back to the legacy
 * IRQ from /sys/class/net/$iface/device/irq.  Interfaces with more than
 * EC_IRQ_MAX_VECTORS vectors are truncated (irrelevant for the dedicated
 * NICs this is meant for).
 *
 * \param iface  Network interface name (e.g. "eth0").
 * \param set    Filled with the discovered IRQ numbers.
 * \return Number of IRQs discovered (> 0) on success, -1 on failure.
 */
int ec_irq_discover(const char *iface, ec_irq_set_t *set);

/**
 * Set the CPU affinity for all IRQs in a set.
 *
 * Writes the CPU mask to /proc/irq/$irq/smp_affinity for each entry.
 * Failures on individual vectors are skipped (some vectors may not be
 * retargetable); success means at least one vector was pinned.
 *
 * \param set  IRQ set from ec_irq_discover().
 * \param cpu  Target CPU number (0-based, max 63).
 * \return Number of IRQs successfully pinned (> 0) on success,
 *         negative errno if the arguments are invalid or no IRQ could
 *         be pinned.
 */
int ec_irq_set_affinity(const ec_irq_set_t *set, int cpu);

#endif /* __EC_TRANSPORT_IRQ_PIN_H__ */
