/*****************************************************************************
 *
 *  Minimal unit test harness for the EtherCAT master.
 *
 *  Each test program is a standalone executable; a non-zero exit status
 *  marks the test as failed for the automake TESTS driver.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#ifndef __EC_TEST_H__
#define __EC_TEST_H__

#include <stdio.h>
#include <string.h>

static int ec_test_checks = 0;
static int ec_test_failures = 0;

/** Check a condition; on failure report and continue with the next check. */
#define TEST_CHECK(cond) \
    do { \
        ec_test_checks++; \
        if (!(cond)) { \
            ec_test_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/** Check integer equality; on failure report both values. */
#define TEST_CHECK_EQ(expected, actual) \
    do { \
        ec_test_checks++; \
        long long _e = (long long) (expected), _a = (long long) (actual); \
        if (_e != _a) { \
            ec_test_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s == %s (expected %lld, got" \
                    " %lld)\n", __FILE__, __LINE__, #expected, #actual, \
                    _e, _a); \
        } \
    } while (0)

/** Check string equality. */
#define TEST_CHECK_STREQ(expected, actual) \
    do { \
        ec_test_checks++; \
        if (strcmp((expected), (actual))) { \
            ec_test_failures++; \
            fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                    __FILE__, __LINE__, (expected), (actual)); \
        } \
    } while (0)

/** Print the summary and return the process exit status. */
static inline int test_done(const char *name)
{
    fprintf(stderr, "%s: %d/%d checks passed\n", name,
            ec_test_checks - ec_test_failures, ec_test_checks);
    return ec_test_failures ? 1 : 0;
}

#endif
