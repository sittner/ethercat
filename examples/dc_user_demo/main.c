/*****************************************************************************
 *
 *  Copyright (C) 2007-2022  Florian Pose, Ingenieurgemeinschaft IgH
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
 ****************************************************************************/

/*
 * DC userspace demo example.
 *
 * Implements distributed clocks (DC) synchronization using only POSIX
 * userspace APIs.  Slave topology mirrors the linuxcnc-ethercat XML snippet:
 *
 *   0,0  EK1100  – bus coupler (selected as DC reference clock)
 *   0,1  EL1808  – 8-ch digital input  (1 byte input PDO)
 *   0,2  EL2808  – 8-ch digital output (1 byte output PDO, bit 0 toggled)
 *   0,3  fb1111  – generic DC slave (assignActivate=0x0300, sync0=1 ms)
 *
 * DC sync algorithm ported from examples/rtai_rtdm_dc/main.c.
 * Timing uses clock_gettime(CLOCK_MONOTONIC) / clock_nanosleep().
 *
 * Uses the userspace master library (EC_USPACE_MASTER):
 *   ecrt_lib_init() + ec_transport_create() + ecrt_startup_master()
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <sched.h>

#include "ecrt.h"

/****************************************************************************/

/* Application parameters */
#define FREQUENCY       1000
#define PERIOD_NS       (NSEC_PER_SEC / FREQUENCY)

/* DC filter length (number of cycles used for the low-pass filter) */
#define DC_FILTER_CNT   1024

/* Set to 1 to sync the master clock to the DC reference slave clock.
 * Set to 0 to sync the reference slave clock to the master clock. */
#define SYNC_MASTER_TO_REF  1

/****************************************************************************/

#define NSEC_PER_SEC    1000000000LL

#define TIMESPEC2NS(T)  ((uint64_t)(T).tv_sec * NSEC_PER_SEC + (T).tv_nsec)

/** Return the sign of a value (-1, 0 or +1). */
#define sign(val) \
    ({ typeof(val) _val = (val); ((_val > 0) - (_val < 0)); })

/****************************************************************************/

/* Transport and master handles */
static ec_transport_t    *transport    = NULL;
static ec_master_t       *master       = NULL;
static ec_master_state_t  master_state = {};

static ec_domain_t       *domain1      = NULL;
static ec_domain_state_t  domain1_state = {};

static uint8_t           *domain1_pd   = NULL;

/* Slave configurations */
static ec_slave_config_t *sc_ek1100    = NULL;
static ec_slave_config_t *sc_el1808    = NULL;
static ec_slave_config_t *sc_el2808    = NULL;
static ec_slave_config_t *sc_fb1111    = NULL;

/****************************************************************************/

/* Slave positions */
#define EK1100_Pos  0, 0
#define EL1808_Pos  0, 1
#define EL2808_Pos  0, 2
#define FB1111_Pos  0, 3

/* Vendor-ID / product-code pairs */
#define Beckhoff_EK1100  0x00000002, 0x044c2c52
#define Beckhoff_EL1808  0x00000002, 0x07103052
#define Beckhoff_EL2808  0x00000002, 0x0af83052
#define FB1111_VID_PID   0x00000002, 0x04570862

/****************************************************************************/

/* PDO entry offsets in the domain process-data image */
static int off_dig_in;   /* EL1808 input byte  */
static int off_dig_out;  /* EL2808 output byte */

/****************************************************************************/

/* DC variables */
static uint64_t dc_start_time_ns = 0ULL;
static uint64_t dc_time_ns       = 0ULL;

#if SYNC_MASTER_TO_REF
static uint8_t  dc_started          = 0;
static int32_t  dc_diff_ns          = 0;
static int32_t  prev_dc_diff_ns     = 0;
static int64_t  dc_diff_total_ns    = 0LL;
static int64_t  dc_delta_total_ns   = 0LL;
static int      dc_filter_idx       = 0;
static int64_t  dc_adjust_ns        = 0LL;
#endif

/* Offset added to (or subtracted from) CLOCK_MONOTONIC to track the
 * DC reference clock.  A positive value means the application clock
 * is running ahead of the reference. */
static int64_t  system_time_base    = 0LL;

/****************************************************************************/

/* Application state */
static volatile sig_atomic_t run = 1;
static unsigned int blink        = 0;

/****************************************************************************/

/** Return the current application time in nanoseconds, adjusted by
 *  system_time_base so that it tracks the DC reference clock.
 */
static uint64_t system_time_ns(void)
{
    struct timespec ts;
    uint64_t t;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t = TIMESPEC2NS(ts);

    if (system_time_base > (int64_t) t) {
        fprintf(stderr, "%s(): system_time_base (%lld) > time (%llu)\n",
                __func__,
                (long long) system_time_base,
                (unsigned long long) t);
        return t;
    }

    return t - (uint64_t) system_time_base;
}

/****************************************************************************/

/** Synchronise the distributed clocks.
 *
 *  Called just before ecrt_master_send() to set the most accurate
 *  master clock time.
 */
static void sync_distributed_clocks(void)
{
#if SYNC_MASTER_TO_REF
    uint32_t ref_time      = 0;
    uint64_t prev_app_time = dc_time_ns;
#endif

    dc_time_ns = system_time_ns();

#if SYNC_MASTER_TO_REF
    /* Get reference clock time to synchronise master cycle. */
    ecrt_master_reference_clock_time(master, &ref_time);
    dc_diff_ns = (uint32_t) prev_app_time - ref_time;
#else
    /* Sync reference clock to master. */
    ecrt_master_sync_reference_clock_to(master, dc_time_ns);
#endif

    /* Call to sync slaves to the reference slave. */
    ecrt_master_sync_slave_clocks(master);
}

/****************************************************************************/

/** Update the master time based on the ref-slave time difference.
 *
 *  Called after ecrt_master_send() to avoid time jitter in
 *  sync_distributed_clocks().
 */
static void update_master_clock(void)
{
#if SYNC_MASTER_TO_REF
    /* Calc drift (via un-normalised time diff). */
    int32_t delta     = dc_diff_ns - prev_dc_diff_ns;
    prev_dc_diff_ns   = dc_diff_ns;

    /* Normalise the time diff to ±(cycle/2). */
    dc_diff_ns =
        ((dc_diff_ns + (PERIOD_NS / 2)) % PERIOD_NS) - (PERIOD_NS / 2);

    if (dc_started) {
        /* Add to running totals. */
        dc_diff_total_ns  += dc_diff_ns;
        dc_delta_total_ns += delta;
        dc_filter_idx++;

        if (dc_filter_idx >= DC_FILTER_CNT) {
            /* Add rounded delta average. */
            dc_adjust_ns +=
                (dc_delta_total_ns + (DC_FILTER_CNT / 2)) / DC_FILTER_CNT;

            /* Pull in general drift. */
            dc_adjust_ns += sign(dc_diff_total_ns / DC_FILTER_CNT);

            /* Limit to ±1000 ns (0.1 % of a 1 ms cycle). */
            if (dc_adjust_ns < -1000) {
                dc_adjust_ns = -1000;
            }
            if (dc_adjust_ns > 1000) {
                dc_adjust_ns =  1000;
            }

            /* Reset accumulators. */
            dc_diff_total_ns  = 0LL;
            dc_delta_total_ns = 0LL;
            dc_filter_idx     = 0;
        }

        /* Apply cycles adjustment plus a spot correction. */
        system_time_base += dc_adjust_ns + sign(dc_diff_ns);
    } else {
        dc_started = (dc_diff_ns != 0);

        if (dc_started) {
            printf("First master diff: %d ns.\n", dc_diff_ns);
            dc_start_time_ns = dc_time_ns;
        }
    }
#endif
}

/****************************************************************************/

static void check_domain1_state(void)
{
    ec_domain_state_t ds;

    ecrt_domain_state(domain1, &ds);

    if (ds.working_counter != domain1_state.working_counter) {
        printf("Domain1: WC %u.\n", ds.working_counter);
    }
    if (ds.wc_state != domain1_state.wc_state) {
        printf("Domain1: State %u.\n", ds.wc_state);
    }

    domain1_state = ds;
}

/****************************************************************************/

static void check_master_state(void)
{
    ec_master_state_t ms;

    ecrt_master_state(master, &ms);

    if (ms.slaves_responding != master_state.slaves_responding) {
        printf("%u slave(s).\n", ms.slaves_responding);
    }
    if (ms.al_states != master_state.al_states) {
        printf("AL states: 0x%02X.\n", ms.al_states);
    }
    if (ms.link_up != master_state.link_up) {
        printf("Link is %s.\n", ms.link_up ? "up" : "down");
    }

    master_state = ms;
}

/****************************************************************************/

static void cyclic_task(void)
{
    struct timespec wakeup_ts;
    uint64_t        wakeup_time;
    unsigned int    counter = 0;

#ifdef MEASURE_TIMING
    struct timespec start_ts, end_ts, last_start_ts = {};
    uint32_t period_ns   = 0, exec_ns      = 0, latency_ns     = 0;
    uint32_t latency_min = 0xffffffff, latency_max  = 0;
    uint32_t period_min  = 0xffffffff, period_max   = 0;
    uint32_t exec_min    = 0xffffffff, exec_max     = 0;
#endif

    /* Compute first wakeup time a few cycles from now. */
    wakeup_time = system_time_ns() + 10 * PERIOD_NS;

    while (run) {
        /* Advance the wakeup time by one cycle. */
        wakeup_time += PERIOD_NS;

        /* Convert to struct timespec for clock_nanosleep().
         * The application clock may be shifted relative to CLOCK_MONOTONIC
         * by system_time_base; we must add it back here so the sleep
         * target is expressed in raw CLOCK_MONOTONIC time. */
        {
            uint64_t mono_time;

            if (system_time_base >= 0) {
                mono_time = wakeup_time + (uint64_t) system_time_base;
            } else {
                uint64_t neg = (uint64_t)(-system_time_base);
                mono_time = (wakeup_time > neg) ? (wakeup_time - neg) : 0;
            }

            wakeup_ts.tv_sec  = (time_t)(mono_time / NSEC_PER_SEC);
            wakeup_ts.tv_nsec = (long)(mono_time % NSEC_PER_SEC);
        }

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_ts, NULL);

        /* Inform the master of the current application time. */
        ecrt_master_application_time(master, wakeup_time);

#ifdef MEASURE_TIMING
        clock_gettime(CLOCK_MONOTONIC, &start_ts);
        {
            uint64_t start_ns = TIMESPEC2NS(start_ts);
            uint64_t wake_ns  = TIMESPEC2NS(wakeup_ts);
            latency_ns = (uint32_t)(start_ns > wake_ns
                    ? start_ns - wake_ns : 0);
        }
        {
            uint64_t s0 = TIMESPEC2NS(start_ts);
            uint64_t s1 = TIMESPEC2NS(last_start_ts);
            period_ns  = (uint32_t)(s0 > s1 ? s0 - s1 : 0);
        }
        {
            uint64_t s0 = TIMESPEC2NS(last_start_ts);
            uint64_t e0 = TIMESPEC2NS(end_ts);
            exec_ns = (uint32_t)(e0 > s0 ? e0 - s0 : 0);
        }
        last_start_ts = start_ts;

        if (latency_ns > latency_max) latency_max = latency_ns;
        if (latency_ns < latency_min) latency_min = latency_ns;
        if (period_ns  > period_max)  period_max  = period_ns;
        if (period_ns  < period_min)  period_min  = period_ns;
        if (exec_ns    > exec_max)    exec_max    = exec_ns;
        if (exec_ns    < exec_min)    exec_min    = exec_ns;
#endif

        /* Receive and process EtherCAT frames. */
        ecrt_master_receive(master);
        ecrt_domain_process(domain1);

        /* Check process data state. */
        check_domain1_state();

        if (counter) {
            counter--;
        } else {
            counter = FREQUENCY; /* reset to 1 Hz */

            check_master_state();

#if SYNC_MASTER_TO_REF
            printf("DC diff: %d ns\n", dc_diff_ns);
#endif

#ifdef MEASURE_TIMING
            printf("period     %10u ... %10u\n", period_min,  period_max);
            printf("exec       %10u ... %10u\n", exec_min,    exec_max);
            printf("latency    %10u ... %10u\n", latency_min, latency_max);

            period_max  = 0; period_min  = 0xffffffff;
            exec_max    = 0; exec_min    = 0xffffffff;
            latency_max = 0; latency_min = 0xffffffff;
#endif
        }

        /* Toggle EL2808 output bit 0 every cycle. */
        blink = !blink;
        EC_WRITE_U8(domain1_pd + off_dig_out, blink ? 0x01 : 0x00);

        /* Queue and send EtherCAT frames. */
        ecrt_domain_queue(domain1);
        sync_distributed_clocks();
        ecrt_master_send(master);

        /* Update master clock after send to minimise jitter. */
        update_master_clock();

#ifdef MEASURE_TIMING
        clock_gettime(CLOCK_MONOTONIC, &end_ts);
#endif
    }
}

/****************************************************************************/

static void signal_handler(int sig)
{
    (void) sig;
    run = 0;
}

/****************************************************************************/

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s -i <interface> [-t <transport>] [-d <debug_level>]\n"
            "\n"
            "  -i <interface>     Network interface name (required)\n"
            "  -t <transport>     Transport type: raw (default), xdp-skb,"
            " xdp-native\n"
            "  -d <level>         Debug level (default: 1)\n"
            "  -h                 Show this help\n",
            prog);
}

/****************************************************************************/

int main(int argc, char *argv[])
{
    const char *interface    = NULL;
    const char *transport_name = "raw";
    unsigned int debug_level = 1;
    int ret;
    int c;

    while ((c = getopt(argc, argv, "i:t:d:h")) != -1) {
        switch (c) {
            case 'i':
                interface = optarg;
                break;
            case 't':
                transport_name = optarg;
                break;
            case 'd':
                debug_level = (unsigned int) strtoul(optarg, NULL, 0);
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (!interface) {
        fprintf(stderr, "Error: network interface required (-i option)\n");
        usage(argv[0]);
        return 1;
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
        return -1;
    }

    /* Initialise the userspace master library.
     * Pass EC_IPC_DEFAULT_SOCKET_PATH so the ethercat tool can connect. */
    ret = ecrt_lib_init(NULL, EC_IPC_DEFAULT_SOCKET_PATH);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialise EtherCAT library: %d\n", ret);
        return -1;
    }

    /* Create the transport for the given interface. */
    ret = ec_transport_find_by_name(transport_name);
    if (ret < 0) {
        fprintf(stderr, "Unknown transport type '%s'\n", transport_name);
        fprintf(stderr, "Available transports: ");
        ec_transport_print_available();
        fprintf(stderr, "\n");
        ecrt_lib_cleanup();
        return -1;
    }
    transport = ec_transport_create((ec_transport_type_t) ret, interface);
    if (!transport) {
        fprintf(stderr, "Failed to create transport on %s.\n", interface);
        ecrt_lib_cleanup();
        return -1;
    }

    printf("Starting master on %s (transport: %s)...\n",
            interface, transport_name);
    master = ecrt_startup_master(0, transport, NULL, debug_level, -1);
    if (!master) {
        fprintf(stderr, "Failed to start EtherCAT master.\n");
        ec_transport_destroy(transport);
        ecrt_lib_cleanup();
        return -1;
    }

    domain1 = ecrt_master_create_domain(master);
    if (!domain1) {
        fprintf(stderr, "Failed to create domain.\n");
        ret = -1;
        goto out_release;
    }

    printf("Creating slave configurations...\n");

    /* 0,0 – EK1100 bus coupler (also the DC reference clock) */
    sc_ek1100 = ecrt_master_slave_config(master, EK1100_Pos, Beckhoff_EK1100);
    if (!sc_ek1100) {
        fprintf(stderr, "Failed to configure EK1100.\n");
        ret = -1;
        goto out_release;
    }

    /* 0,1 – EL1808 8-ch digital input */
    sc_el1808 = ecrt_master_slave_config(master, EL1808_Pos, Beckhoff_EL1808);
    if (!sc_el1808) {
        fprintf(stderr, "Failed to configure EL1808.\n");
        ret = -1;
        goto out_release;
    }

    off_dig_in = ecrt_slave_config_reg_pdo_entry(sc_el1808,
            0x6000, 0x01, domain1, NULL);
    if (off_dig_in < 0) {
        fprintf(stderr, "Failed to register EL1808 PDO entry.\n");
        ret = -1;
        goto out_release;
    }

    /* 0,2 – EL2808 8-ch digital output */
    sc_el2808 = ecrt_master_slave_config(master, EL2808_Pos, Beckhoff_EL2808);
    if (!sc_el2808) {
        fprintf(stderr, "Failed to configure EL2808.\n");
        ret = -1;
        goto out_release;
    }

    off_dig_out = ecrt_slave_config_reg_pdo_entry(sc_el2808,
            0x7000, 0x01, domain1, NULL);
    if (off_dig_out < 0) {
        fprintf(stderr, "Failed to register EL2808 PDO entry.\n");
        ret = -1;
        goto out_release;
    }

    /* 0,3 – fb1111 generic DC slave
     * assignActivate=0x0300, sync0Cycle=PERIOD_NS, sync0Shift=0 */
    sc_fb1111 = ecrt_master_slave_config(master, FB1111_Pos, FB1111_VID_PID);
    if (!sc_fb1111) {
        fprintf(stderr, "Failed to configure fb1111.\n");
        ret = -1;
        goto out_release;
    }

    ecrt_slave_config_dc(sc_fb1111, 0x0300, PERIOD_NS, 0, 0, 0);

    /* Select EK1100 as the DC reference clock. */
    ret = ecrt_master_select_reference_clock(master, sc_ek1100);
    if (ret < 0) {
        fprintf(stderr, "Failed to select reference clock: %s\n",
                strerror(-ret));
        ret = -1;
        goto out_release;
    }

    /* Record the initial master time. */
    dc_start_time_ns = system_time_ns();
    dc_time_ns       = dc_start_time_ns;

    printf("Activating master...\n");
    if (ecrt_master_activate(master)) {
        fprintf(stderr, "Failed to activate master.\n");
        ret = -1;
        goto out_release;
    }

    domain1_pd = ecrt_domain_data(domain1);
    if (!domain1_pd) {
        fprintf(stderr, "Failed to get domain data pointer.\n");
        ret = -1;
        goto out_release;
    }

    /* Set SCHED_FIFO priority. */
    {
        struct sched_param param = {};
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        printf("Using priority %i.\n", param.sched_priority);
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
            perror("sched_setscheduler failed");
        }
    }

    printf("Starting cyclic task.\n");
    cyclic_task();
    ret = 0;

out_release:
    printf("Shutting down.\n");
    ecrt_release_master(master);
    ec_transport_destroy(transport);
    ecrt_lib_cleanup();

    return ret;
}

/****************************************************************************/

