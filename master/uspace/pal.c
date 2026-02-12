/*****************************************************************************
 *
 *  Copyright (C) 2006-2024  Florian Pose, Ingenieurgemeinschaft IgH
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

#include "pal.h"

#include "../master.h"
#include "../fsm_master.h"

static bool printk_newline = false;
static pthread_mutex_t printk_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *loglevel_names[] = {
    "EMERG",
    "ALERT",
    "CRIT",
    "ERR",
    "WARN",
    "NOTICE",
    "INFO",
    "DEBUG",
};

int printk(const char *fmt, ...)
{
    va_list args;
    int priority = LOG_INFO;
    const char *msg = fmt;
    int ret = 0;
    bool is_cont = false;
    size_t len;

    if (!fmt)
        return 0;

    pthread_mutex_lock(&printk_lock);

    /* Check for log level prefix */
    if (fmt[0] == '<' && fmt[1] != 0 && fmt[2] == '>') {
        if (fmt[1] >= '0' && fmt[1] <= '7') {
            priority = fmt[1] - '0';
            msg = fmt + 3;
        } else if (fmt[1] == 'c') {
            is_cont = true;
            msg = fmt + 3;
        }
    }

    /* Force newline if starting new message but previous wasn't complete */
    if (!is_cont) {
        if (printk_newline) {
            putchar('\n');
            printk_newline = false;
        }
        printf("%s: ", loglevel_names[priority]);
    }

    va_start(args, fmt);
    ret = vprintf(msg, args);
    va_end(args);

    /* Track newline state */
    len = strlen(msg);
    printk_newline = (len > 0 && msg[len - 1] != '\n');

    fflush(stdout);

    pthread_mutex_unlock(&printk_lock);
    return ret;
}

static void ec_master_nanosleep(const unsigned long nsecs) {
    struct timespec ts = {
        .tv_sec = nsecs / 1000000000UL,
        .tv_nsec = nsecs % 1000000000UL
    };

    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

void ec_master_idle_thread_schedule(ec_master_t *master, int sent_bytes) {
    if (ec_fsm_master_idle(&master->fsm)) {
        ec_master_nanosleep(master->send_interval * 1000);
    } else {
        ec_master_nanosleep(sent_bytes * EC_BYTE_TRANSMISSION_TIME_NS);
    }
}

void ec_master_operation_thread_schedule(ec_master_t *master) {
    // the op thread should not work faster than the sending RT thread
    ec_master_nanosleep(master->send_interval * 1000);
}

//TODO
struct workqueue_struct *system_wq;
struct pal_irq_work_queue *irq_work_queue_global;

