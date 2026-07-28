/*****************************************************************************
 *
 *  Unit test: the guard that decides when a background SDO dictionary fetch
 *  may start (ec_fsm_slave_action_process_dict()).
 *
 *  The fetch runs on the slave's own FSM, in parallel with everything the
 *  master FSM does, so the guard is the only thing keeping it away from a
 *  slave that is still being scanned or configured — both of which use the
 *  same mailbox.  Two conditions carry that weight:
 *
 *    - master->allow_sdo_dict, granted only once no slave needs
 *      configuration;
 *    - the slave has settled in PREOP for EC_WAIT_SDO_DICT seconds.
 *
 *  The second is measured from slave->time_preop, which stays zero until
 *  this master drives the slave into PREOP itself.  Measuring an elapsed
 *  time from zero makes the comparison trivially true, so "never configured"
 *  has to be rejected explicitly — otherwise the settling time silently does
 *  not apply to exactly the slaves that need it most.
 *
 *  Reaching that state through the FSM is not currently possible, which is
 *  why this is a unit test of the guard rather than a bus scenario.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include <string.h>

#include "master_globals.h"
#include "master.h"
#include "fsm_slave.h"

#include "test.h"

/** Both are far too large for the stack. */
static ec_master_t master;
static ec_slave_t slave;

static ec_fsm_slave_t fsm;
static ec_datagram_t datagram;

/** Put the slave FSM back where state_ready() would call the action from.
 * \a fetched carries the "dictionary already read" flag over between cases. */
static void arm(int fetched)
{
    slave.sdo_dictionary_fetched = fetched ? 1 : 0;
    slave.current_state = EC_SLAVE_STATE_PREOP;

    ec_fsm_slave_clear(&fsm);
    ec_fsm_slave_init(&fsm, &slave);
    ec_fsm_slave_set_ready(&fsm);
    TEST_CHECK(ec_fsm_slave_is_ready(&fsm));
}

int main(void)
{
    ec_time_t settled = ec_ms_to_time((EC_WAIT_SDO_DICT + 1) * 1000);

    TEST_CHECK_EQ(0, ecrt_lib_init(NULL, NULL));

    memset(&master, 0, sizeof(master));
    memset(&slave, 0, sizeof(slave));

    slave.master = &master;
    slave.station_address = 0x1001;
    slave.sii.mailbox_protocols = EC_MBOX_COE;
    slave.sii.has_general = 0;
    slave.configured_rx_mailbox_offset = 0x1000;
    slave.configured_rx_mailbox_size = 128;
    slave.configured_tx_mailbox_offset = 0x1080;
    slave.configured_tx_mailbox_size = 128;

    ec_fsm_slave_init(&fsm, &slave);
    ec_datagram_init(&datagram);
    TEST_CHECK_EQ(0, ec_datagram_prealloc(&datagram, EC_MAX_DATA_SIZE));

    /* 1. Without the master's permission nothing starts, however long the
     *    slave has been sitting in PREOP. */
    master.allow_sdo_dict = 0;
    slave.time_preop = ec_current_time() - settled;
    arm(0);
    TEST_CHECK_EQ(0, ec_fsm_slave_action_process_dict(&fsm, &datagram));
    TEST_CHECK_EQ(0, slave.sdo_dictionary_fetched);

    /* 2. Permission granted, but this master never drove the slave to PREOP:
     *    there is no settling time to measure, so no fetch. This is the case
     *    a plain elapsed-time test gets wrong — the subtraction from zero
     *    yields the whole monotonic clock and sails past the threshold. */
    master.allow_sdo_dict = 1;
    slave.time_preop = 0;
    arm(0);
    TEST_CHECK_EQ(0, ec_fsm_slave_action_process_dict(&fsm, &datagram));
    TEST_CHECK_EQ(0, slave.sdo_dictionary_fetched);
    TEST_CHECK(ec_fsm_slave_is_ready(&fsm));

    /* 3. Just arrived in PREOP: the settling time still has to elapse. */
    slave.time_preop = ec_current_time();
    arm(0);
    TEST_CHECK_EQ(0, ec_fsm_slave_action_process_dict(&fsm, &datagram));
    TEST_CHECK_EQ(0, slave.sdo_dictionary_fetched);

    /* 4. Configured and settled: the fetch starts, and the slave FSM is busy
     *    for its duration (which is what defers configuration of this slave,
     *    see ec_fsm_master_fill_config_slot()). */
    slave.time_preop = ec_current_time() - settled;
    arm(0);
    TEST_CHECK_EQ(1, ec_fsm_slave_action_process_dict(&fsm, &datagram));
    TEST_CHECK_EQ(1, slave.sdo_dictionary_fetched);
    TEST_CHECK(ec_fsm_slave_is_busy(&fsm));

    /* 5. And it is fetched once: a second pass finds it done. */
    arm(1);
    TEST_CHECK_EQ(0, ec_fsm_slave_action_process_dict(&fsm, &datagram));

    ec_datagram_clear(&datagram);
    ec_fsm_slave_clear(&fsm);
    ecrt_lib_cleanup();

    return test_done("test_dict_gate");
}
