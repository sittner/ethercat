/*****************************************************************************
 *
 *  Unit tests for PDO entry/list string formatting.
 *
 *  Regression test for the buffer overflow fixed in commit f5ed03a1: with
 *  more PDO entries than fit into the output buffer, the accumulated
 *  snprintf offset overran the buffer and wrote past its end.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include "pdo.h"
#include "pdo_list.h"

#include "test.h"

#define BUF_SIZE 256
#define CANARY 0x5a

int main(void)
{
    ec_pdo_t pdo;
    ec_pdo_list_t pl;
    unsigned char mem[3 * BUF_SIZE];
    char *buf = (char *) mem + BUF_SIZE;
    unsigned int i;

    /* Empty PDO prints "(none)". */
    ec_pdo_init(&pdo);
    pdo.index = 0x1a00;
    memset(mem, CANARY, sizeof(mem));
    ec_pdo_print_entries(&pdo, buf, BUF_SIZE);
    TEST_CHECK_STREQ("(none)", buf);

    /* One entry formats as index:subindex/bits. */
    TEST_CHECK(ec_pdo_add_entry(&pdo, 0x6000, 0x01, 8) != NULL);
    ec_pdo_print_entries(&pdo, buf, BUF_SIZE);
    TEST_CHECK_STREQ("0x6000:01/8", buf);

    /* Many entries: output must be truncated, not overflow (f5ed03a1). */
    for (i = 0; i < 40; i++) {
        TEST_CHECK(ec_pdo_add_entry(&pdo, 0x6000 + i, 0x01, 16) != NULL);
    }
    memset(mem, CANARY, sizeof(mem));
    ec_pdo_print_entries(&pdo, buf, BUF_SIZE);
    TEST_CHECK(memchr(buf, '\0', BUF_SIZE) != NULL); /* NUL-terminated */
    for (i = 0; i < BUF_SIZE; i++) { /* canaries around the buffer intact */
        TEST_CHECK_EQ(CANARY, mem[i]);
        TEST_CHECK_EQ(CANARY, mem[2 * BUF_SIZE + i]);
    }

    /* Zero-length buffer must not be written at all. */
    memset(mem, CANARY, sizeof(mem));
    ec_pdo_print_entries(&pdo, buf, 0);
    for (i = 0; i < sizeof(mem); i++) {
        TEST_CHECK_EQ(CANARY, mem[i]);
    }

    /* Same overflow pattern in the PDO list printer. */
    ec_pdo_list_init(&pl);
    memset(mem, CANARY, sizeof(mem));
    ec_pdo_list_print(&pl, buf, BUF_SIZE);
    TEST_CHECK_STREQ("(none)", buf);

    for (i = 0; i < 64; i++) {
        TEST_CHECK(ec_pdo_list_add_pdo(&pl, 0x1a00 + i) != NULL);
    }
    memset(mem, CANARY, sizeof(mem));
    ec_pdo_list_print(&pl, buf, BUF_SIZE);
    TEST_CHECK(memchr(buf, '\0', BUF_SIZE) != NULL);
    for (i = 0; i < BUF_SIZE; i++) {
        TEST_CHECK_EQ(CANARY, mem[i]);
        TEST_CHECK_EQ(CANARY, mem[2 * BUF_SIZE + i]);
    }

    ec_pdo_list_clear(&pl);
    ec_pdo_clear(&pdo);

    return test_done("test_pdo_print");
}
