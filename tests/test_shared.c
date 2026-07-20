/*****************************************************************************
 *
 *  Unit tests for the REAL/LREAL PDO accessors (lib/shared.c) and the
 *  EC_READ_*()/EC_WRITE_*() data access macros.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include <math.h>
#include <stdint.h>

#include "ecrt.h"

#include "test.h"

int main(void)
{
    unsigned char buf[16];
    unsigned int i;

    const float fv[] = { 0.0f, -1.5f, 3.1415927f, 1e-38f, INFINITY };
    const double dv[] = { 0.0, -1.5, 2.718281828459045, 1e-300, -INFINITY };

    for (i = 0; i < sizeof(fv) / sizeof(fv[0]); i++) {
        /* Deliberately unaligned buffer offset. */
        ecrt_write_real(buf + 1, fv[i]);
        float f = ecrt_read_real(buf + 1);
        TEST_CHECK(memcmp(&f, &fv[i], sizeof(f)) == 0);

        ecrt_write_lreal(buf + 1, dv[i]);
        double d = ecrt_read_lreal(buf + 1);
        TEST_CHECK(memcmp(&d, &dv[i], sizeof(d)) == 0);
    }

    /* REAL wire format: IEEE 754 little-endian (1.0f = 0x3f800000). */
    ecrt_write_real(buf, 1.0f);
    TEST_CHECK(buf[0] == 0x00 && buf[1] == 0x00 &&
            buf[2] == 0x80 && buf[3] == 0x3f);

    /* Integer macros: little-endian wire format and sign extension. */
    EC_WRITE_U32(buf, 0x12345678u);
    TEST_CHECK(buf[0] == 0x78 && buf[1] == 0x56 &&
            buf[2] == 0x34 && buf[3] == 0x12);
    TEST_CHECK_EQ(0x12345678u, EC_READ_U32(buf));

    EC_WRITE_S16(buf, -2);
    TEST_CHECK_EQ(-2, EC_READ_S16(buf));
    TEST_CHECK_EQ(0xfffe, EC_READ_U16(buf));

    EC_WRITE_U64(buf, UINT64_C(0x0102030405060708));
    TEST_CHECK(buf[0] == 0x08 && buf[7] == 0x01);
    TEST_CHECK(EC_READ_U64(buf) == UINT64_C(0x0102030405060708));

    return test_done("test_shared");
}
