/*****************************************************************************
 *
 *  transport_sim — in-process simulated EtherCAT bus for unit tests.
 *
 *  See transport_sim.h for the concept. Emulation scope:
 *
 *  - Datagram commands: NOP, APRD/APWR, FPRD/FPWR, BRD (bitwise OR
 *    semantics)/BWR. Logical (FMMU) and DC commands are not implemented
 *    yet; they return working counter 0.
 *  - Auto-increment addressing: the position field is incremented by
 *    every slave the frame passes; the slave seeing position 0 processes.
 *  - Register space per slave, with side effects on:
 *      0x0120 AL control  -> AL status (0x0130) acks the requested state,
 *                            AL status code (0x0134) is cleared
 *      0x0502 SII control -> read op serves 2 words from the EEPROM image
 *                            at 0x0508, then clears the busy/op bits;
 *                            write ops set the error bit (unsupported)
 *  - Base information block (0x0000): 8 FMMUs, 8 sync managers, MII
 *    ports 0/1, no DC support.
 *  - Working counters: +1 per matching slave for read or write commands.
 *
 *  Frames are processed synchronously in send() and queued for the next
 *  receive() call — one cycle of latency, like a real bus.
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 ****************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transport_sim.h"

/****************************************************************************/

#define SIM_ETH_HLEN 14
#define SIM_ECAT_HDR_LEN 2
#define SIM_DGRAM_HDR_LEN 10
#define SIM_RXQ_LEN 32

typedef struct {
    uint8_t regs[SIM_REG_SIZE];
    uint16_t eeprom[SIM_EEPROM_WORDS];
} sim_slave_t;

struct sim_bus {
    ec_transport_t transport; /**< Embedded transport instance. */
    sim_slave_t slaves[SIM_MAX_SLAVES];
    unsigned int nslaves;
    int link_up;
    unsigned long frame_count;
    pthread_mutex_t lock;

    struct {
        uint8_t data[EC_TRANSPORT_MAX_FRAME_SIZE];
        size_t len;
    } rxq[SIM_RXQ_LEN];
    unsigned int rxq_head; /**< Next slot to fill. */
    unsigned int rxq_tail; /**< Next slot to drain. */
};

/****************************************************************************/

static uint16_t sim_rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static void sim_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

/****************************************************************************/

/** Build the SII EEPROM image for one slave.
 *
 * Word layout (ETG.1000.6): 0x0004 alias, 0x0008 vendor id,
 * 0x000A product code, 0x000C revision, 0x000E serial, 0x0018..0x001C
 * mailbox configuration (zero: no mailbox), 0x0040 first category
 * header (0xFFFF: end marker).
 */
static void sim_slave_init_eeprom(sim_slave_t *slave,
        const sim_slave_identity_t *id)
{
    uint16_t *ee = slave->eeprom;

    memset(ee, 0, sizeof(slave->eeprom));

    ee[0x0004] = id->alias;
    ee[0x0008] = (uint16_t) id->vendor_id;
    ee[0x0009] = (uint16_t) (id->vendor_id >> 16);
    ee[0x000A] = (uint16_t) id->product_code;
    ee[0x000B] = (uint16_t) (id->product_code >> 16);
    ee[0x000C] = (uint16_t) id->revision_number;
    ee[0x000D] = (uint16_t) (id->revision_number >> 16);
    ee[0x000E] = (uint16_t) id->serial_number;
    ee[0x000F] = (uint16_t) (id->serial_number >> 16);

    ee[0x0040] = 0xFFFF; /* end-of-categories marker */
}

static void sim_slave_init_regs(sim_slave_t *slave, unsigned int pos,
        unsigned int nslaves)
{
    uint8_t *r = slave->regs;

    memset(r, 0, SIM_REG_SIZE);

    /* Base information block. */
    r[0x0000] = 0x11; /* type */
    r[0x0001] = 0x00; /* revision */
    sim_wr16(r + 0x0002, 0x0001); /* build */
    r[0x0004] = 8; /* FMMU count */
    r[0x0005] = 8; /* sync manager count */
    r[0x0007] = 0x0F; /* ports 0+1: MII, ports 2+3: not implemented */
    r[0x0008] = 0x00; /* features: no FMMU bit op, no DC */

    /* DL status: bit 4+i = link on port i, bit 8+2i = loop closed on
     * port i, bit 9+2i = signal detected on port i. Port 0 is the
     * upstream port (link + signal, loop open); port 1 is downstream
     * (open towards the next slave, loop closed on the last one); ports
     * 2/3 are unimplemented (loop closed). The topology calculation
     * follows every port whose loop is open, so the last slave MUST
     * close port 1 or the recursion runs off the chain. */
    {
        uint16_t dl = 0x0010 | 0x0200 | 0x1000 | 0x4000;
        if (pos + 1 < nslaves) {
            dl |= 0x0020 | 0x0800; /* port 1: link + signal, loop open */
        } else {
            dl |= 0x0400; /* port 1: loop closed */
        }
        sim_wr16(r + 0x0110, dl);
    }

    /* AL status: INIT. */
    sim_wr16(r + 0x0130, 0x0001);
}

/****************************************************************************/

/** Apply write side effects after data has been copied into the register
 * space. \a adr / \a len describe the written range. */
static void sim_slave_write_effects(sim_slave_t *slave, uint16_t adr,
        uint16_t len)
{
    uint8_t *r = slave->regs;

    /* AL control (0x0120): acknowledge the requested state. */
    if (adr <= 0x0120 && 0x0120 < adr + len) {
        uint16_t ctl = sim_rd16(r + 0x0120);
        sim_wr16(r + 0x0130, ctl & 0x000F);
        sim_wr16(r + 0x0134, 0x0000); /* AL status code: no error */
    }

    /* SII control (0x0502): execute the requested EEPROM operation. */
    if (adr <= 0x0502 && 0x0502 < adr + len) {
        uint8_t op = r[0x0503] & 0x03;
        uint16_t waddr = sim_rd16(r + 0x0504);

        if (op & 0x01) { /* read: serve two words */
            unsigned int i;
            for (i = 0; i < 2; i++) {
                uint16_t w = (waddr + i < SIM_EEPROM_WORDS)
                        ? slave->eeprom[waddr + i] : 0xFFFF;
                sim_wr16(r + 0x0508 + 2 * i, w);
            }
            r[0x0502] &= (uint8_t) ~0x03;
            r[0x0503] = 0x00; /* idle: no busy, no error */
        } else if (op) { /* write/reload: not supported by the sim */
            r[0x0503] = 0x20; /* error on last command */
        }
    }
}

/** Process one datagram against one slave. Returns the working counter
 * increment. */
static unsigned int sim_slave_process(sim_slave_t *slave, uint8_t cmd,
        uint16_t adr, uint8_t *data, uint16_t len)
{
    if ((size_t) adr + len > SIM_REG_SIZE) {
        return 0;
    }

    switch (cmd) {
        case 0x01: /* APRD */
        case 0x04: /* FPRD */
            memcpy(data, slave->regs + adr, len);
            return 1;
        case 0x07: /* BRD: bitwise OR of all slaves' data */
            {
                uint16_t i;
                for (i = 0; i < len; i++) {
                    data[i] |= slave->regs[adr + i];
                }
            }
            return 1;
        case 0x02: /* APWR */
        case 0x05: /* FPWR */
        case 0x08: /* BWR */
            memcpy(slave->regs + adr, data, len);
            sim_slave_write_effects(slave, adr, len);
            return 1;
        default:
            return 0;
    }
}

/** Process one datagram against the whole bus (in place). */
static void sim_bus_process_datagram(sim_bus_t *bus, uint8_t cmd,
        uint8_t *addr, uint8_t *data, uint16_t len, uint8_t *wkc_field)
{
    uint16_t wkc = sim_rd16(wkc_field);
    uint16_t offset = sim_rd16(addr + 2);
    unsigned int i;

    switch (cmd) {
        case 0x01: /* APRD */
        case 0x02: /* APWR */
            {
                /* Position addressing: the slave seeing position 0
                 * processes; every slave increments the field. */
                int16_t pos = (int16_t) sim_rd16(addr);
                for (i = 0; i < bus->nslaves; i++) {
                    if (pos == 0) {
                        wkc = (uint16_t) (wkc + sim_slave_process(
                                &bus->slaves[i], cmd, offset, data, len));
                    }
                    pos++;
                }
                sim_wr16(addr, (uint16_t) pos);
            }
            break;
        case 0x04: /* FPRD */
        case 0x05: /* FPWR */
            {
                uint16_t station = sim_rd16(addr);
                for (i = 0; i < bus->nslaves; i++) {
                    if (sim_rd16(bus->slaves[i].regs + 0x0010) == station) {
                        wkc = (uint16_t) (wkc + sim_slave_process(
                                &bus->slaves[i], cmd, offset, data, len));
                    }
                }
            }
            break;
        case 0x07: /* BRD */
        case 0x08: /* BWR */
            {
                int16_t pos = (int16_t) sim_rd16(addr);
                for (i = 0; i < bus->nslaves; i++) {
                    wkc = (uint16_t) (wkc + sim_slave_process(
                            &bus->slaves[i], cmd, offset, data, len));
                    pos++;
                }
                sim_wr16(addr, (uint16_t) pos);
            }
            break;
        default:
            /* NOP, logical and DC commands: no slave processes. */
            break;
    }

    sim_wr16(wkc_field, wkc);
}

/** Process a full frame in place. Returns 0 if the frame was valid. */
static int sim_bus_process_frame(sim_bus_t *bus, uint8_t *frame, size_t size)
{
    size_t pos = SIM_ETH_HLEN;
    uint16_t ecat_hdr, ecat_len;
    int more;

    if (size < SIM_ETH_HLEN + SIM_ECAT_HDR_LEN) {
        return -EINVAL;
    }
    if (frame[12] != 0x88 || frame[13] != 0xA4) {
        return -EINVAL; /* not an EtherCAT frame */
    }

    ecat_hdr = sim_rd16(frame + pos);
    ecat_len = ecat_hdr & 0x07FF;
    if ((ecat_hdr >> 12) != 0x1) {
        return -EINVAL; /* not a datagram-type frame */
    }
    pos += SIM_ECAT_HDR_LEN;
    if (pos + ecat_len > size) {
        return -EINVAL;
    }

    do {
        uint8_t cmd, *addr, *data;
        uint16_t lenflags, dlen;

        if (pos + SIM_DGRAM_HDR_LEN + 2 > size) {
            return -EINVAL;
        }
        cmd = frame[pos];
        addr = frame + pos + 2;
        lenflags = sim_rd16(frame + pos + 6);
        dlen = lenflags & 0x07FF;
        more = lenflags & 0x8000;
        data = frame + pos + SIM_DGRAM_HDR_LEN;

        if (pos + SIM_DGRAM_HDR_LEN + dlen + 2 > size) {
            return -EINVAL;
        }

        sim_bus_process_datagram(bus, cmd, addr, data, dlen,
                data + dlen /* working counter */);

        pos += SIM_DGRAM_HDR_LEN + dlen + 2;
    } while (more);

    return 0;
}

/****************************************************************************/
/* Transport operations                                                     */
/****************************************************************************/

static int sim_open(ec_transport_t *transport, const char *interface)
{
    (void) transport;
    (void) interface;
    return 0;
}

static void sim_close(ec_transport_t *transport)
{
    (void) transport;
}

static uint8_t *sim_get_tx_buffer(ec_transport_t *transport)
{
    return transport->tx_buffer;
}

static int sim_send(ec_transport_t *transport, size_t size)
{
    sim_bus_t *bus = transport->priv;

    if (size > sizeof(transport->tx_buffer)) {
        return -EINVAL;
    }

    pthread_mutex_lock(&bus->lock);

    if (!bus->link_up) { /* link down: frame is lost */
        pthread_mutex_unlock(&bus->lock);
        return 0;
    }

    bus->frame_count++;

    if (sim_bus_process_frame(bus, transport->tx_buffer, size) == 0) {
        /* Queue the processed frame for the next receive() (drop the
         * oldest frame on overflow, like a full RX ring would). */
        unsigned int next = (bus->rxq_head + 1) % SIM_RXQ_LEN;
        if (next == bus->rxq_tail) {
            bus->rxq_tail = (bus->rxq_tail + 1) % SIM_RXQ_LEN;
        }
        memcpy(bus->rxq[bus->rxq_head].data, transport->tx_buffer, size);
        bus->rxq[bus->rxq_head].len = size;
        bus->rxq_head = next;
    }

    pthread_mutex_unlock(&bus->lock);
    return 0;
}

static int sim_receive(ec_transport_t *transport, uint8_t *buffer,
        size_t max_size)
{
    sim_bus_t *bus = transport->priv;
    size_t len = 0;

    pthread_mutex_lock(&bus->lock);
    if (bus->rxq_tail != bus->rxq_head) {
        len = bus->rxq[bus->rxq_tail].len;
        if (len <= max_size) {
            memcpy(buffer, bus->rxq[bus->rxq_tail].data, len);
        } else {
            len = 0; /* drop oversized frame */
        }
        bus->rxq_tail = (bus->rxq_tail + 1) % SIM_RXQ_LEN;
    }
    pthread_mutex_unlock(&bus->lock);

    return (int) len;
}

static int sim_get_link_state(ec_transport_t *transport)
{
    sim_bus_t *bus = transport->priv;
    int up;

    pthread_mutex_lock(&bus->lock);
    up = bus->link_up;
    pthread_mutex_unlock(&bus->lock);
    return up;
}

static int sim_get_mac(ec_transport_t *transport, uint8_t mac[6])
{
    static const uint8_t sim_mac[6] = { 0x02, 0x53, 0x49, 0x4D, 0x00, 0x01 };

    (void) transport;
    memcpy(mac, sim_mac, 6);
    return 0;
}

static int sim_get_fd(ec_transport_t *transport)
{
    (void) transport;
    return -1;
}

static const ec_transport_ops_t sim_transport_ops = {
    .name = "sim",
    .open = sim_open,
    .close = sim_close,
    .get_tx_buffer = sim_get_tx_buffer,
    .send = sim_send,
    .receive = sim_receive,
    .get_link_state = sim_get_link_state,
    .get_mac = sim_get_mac,
    .get_fd = sim_get_fd,
    .set_cpu_affinity = NULL,
};

/****************************************************************************/
/* Public API                                                               */
/****************************************************************************/

sim_bus_t *sim_bus_create(unsigned int nslaves,
        const sim_slave_identity_t *identities)
{
    sim_bus_t *bus;
    unsigned int i;

    if (!nslaves || nslaves > SIM_MAX_SLAVES || !identities) {
        return NULL;
    }

    bus = calloc(1, sizeof(*bus));
    if (!bus) {
        return NULL;
    }

    bus->nslaves = nslaves;
    bus->link_up = 1;
    pthread_mutex_init(&bus->lock, NULL);

    for (i = 0; i < nslaves; i++) {
        sim_slave_init_regs(&bus->slaves[i], i, nslaves);
        sim_slave_init_eeprom(&bus->slaves[i], &identities[i]);
    }

    bus->transport.ops = &sim_transport_ops;
    bus->transport.priv = bus;
    snprintf(bus->transport.interface, sizeof(bus->transport.interface),
            "sim0");

    return bus;
}

void sim_bus_destroy(sim_bus_t *bus)
{
    if (bus) {
        pthread_mutex_destroy(&bus->lock);
        free(bus);
    }
}

ec_transport_t *sim_bus_transport(sim_bus_t *bus)
{
    return &bus->transport;
}

void sim_bus_set_link(sim_bus_t *bus, int up)
{
    pthread_mutex_lock(&bus->lock);
    bus->link_up = up;
    pthread_mutex_unlock(&bus->lock);
}

unsigned int sim_bus_slave_al_state(sim_bus_t *bus, unsigned int pos)
{
    unsigned int state = 0;

    if (pos < bus->nslaves) {
        pthread_mutex_lock(&bus->lock);
        state = sim_rd16(bus->slaves[pos].regs + 0x0130) & 0x000F;
        pthread_mutex_unlock(&bus->lock);
    }
    return state;
}

uint8_t *sim_bus_slave_regs(sim_bus_t *bus, unsigned int pos)
{
    return pos < bus->nslaves ? bus->slaves[pos].regs : NULL;
}

unsigned long sim_bus_frame_count(sim_bus_t *bus)
{
    unsigned long n;

    pthread_mutex_lock(&bus->lock);
    n = bus->frame_count;
    pthread_mutex_unlock(&bus->lock);
    return n;
}
