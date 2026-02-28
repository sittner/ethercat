/******************************************************************************
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
 *****************************************************************************/

/**
 * \file
 * MACB/GEM (Cadence GEM IP) direct register access transport for userspace.
 *
 * Provides minimal-latency EtherCAT communication by bypassing the Linux
 * network stack entirely. Uses either UIO (/dev/uioN) or /dev/mem for
 * register access, and hugepages (or mlock'd memory) for DMA buffers.
 *
 * The interface parameter passed to open() is interpreted as:
 *   - A path starting with '/' (e.g. "/dev/uio0") — UIO device access
 *   - A hex string (e.g. "fe1c0000") — /dev/mem access at that physical address
 *
 * Interrupt-free operation: all GEM interrupts are disabled on open.
 * Pure polling is used for both TX completion and RX availability.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdint.h>

#include "ectp.h"

/****************************************************************************/

/* MACB/GEM register offsets */
#define GEM_NWCTRL      0x0000  /**< Network Control */
#define GEM_NWCFG       0x0004  /**< Network Configuration */
#define GEM_NWSR        0x0008  /**< Network Status */
#define GEM_TXSTATUS    0x0014  /**< TX Status */
#define GEM_RXQBASE     0x0018  /**< RX Queue Base Address */
#define GEM_TXQBASE     0x001C  /**< TX Queue Base Address */
#define GEM_RXSTATUS    0x0020  /**< RX Status */
#define GEM_ISR         0x0024  /**< Interrupt Status Register (read to clear) */
#define GEM_IDR         0x002C  /**< Interrupt Disable Register */
#define GEM_PHYMNTNC    0x0034  /**< PHY Maintenance */
#define GEM_SA1BOT      0x0088  /**< Specific Address 1 Bottom (MAC low) */
#define GEM_SA1TOP      0x008C  /**< Specific Address 1 Top (MAC high) */
#define GEM_DCFG1       0x0280  /**< Design Config 1 */

/* GEM_NWCTRL bits */
#define GEM_NWCTRL_RXEN         (1U << 2)   /**< Enable receiver */
#define GEM_NWCTRL_TXEN         (1U << 3)   /**< Enable transmitter */
#define GEM_NWCTRL_MDEN         (1U << 4)   /**< Enable management port */
#define GEM_NWCTRL_STARTTX      (1U << 9)   /**< Start transmission */

/* GEM_NWCFG bits */
#define GEM_NWCFG_SPEED         (1U << 0)   /**< 1 = 100 Mbps, 0 = 10 Mbps */
#define GEM_NWCFG_FDEN          (1U << 1)   /**< Full duplex */
#define GEM_NWCFG_FCSREM        (1U << 17)  /**< Strip FCS from received frames */

/* GEM_NWSR bits */
#define GEM_NWSR_MDIO_IDLE      (1U << 2)   /**< PHY management idle */

/* GEM_PHYMNTNC bits */
#define GEM_PHYMNTNC_OP_READ    (0x2U << 28) /**< Read operation */
#define GEM_PHYMNTNC_MUST10     (0x2U << 16) /**< Must be 10 */

/* TX descriptor control word bits */
#define MACB_TX_USED            (1U << 31)  /**< Owned by software when set */
#define MACB_TX_WRAP            (1U << 30)  /**< Last descriptor in ring */
#define MACB_TX_LAST            (1U << 15)  /**< Last buffer in frame */

/* RX descriptor address word bits */
#define MACB_RX_USED            (1U << 0)   /**< Owned by software when set */
#define MACB_RX_WRAP            (1U << 1)   /**< Last descriptor in ring */

/* RX descriptor status word: bits 0-13 hold the frame length */
#define MACB_RX_LEN_MASK        0x3FFFU

/* PHY Basic Status register (MII register 1) - link status bit */
#define MII_BMSR                1
#define MII_BMSR_LSTATUS        (1U << 2)   /**< Link status */

/* DMA configuration */
#define MACB_RING_SIZE          4           /**< Number of descriptors */
#define MACB_BUF_SIZE           2048        /**< Bytes per DMA buffer (>= max frame) */
#define MACB_REG_MAP_SIZE       0x1000      /**< Register space to map (4KB) */
#define MACB_TX_POLL_RETRIES    10000       /**< Max poll iterations for TX completion */
#define MACB_MDIO_POLL_RETRIES  1000        /**< Max poll iterations for MDIO idle */

/****************************************************************************/

/** DMA descriptor (two 32-bit words) */
struct macb_dma_desc {
    uint32_t addr;  /**< Buffer address / RX ownership bits */
    uint32_t ctrl;  /**< Control / status */
};

/****************************************************************************/

/** Private data for MACB/GEM transport */
typedef struct {
    volatile uint32_t *regs;        /**< mmap'd register base */
    struct macb_dma_desc *tx_ring;  /**< TX descriptor ring (virtual) */
    struct macb_dma_desc *rx_ring;  /**< RX descriptor ring (virtual) */
    uint8_t *tx_buffers;            /**< TX DMA buffers (virtual) */
    uint8_t *rx_buffers;            /**< RX DMA buffers (virtual) */
    uintptr_t tx_ring_phys;         /**< TX ring physical address */
    uintptr_t rx_ring_phys;         /**< RX ring physical address */
    uintptr_t tx_buf_phys;          /**< TX buffers physical address */
    uintptr_t rx_buf_phys;          /**< RX buffers physical address */
    uint8_t mac_addr[6];            /**< MAC address */
    int mem_fd;                     /**< /dev/mem or /dev/uioN fd */
    unsigned int tx_head;           /**< Next TX descriptor to use */
    unsigned int rx_tail;           /**< Next RX descriptor to check */
    size_t dma_size;                /**< Total DMA allocation size */
    void *dma_base;                 /**< DMA allocation base (virtual) */
    int using_uio;                  /**< 1 = UIO mode, 0 = /dev/mem mode */
} ec_transport_macb_t;

/****************************************************************************/

/**
 * Read a 32-bit register.
 */
static inline uint32_t macb_readl(ec_transport_macb_t *macb, unsigned int off)
{
    return macb->regs[off / 4];
}

/****************************************************************************/

/**
 * Write a 32-bit register.
 */
static inline void macb_writel(ec_transport_macb_t *macb, unsigned int off,
        uint32_t val)
{
    macb->regs[off / 4] = val;
}

/****************************************************************************/

/**
 * Resolve virtual address to physical address via /proc/self/pagemap.
 *
 * @param vaddr Virtual address
 * @return Physical address, or 0 on failure
 */
static uintptr_t virt_to_phys(void *vaddr)
{
    int fd;
    uintptr_t virt;
    uintptr_t page_num;
    uintptr_t page_off;
    uint64_t entry;
    ssize_t nread;
    uintptr_t phys;
    long page_size;

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }

    virt = (uintptr_t)vaddr;
    page_num = virt / (uintptr_t)page_size;
    page_off = virt % (uintptr_t)page_size;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    if (lseek(fd, (off_t)(page_num * 8), SEEK_SET) < 0) {
        close(fd);
        return 0;
    }

    nread = read(fd, &entry, 8);
    close(fd);

    if (nread != 8) {
        return 0;
    }

    /* Bit 63: page present; bits 0-54: PFN */
    if (!(entry & (1ULL << 63))) {
        return 0;
    }

    phys = (uintptr_t)((entry & 0x7FFFFFFFFFFFFFULL) * (uint64_t)page_size);
    phys += page_off;
    return phys;
}

/****************************************************************************/

/**
 * Allocate DMA-capable memory using hugepages (preferred) or regular pages.
 *
 * Hugepages provide physically contiguous memory that is easier to pin.
 * Falls back to regular mmap + mlock if hugepages are unavailable.
 *
 * @param size   Requested allocation size in bytes
 * @param actual Actual allocated size (always >= size)
 * @return Virtual address of allocation, or NULL on failure
 */
static void *alloc_dma_memory(size_t size, size_t *actual)
{
    void *ptr;
    long page_size;

    /* Try 2 MB hugepage first */
#ifdef MAP_HUGETLB
    {
        size_t huge_size = (size + (2 * 1024 * 1024) - 1)
            & ~((size_t)(2 * 1024 * 1024 - 1));
        ptr = mmap(NULL, huge_size,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_LOCKED,
                   -1, 0);
        if (ptr != MAP_FAILED) {
            /* Touch pages to ensure physical allocation */
            memset(ptr, 0, huge_size);
            *actual = huge_size;
            return ptr;
        }
    }
#endif /* MAP_HUGETLB */

    /* Fall back to regular anonymous pages with mlock */
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        page_size = 4096;
    }

    {
        size_t aligned_size = (size + (size_t)page_size - 1)
            & ~((size_t)page_size - 1);
        ptr = mmap(NULL, aligned_size,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
        if (ptr == MAP_FAILED) {
            return NULL;
        }

        /* Touch and lock pages to prevent swapping */
        memset(ptr, 0, aligned_size);
        if (mlock(ptr, aligned_size) < 0) {
            fprintf(stderr,
                    "macb: mlock failed (%s); DMA may be unreliable\n",
                    strerror(errno));
        }

        *actual = aligned_size;
        return ptr;
    }
}

/****************************************************************************/

/**
 * Read MAC address from GEM SA1 registers.
 */
static void macb_read_mac(ec_transport_macb_t *macb)
{
    uint32_t bot = macb_readl(macb, GEM_SA1BOT);
    uint32_t top = macb_readl(macb, GEM_SA1TOP);

    macb->mac_addr[0] = (uint8_t)(bot >>  0);
    macb->mac_addr[1] = (uint8_t)(bot >>  8);
    macb->mac_addr[2] = (uint8_t)(bot >> 16);
    macb->mac_addr[3] = (uint8_t)(bot >> 24);
    macb->mac_addr[4] = (uint8_t)(top >>  0);
    macb->mac_addr[5] = (uint8_t)(top >>  8);
}

/****************************************************************************/

/**
 * Wait for MDIO management port to become idle.
 *
 * @return 0 on success, -ETIMEDOUT if it never went idle
 */
static int macb_mdio_wait_idle(ec_transport_macb_t *macb)
{
    int i;

    for (i = 0; i < MACB_MDIO_POLL_RETRIES; i++) {
        if (macb_readl(macb, GEM_NWSR) & GEM_NWSR_MDIO_IDLE) {
            return 0;
        }
    }

    return -ETIMEDOUT;
}

/****************************************************************************/

/**
 * Read a PHY register via MDIO.
 *
 * @param phy_addr PHY address (0-31)
 * @param reg_num  Register number (0-31)
 * @return Register value on success, negative error code on failure
 */
static int macb_mdio_read(ec_transport_macb_t *macb, int phy_addr,
        int reg_num)
{
    uint32_t frame;
    int ret;

    ret = macb_mdio_wait_idle(macb);
    if (ret) {
        return ret;
    }

    frame = GEM_PHYMNTNC_OP_READ
        | GEM_PHYMNTNC_MUST10
        | ((uint32_t)(phy_addr & 0x1F) << 23)
        | ((uint32_t)(reg_num  & 0x1F) << 18);

    macb_writel(macb, GEM_PHYMNTNC, frame);

    ret = macb_mdio_wait_idle(macb);
    if (ret) {
        return ret;
    }

    return (int)(macb_readl(macb, GEM_PHYMNTNC) & 0xFFFFU);
}

/****************************************************************************/

/**
 * Initialize TX descriptor ring.
 */
static void macb_init_tx_ring(ec_transport_macb_t *macb)
{
    unsigned int i;

    for (i = 0; i < MACB_RING_SIZE; i++) {
        /* Software owns all TX descriptors initially */
        macb->tx_ring[i].addr = 0;
        macb->tx_ring[i].ctrl = MACB_TX_USED;
    }
    /* Mark last descriptor as wrap */
    macb->tx_ring[MACB_RING_SIZE - 1].ctrl |= MACB_TX_WRAP;

    macb->tx_head = 0;
}

/****************************************************************************/

/**
 * Initialize RX descriptor ring.
 */
static void macb_init_rx_ring(ec_transport_macb_t *macb)
{
    unsigned int i;

    for (i = 0; i < MACB_RING_SIZE; i++) {
        uintptr_t buf_phys = macb->rx_buf_phys + (uintptr_t)(i * MACB_BUF_SIZE);

        /* Hardware owns all RX descriptors (USED bit = 0) */
        macb->rx_ring[i].addr = (uint32_t)(buf_phys & ~(uint32_t)(MACB_RX_USED | MACB_RX_WRAP));
        macb->rx_ring[i].ctrl = 0;
    }
    /* Mark last descriptor as wrap */
    macb->rx_ring[MACB_RING_SIZE - 1].addr |= MACB_RX_WRAP;

    macb->rx_tail = 0;
}

/****************************************************************************/

/**
 * Open MACB/GEM transport.
 *
 * @param transport Transport instance
 * @param interface UIO device path (e.g. "/dev/uio0") or hex physical address
 * @return 0 on success, negative error code on failure
 */
static int macb_open(ec_transport_t *transport, const char *interface)
{
    ec_transport_macb_t *macb;
    void *reg_ptr;
    uintptr_t phys_addr;
    size_t dma_size_needed;
    size_t dma_actual;
    uint8_t *dma_ptr;
    int ret;

    /* Allocate private data */
    macb = calloc(1, sizeof(ec_transport_macb_t));
    if (!macb) {
        return -ENOMEM;
    }

    macb->mem_fd = -1;
    transport->priv = macb;

    /* Determine access mode from interface string */
    if (interface[0] == '/') {
        /* UIO device path */
        macb->using_uio = 1;
        macb->mem_fd = open(interface, O_RDWR | O_SYNC);
        if (macb->mem_fd < 0) {
            ret = -errno;
            fprintf(stderr, "macb: failed to open UIO device %s: %s\n",
                    interface, strerror(errno));
            goto err_free;
        }

        /* Map UIO registers (offset 0 = first memory region) */
        reg_ptr = mmap(NULL, MACB_REG_MAP_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       macb->mem_fd, 0);
        if (reg_ptr == MAP_FAILED) {
            ret = -errno;
            fprintf(stderr, "macb: failed to mmap UIO registers: %s\n",
                    strerror(errno));
            goto err_close;
        }
    } else {
        /* Parse as hex physical address, use /dev/mem */
        char *endptr;
        macb->using_uio = 0;

        phys_addr = (uintptr_t)strtoull(interface, &endptr, 16);
        if (*endptr != '\0' || phys_addr == 0) {
            ret = -EINVAL;
            fprintf(stderr,
                    "macb: invalid interface '%s'; expected /dev/uioN or hex "
                    "physical address\n", interface);
            goto err_free;
        }

        macb->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (macb->mem_fd < 0) {
            ret = -errno;
            fprintf(stderr, "macb: failed to open /dev/mem: %s\n",
                    strerror(errno));
            goto err_free;
        }

        reg_ptr = mmap(NULL, MACB_REG_MAP_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_SHARED,
                       macb->mem_fd, (off_t)phys_addr);
        if (reg_ptr == MAP_FAILED) {
            ret = -errno;
            fprintf(stderr,
                    "macb: failed to mmap registers at 0x%lx: %s\n",
                    (unsigned long)phys_addr, strerror(errno));
            goto err_close;
        }
    }

    macb->regs = (volatile uint32_t *)reg_ptr;

    /* Sanity-check: GEM_DCFG1 should be non-zero on real GEM hardware */
    if (macb_readl(macb, GEM_DCFG1) == 0) {
        fprintf(stderr,
                "macb: GEM_DCFG1 reads as zero; hardware may not be present "
                "or accessible\n");
        /* Non-fatal: continue, hardware may still work */
    }

    /* Allocate DMA memory for descriptors and buffers */
    dma_size_needed = (size_t)(MACB_RING_SIZE * 2 * sizeof(struct macb_dma_desc))
        + (size_t)(MACB_RING_SIZE * 2 * MACB_BUF_SIZE);

    macb->dma_base = alloc_dma_memory(dma_size_needed, &dma_actual);
    if (!macb->dma_base) {
        ret = -ENOMEM;
        fprintf(stderr, "macb: failed to allocate DMA memory\n");
        goto err_unmap;
    }

    macb->dma_size = dma_actual;

    /* Lay out DMA regions within the allocation */
    dma_ptr = (uint8_t *)macb->dma_base;
    macb->tx_ring   = (struct macb_dma_desc *)(void *)dma_ptr;
    dma_ptr += MACB_RING_SIZE * sizeof(struct macb_dma_desc);

    macb->rx_ring   = (struct macb_dma_desc *)(void *)dma_ptr;
    dma_ptr += MACB_RING_SIZE * sizeof(struct macb_dma_desc);

    macb->tx_buffers = dma_ptr;
    dma_ptr += (size_t)(MACB_RING_SIZE * MACB_BUF_SIZE);

    macb->rx_buffers = dma_ptr;

    /* Resolve physical addresses */
    macb->tx_ring_phys = virt_to_phys(macb->tx_ring);
    macb->rx_ring_phys = virt_to_phys(macb->rx_ring);
    macb->tx_buf_phys  = virt_to_phys(macb->tx_buffers);
    macb->rx_buf_phys  = virt_to_phys(macb->rx_buffers);

    if (!macb->tx_ring_phys || !macb->rx_ring_phys
            || !macb->tx_buf_phys || !macb->rx_buf_phys) {
        ret = -EFAULT;
        fprintf(stderr,
                "macb: failed to resolve DMA buffer physical addresses; "
                "run as root and ensure hugepages/mlock are available\n");
        goto err_dma;
    }

    /* Read MAC address before resetting the controller */
    macb_read_mac(macb);

    /* Reset controller: disable TX and RX */
    macb_writel(macb, GEM_NWCTRL, 0);

    /* Disable all interrupts */
    macb_writel(macb, GEM_IDR, 0xFFFFFFFFU);

    /* Clear status registers */
    macb_readl(macb, GEM_ISR);
    macb_writel(macb, GEM_TXSTATUS, 0xFFFFFFFFU);
    macb_writel(macb, GEM_RXSTATUS, 0xFFFFFFFFU);

    /* Configure: 100 Mbps, full duplex, strip FCS */
    macb_writel(macb, GEM_NWCFG,
                GEM_NWCFG_SPEED | GEM_NWCFG_FDEN | GEM_NWCFG_FCSREM);

    /* Initialize descriptor rings */
    macb_init_tx_ring(macb);
    macb_init_rx_ring(macb);

    /* Program descriptor base addresses (32-bit physical addresses) */
    macb_writel(macb, GEM_TXQBASE, (uint32_t)(macb->tx_ring_phys & 0xFFFFFFFFUL));
    macb_writel(macb, GEM_RXQBASE, (uint32_t)(macb->rx_ring_phys & 0xFFFFFFFFUL));

    /* Enable management port, TX, and RX */
    macb_writel(macb, GEM_NWCTRL,
                GEM_NWCTRL_MDEN | GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN);

    return 0;

err_dma:
    munmap(macb->dma_base, macb->dma_size);
    macb->dma_base = NULL;
err_unmap:
    munmap((void *)(uintptr_t)macb->regs, MACB_REG_MAP_SIZE);
    macb->regs = NULL;
err_close:
    close(macb->mem_fd);
    macb->mem_fd = -1;
err_free:
    free(macb);
    transport->priv = NULL;
    return ret;
}

/****************************************************************************/

/**
 * Close MACB/GEM transport.
 */
static void macb_close(ec_transport_t *transport)
{
    ec_transport_macb_t *macb = transport->priv;

    if (!macb) {
        return;
    }

    /* Disable TX and RX, disable interrupts */
    if (macb->regs) {
        macb_writel(macb, GEM_NWCTRL, 0);
        macb_writel(macb, GEM_IDR, 0xFFFFFFFFU);
        munmap((void *)(uintptr_t)macb->regs, MACB_REG_MAP_SIZE);
        macb->regs = NULL;
    }

    if (macb->dma_base) {
        munmap(macb->dma_base, macb->dma_size);
        macb->dma_base = NULL;
    }

    if (macb->mem_fd >= 0) {
        close(macb->mem_fd);
        macb->mem_fd = -1;
    }

    free(macb);
    transport->priv = NULL;
}

/****************************************************************************/

/**
 * Get TX buffer pointer.
 *
 * Returns pointer to the current TX descriptor's buffer so the caller can
 * write the frame directly.
 */
static uint8_t *macb_get_tx_buffer(ec_transport_t *transport)
{
    ec_transport_macb_t *macb = transport->priv;

    if (!macb) {
        return transport->tx_buffer;
    }

    return macb->tx_buffers + macb->tx_head * MACB_BUF_SIZE;
}

/****************************************************************************/

/**
 * Send a frame.
 *
 * @param transport Transport instance
 * @param size      Frame size in bytes
 * @return 0 on success, negative error code on failure
 */
static int macb_send(ec_transport_t *transport, size_t size)
{
    ec_transport_macb_t *macb = transport->priv;
    struct macb_dma_desc *desc;
    uintptr_t buf_phys;
    uint32_t ctrl;
    int i;

    if (!macb || !macb->regs) {
        return -ENODEV;
    }

    if (size > EC_TRANSPORT_MAX_FRAME_SIZE) {
        return -EINVAL;
    }

    desc = &macb->tx_ring[macb->tx_head];
    buf_phys = macb->tx_buf_phys + (uintptr_t)(macb->tx_head * MACB_BUF_SIZE);

    /* Build control word: length, LAST bit, WRAP bit on last descriptor */
    ctrl = (uint32_t)(size & 0x3FFF) | MACB_TX_LAST;
    if (macb->tx_head == MACB_RING_SIZE - 1) {
        ctrl |= MACB_TX_WRAP;
    }
    /* USED bit clear = HW may transmit */

    desc->addr = (uint32_t)(buf_phys & 0xFFFFFFFFUL);
    desc->ctrl = ctrl;  /* Clears MACB_TX_USED, giving ownership to HW */

    /* Trigger transmission */
    macb_writel(macb, GEM_NWCTRL,
                GEM_NWCTRL_MDEN | GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN
                | GEM_NWCTRL_STARTTX);

    /* Poll for TX completion (HW sets USED bit when done) */
    for (i = 0; i < MACB_TX_POLL_RETRIES; i++) {
        if (desc->ctrl & MACB_TX_USED) {
            break;
        }
    }

    if (!(desc->ctrl & MACB_TX_USED)) {
        fprintf(stderr, "macb: TX timeout on descriptor %u\n", macb->tx_head);
        /* Reset USED so ring is not stuck */
        desc->ctrl |= MACB_TX_USED;
    }

    /* Advance head */
    macb->tx_head = (macb->tx_head + 1) % MACB_RING_SIZE;

    return 0;
}

/****************************************************************************/

/**
 * Receive a frame (non-blocking).
 *
 * @param transport Transport instance
 * @param buffer    Buffer to copy the received frame into
 * @param max_size  Maximum buffer size
 * @return Number of bytes received, 0 if no data, negative error code on failure
 */
static int macb_receive(ec_transport_t *transport, uint8_t *buffer,
        size_t max_size)
{
    ec_transport_macb_t *macb = transport->priv;
    struct macb_dma_desc *desc;
    uint32_t addr_word;
    uint32_t status;
    size_t len;

    if (!macb || !macb->regs) {
        return -ENODEV;
    }

    desc = &macb->rx_ring[macb->rx_tail];
    addr_word = desc->addr;

    /* USED bit in addr word: 1 = SW owns (frame ready) */
    if (!(addr_word & MACB_RX_USED)) {
        return 0;  /* No data available */
    }

    status = desc->ctrl;
    len = (size_t)(status & MACB_RX_LEN_MASK);

    if (len > max_size) {
        len = max_size;
    }

    if (len > 0) {
        memcpy(buffer,
               macb->rx_buffers + macb->rx_tail * MACB_BUF_SIZE,
               len);
    }

    /* Return descriptor to hardware: clear USED bit, preserve WRAP */
    desc->addr = (addr_word & ~(uint32_t)MACB_RX_USED);
    desc->ctrl = 0;

    /* Advance tail */
    macb->rx_tail = (macb->rx_tail + 1) % MACB_RING_SIZE;

    return (int)len;
}

/****************************************************************************/

/**
 * Get link state via MDIO PHY read.
 *
 * Reads PHY Basic Status register (MII register 1) from PHY address 0.
 * Falls back to a simple GEM_NWSR check if MDIO fails.
 *
 * @return 1 if link is up, 0 if down, negative error code on failure
 */
static int macb_get_link_state(ec_transport_t *transport)
{
    ec_transport_macb_t *macb = transport->priv;
    int bmsr;

    if (!macb || !macb->regs) {
        return -ENODEV;
    }

    /* Try MDIO read of PHY Basic Status register */
    bmsr = macb_mdio_read(macb, 0, MII_BMSR);
    if (bmsr >= 0) {
        return (bmsr & MII_BMSR_LSTATUS) ? 1 : 0;
    }

    /* MDIO timed out or not supported; use GEM network status as fallback */
    return (macb_readl(macb, GEM_NWSR) & GEM_NWSR_MDIO_IDLE) ? 1 : 0;
}

/****************************************************************************/

/**
 * Get MAC address.
 */
static int macb_get_mac(ec_transport_t *transport, uint8_t mac[6])
{
    ec_transport_macb_t *macb = transport->priv;

    if (!macb) {
        return -ENODEV;
    }

    memcpy(mac, macb->mac_addr, 6);
    return 0;
}

/****************************************************************************/

/**
 * Get file descriptor for polling.
 *
 * The MACB transport uses pure polling; there is no file descriptor to
 * poll. Returns -1.
 */
static int macb_get_fd(ec_transport_t *transport)
{
    (void)transport;
    return -1;
}

/****************************************************************************/

/** MACB/GEM UIO transport operations */
const ec_transport_ops_t ec_transport_macb_uio_ops = {
    .name           = "macb-uio",
    .open           = macb_open,
    .close          = macb_close,
    .get_tx_buffer  = macb_get_tx_buffer,
    .send           = macb_send,
    .receive        = macb_receive,
    .get_link_state = macb_get_link_state,
    .get_mac        = macb_get_mac,
    .get_fd         = macb_get_fd,
};

/****************************************************************************/
