/*****************************************************************************
 *
 *  Unit tests for the CoE emergency message ring (master/coe_emerg_ring.c).
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include <errno.h>

#include "coe_emerg_ring.h"

#include "test.h"

int main(void)
{
    ec_coe_emerg_ring_t ring;
    uint8_t msg[EC_COE_EMERGENCY_MSG_SIZE];
    uint8_t out[EC_COE_EMERGENCY_MSG_SIZE];
    unsigned int i;

    ec_coe_emerg_ring_init(&ring, NULL);

    /* Unsized ring drops pushes and counts overruns. */
    memset(msg, 0x11, sizeof(msg));
    ec_coe_emerg_ring_push(&ring, msg);
    TEST_CHECK_EQ(1, ec_coe_emerg_ring_overruns(&ring));
    TEST_CHECK_EQ(-ENOENT, ec_coe_emerg_ring_pop(&ring, out));

    /* Resizing keeps the overrun count; only clear_ring() resets it. */
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_size(&ring, 4));
    TEST_CHECK_EQ(1, ec_coe_emerg_ring_overruns(&ring));
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_clear_ring(&ring));
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_overruns(&ring));

    /* FIFO order across ring capacity. */
    for (i = 0; i < 4; i++) {
        memset(msg, i, sizeof(msg));
        ec_coe_emerg_ring_push(&ring, msg);
    }
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_overruns(&ring));

    /* Fifth push into a full ring must overrun, not overwrite. */
    memset(msg, 0xff, sizeof(msg));
    ec_coe_emerg_ring_push(&ring, msg);
    TEST_CHECK_EQ(1, ec_coe_emerg_ring_overruns(&ring));

    for (i = 0; i < 4; i++) {
        TEST_CHECK_EQ(0, ec_coe_emerg_ring_pop(&ring, out));
        TEST_CHECK_EQ(i, out[0]);
    }
    TEST_CHECK_EQ(-ENOENT, ec_coe_emerg_ring_pop(&ring, out));

    /* clear_ring() empties the ring and resets the overrun counter. */
    memset(msg, 0x22, sizeof(msg));
    ec_coe_emerg_ring_push(&ring, msg);
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_clear_ring(&ring));
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_overruns(&ring));
    TEST_CHECK_EQ(-ENOENT, ec_coe_emerg_ring_pop(&ring, out));

    /* Resizing to zero disables the ring again. */
    TEST_CHECK_EQ(0, ec_coe_emerg_ring_size(&ring, 0));
    ec_coe_emerg_ring_push(&ring, msg);
    TEST_CHECK_EQ(1, ec_coe_emerg_ring_overruns(&ring));

    /* Allocation-size overflow must be rejected, not wrap. */
    TEST_CHECK_EQ(-EINVAL, ec_coe_emerg_ring_size(&ring, (size_t) -1));

    ec_coe_emerg_ring_clear(&ring);

    return test_done("test_emerg_ring");
}
