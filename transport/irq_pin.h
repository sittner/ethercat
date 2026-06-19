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
 * By pinning the NIC IRQ to the same CPU that calls ecrt_master_receive(),
 * the NAPI poll fills the RX ring in the local L1/L2 cache, and the RT thread
 * consumes it immediately -- achieving phase-locked, cache-local I/O with
 * minimal and deterministic wakeup latency.
 *
 * This is only beneficial for dedicated, isolated NICs used exclusively for
 * EtherCAT fieldbus traffic (not for general-purpose networking).
 */

#ifndef __EC_TRANSPORT_IRQ_PIN_H__
#define __EC_TRANSPORT_IRQ_PIN_H__

/**
 * Discover the primary IRQ number for a network interface.
 *
 * Prefers the lowest MSI-X IRQ (queue 0) if available, otherwise
 * falls back to the legacy/MSI IRQ from sysfs.
 *
 * \param iface  Network interface name (e.g. "eth0").
 * \return IRQ number (>0) on success, -1 on failure.
 */
int ec_irq_discover(const char *iface);

/**
 * Set the CPU affinity for an IRQ.
 *
 * Writes the CPU mask to /proc/irq/$irq/smp_affinity.
 *
 * \param irq  IRQ number (must be > 0).
 * \param cpu  Target CPU number (0-based, max 63).
 * \return 0 on success, negative errno on failure.
 */
int ec_irq_set_affinity(int irq, int cpu);

#endif /* __EC_TRANSPORT_IRQ_PIN_H__ */
