/*****************************************************************************
 *
 *  Unit tests for datagram construction (master/datagram.c).
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include "datagram.h"

#include "test.h"

int main(void)
{
    ec_datagram_t dg;

    ec_datagram_init(&dg);
    TEST_CHECK_EQ(EC_DATAGRAM_INIT, dg.state);
    TEST_CHECK_EQ(0, dg.data_size);
    TEST_CHECK(dg.data == NULL);

    /* Preallocation provides (RT-locked) payload memory. */
    TEST_CHECK_EQ(0, ec_datagram_prealloc(&dg, 64));
    TEST_CHECK(dg.data != NULL);
    TEST_CHECK(dg.mem_size >= 64);

    /* Broadcast read: type, address and payload size. */
    TEST_CHECK_EQ(0, ec_datagram_brd(&dg, 0x0130, 2));
    TEST_CHECK_EQ(EC_DATAGRAM_BRD, dg.type);
    TEST_CHECK_EQ(2, dg.data_size);
    TEST_CHECK_EQ(0x00, dg.address[0]); /* broadcast: position 0 */
    TEST_CHECK_EQ(0x30, dg.address[2]); /* offset, little endian */
    TEST_CHECK_EQ(0x01, dg.address[3]);

    /* Configured-address read. */
    TEST_CHECK_EQ(0, ec_datagram_fprd(&dg, 0x03e9, 0x0110, 4));
    TEST_CHECK_EQ(EC_DATAGRAM_FPRD, dg.type);
    TEST_CHECK_EQ(4, dg.data_size);
    TEST_CHECK_EQ(0xe9, dg.address[0]); /* station address, little endian */
    TEST_CHECK_EQ(0x03, dg.address[1]);
    TEST_CHECK_EQ(0x10, dg.address[2]);
    TEST_CHECK_EQ(0x01, dg.address[3]);

    /* Growing beyond the preallocation must reallocate. */
    TEST_CHECK_EQ(0, ec_datagram_prealloc(&dg, 1024));
    TEST_CHECK(dg.mem_size >= 1024);
    TEST_CHECK_EQ(0, ec_datagram_bwr(&dg, 0x0120, 512));
    TEST_CHECK_EQ(EC_DATAGRAM_BWR, dg.type);
    TEST_CHECK_EQ(512, dg.data_size);

    /* Payload is writable across the whole requested size. */
    memset(dg.data, 0xa5, dg.data_size);
    TEST_CHECK_EQ(0xa5, dg.data[511]);

    ec_datagram_clear(&dg);

    return test_done("test_datagram");
}
