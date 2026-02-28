# MACB/GEM Userspace Transport

This document describes the `macb-uio` transport plugin for the IgH EtherCAT
userspace master. It provides **direct register-level access** to the
Cadence GEM (MACB) Ethernet controller, bypassing the Linux network stack
entirely for minimal-latency EtherCAT communication.

## Motivation

The Raspberry Pi 5 uses a MACB/GEM NIC (via the RP1 chip). The conventional
`raw` (AF\_PACKET) and `xdp` transports both traverse the Linux network stack
and incur syscall/context-switch overhead. For EtherCAT, latency and
determinism matter more than throughput.

Direct register access eliminates:
- Socket syscalls (`sendto`/`recvfrom`) per cycle
- Kernel network stack traversal
- Context switches
- Interrupt handling (pure polling replaces interrupts)

Expected improvement: ~50–100 µs round-trip (raw socket) → ~10–20 µs or less.

## Architecture

The transport plugs into the existing `ec_transport_ops_t` vtable defined in
`include/ectp.h`. It is registered as `EC_TRANSPORT_MACB_UIO` with the name
`"macb-uio"`.

## Prerequisites

### 1. Unbind the kernel MACB driver

The kernel `macb` driver must not be bound to the NIC while this transport is
active. Find the platform device name first:

```sh
ls /sys/bus/platform/drivers/macb/
# Example output: fe1c0000.ethernet
echo fe1c0000.ethernet > /sys/bus/platform/drivers/macb/unbind
```

### 2a. UIO access (preferred)

Load the generic UIO platform driver and bind it to the device:

```sh
modprobe uio_pdrv_genirq
echo uio_pdrv_genirq > /sys/bus/platform/devices/fe1c0000.ethernet/driver_override
echo fe1c0000.ethernet > /sys/bus/platform/drivers/uio_pdrv_genirq/bind
```

This creates `/dev/uio0` (or similar). Use that path as the interface name.

### 2b. `/dev/mem` access (fallback)

Requires root or `CAP_SYS_RAWIO`. Pass the physical base address of the GEM
registers as a hex string (e.g. `fe1c0000` for the RPi5 GEM).

```sh
# Confirm physical address from device tree
cat /sys/firmware/devicetree/base/axi/pcie@120000/rp1/ethernet@100000/reg \
    | od -An -tx4
```

### 3. Hugepages for DMA buffers (recommended)

The transport allocates DMA buffers using hugepages for physically contiguous,
pinned memory. Set up 2 MB hugepages before starting the master:

```sh
echo 4 > /proc/sys/vm/nr_hugepages
```

If hugepages are unavailable, the transport falls back to `mmap` +
`mlock` on regular pages. This may work on some systems but is not
guaranteed to provide physically contiguous memory.

### 4. Physical address resolution

The transport reads `/proc/self/pagemap` to determine the physical addresses
of DMA buffers. This requires root privileges or `CAP_SYS_ADMIN`, as Linux
restricts pagemap access to prevent information leaks (see
`/proc/sys/kernel/kptr_restrict`). Run the master as root or grant the
appropriate capability.

## Build

Enable the transport at configure time:

```sh
./bootstrap
./configure --enable-uspace-master --disable-kernel --enable-macb-uio
make
```

## Usage

Pass `macb-uio` as the transport name and the UIO device (or hex address) as
the interface:

```c
ec_transport_t *transport = ec_transport_create_by_name("macb-uio", "/dev/uio0");
```

Or using the physical address fallback:

```c
ec_transport_t *transport = ec_transport_create_by_name("macb-uio", "fe1c0000");
```

## Register Map (Cadence GEM)

| Offset | Name       | Description                         |
|--------|------------|-------------------------------------|
| 0x0000 | NWCTRL     | Network Control                     |
| 0x0004 | NWCFG      | Network Configuration               |
| 0x0008 | NWSR       | Network Status                      |
| 0x0014 | TXSTATUS   | TX Status                           |
| 0x0018 | RXQBASE    | RX Queue Base Address               |
| 0x001C | TXQBASE    | TX Queue Base Address               |
| 0x0020 | RXSTATUS   | RX Status                           |
| 0x0024 | ISR        | Interrupt Status (read to clear)    |
| 0x002C | IDR        | Interrupt Disable                   |
| 0x0034 | PHYMNTNC   | PHY Maintenance (MDIO)              |
| 0x0088 | SA1BOT     | Specific Address 1 Bottom (MAC low) |
| 0x008C | SA1TOP     | Specific Address 1 Top (MAC high)   |
| 0x0280 | DCFG1      | Design Config 1 (HW capability ID)  |

## Design Decisions

- **Polling only**: all GEM interrupts are disabled (`IDR = 0xFFFFFFFF`).
- **Ring size 4**: minimal ring; EtherCAT sends one frame at a time.
- **2048-byte buffers**: each DMA slot holds one full Ethernet frame.
- **No scatter-gather, no checksumming, no VLAN, no multiqueue**.
- **Single TX/RX queue** (queue 0).
- **FCS stripped** from received frames (`NWCFG.FCSREM = 1`).
- **100 Mbps, full duplex** by default (standard for EtherCAT).

## Known Limitations

- Requires **exclusive access** to the NIC; the kernel `macb` driver must
  be unbound.
- `/dev/mem` access requires `root` or `CAP_SYS_RAWIO`.
- DMA reliability depends on hugepages or locked memory. Without hugepages
  and on systems with IOMMU enabled, DMA may silently fail.
- 32-bit DMA addresses only. On systems with physical memory above 4 GB,
  ensure DMA buffers land below the 4 GB boundary (hugepages normally do).
- PHY address 0 is assumed for link state detection via MDIO. Adjust the
  `macb_get_link_state()` call if your PHY is at a different address.
- This transport has been designed for the Cadence GEM IP. Other MACB
  variants may have slightly different register layouts.

## Safety

Driving the NIC from userspace bypasses all kernel safety checks. Ensure:
- No other process (including the kernel driver) accesses the NIC
  simultaneously.
- The physical or UIO address is correct; wrong addresses cause
  undefined behavior.
- Cleanup (`macb_close`) always runs; use signal handlers to ensure this on
  abnormal termination.
