/*****************************************************************************
 *
 *  transport_sim — in-process simulated EtherCAT bus for unit tests.
 *
 *  Implements the public ec_transport_ops_t interface with a virtual bus
 *  that emulates slaves at datagram level: position/configured/broadcast
 *  addressing, working counters, a register space per slave, the AL state
 *  machine and the SII EEPROM interface.  This makes the master's scan,
 *  configuration and cyclic logic testable without hardware; the core is
 *  shared with kernel mode, so these tests cover kernel-mode logic too.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#ifndef __EC_TRANSPORT_SIM_H__
#define __EC_TRANSPORT_SIM_H__

#include "ecrt.h"
#include "ectp.h"

/** Maximum number of simulated slaves per bus. */
#define SIM_MAX_SLAVES 8

/** Per-slave register space (standard register area + process data RAM). */
#define SIM_REG_SIZE 0x3000

/** SII EEPROM size in words. */
#define SIM_EEPROM_WORDS 0x100

typedef struct sim_bus sim_bus_t;

/** Identity used to build a slave's SII EEPROM image.
 *
 * If \a sm2_len or \a sm3_len is nonzero, the EEPROM contains a sync
 * manager category describing SM0/SM1 (disabled, no mailbox) and SM2
 * (process data output) / SM3 (process data input) at the given
 * physical addresses, so the slave can carry PDOs configured via
 * ecrt_slave_config_pdos(). */
typedef struct {
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision_number;
    uint32_t serial_number;
    uint16_t alias;
    uint16_t sm2_phys; /**< Physical start of the output SM (e.g. 0x1100). */
    uint16_t sm2_len;  /**< Default length of the output SM (0: no SM2/3). */
    uint16_t sm3_phys; /**< Physical start of the input SM (e.g. 0x1400). */
    uint16_t sm3_len;  /**< Default length of the input SM. */
    uint16_t mbox_out_phys; /**< Physical start of the receive (master ->
                              slave) mailbox / SM0 (e.g. 0x1000). */
    uint16_t mbox_out_len;  /**< Receive mailbox size (0: no mailbox). */
    uint16_t mbox_in_phys;  /**< Physical start of the send (slave ->
                              master) mailbox / SM1 (e.g. 0x1080). */
    uint16_t mbox_in_len;   /**< Send mailbox size. */
} sim_slave_identity_t;

/** Create a bus with \a nslaves slaves, each initialized from
 * \a identities (must have \a nslaves entries). Returns NULL on error. */
sim_bus_t *sim_bus_create(unsigned int nslaves,
        const sim_slave_identity_t *identities);

void sim_bus_destroy(sim_bus_t *bus);

/** The transport instance to pass to ecrt_startup_master(). Owned by the
 * bus; do not call ec_transport_destroy() on it. */
ec_transport_t *sim_bus_transport(sim_bus_t *bus);

/** Set the simulated link state (1 = up, 0 = down; initial: up).
 * With the link down, frames are dropped. */
void sim_bus_set_link(sim_bus_t *bus, int up);

/** Current AL state of slave \a pos (low nibble of the AL status
 * register, e.g. 1 = INIT, 2 = PREOP, 4 = SAFEOP, 8 = OP). */
unsigned int sim_bus_slave_al_state(sim_bus_t *bus, unsigned int pos);

/** Direct access to slave register memory (for test assertions and for
 * feeding simulated input process data). */
uint8_t *sim_bus_slave_regs(sim_bus_t *bus, unsigned int pos);

/** Number of frames processed by the bus so far. */
unsigned long sim_bus_frame_count(sim_bus_t *bus);

/** Create or update an object-dictionary entry of slave \a pos (CoE
 * slaves only; entries serve SDO uploads and accept downloads).
 * Returns 0 on success. */
int sim_bus_od_set(sim_bus_t *bus, unsigned int pos, uint16_t index,
        uint8_t subindex, const void *data, size_t size);

/** Look up an object-dictionary entry; returns the data pointer and
 * stores the current size in \a size, or NULL if not present. */
const uint8_t *sim_bus_od_data(sim_bus_t *bus, unsigned int pos,
        uint16_t index, uint8_t subindex, size_t *size);

/** Number of SDO download requests slave \a pos has received. */
unsigned long sim_bus_od_download_count(sim_bus_t *bus, unsigned int pos);

#endif
