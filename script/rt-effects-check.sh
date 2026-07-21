#!/bin/bash
# rt-effects-check.sh — verify the ECRT_RT_ATTR annotation contract with
# clang's function-effects analysis.
#
# Every ecrt.h function documented rt_safe carries ECRT_RT_ATTR
# (__attribute__((nonblocking)) on clang >= 20).  An application that
# marks its cyclic task function ECRT_RT_ATTR gets a compile-time
# diagnostic when that function calls non-RT-safe API or other
# blocking/allocating code.  This script proves that contract with three
# self-test translation units (tests/rt-effects/):
#
#   rt_ok.c        annotated cyclic function using the whole rt_safe
#                  subset — must compile clean
#   rt_bad.c       annotated cyclic function calling SDO transfers,
#                  config API and malloc — every call must be diagnosed
#   rt_override.c  ECRT_RT_ATTR overridden before including ecrt.h
#                  (the framework hand-over hook) — must compile clean
#
# In addition, the master's IMPLEMENTATION is verified transitively:
# the internal cyclic call tree carries EC_RT_ATTR (globals.h), the
# cyclic transport ops types carry ECRT_RT_ATTR (ectp.h), and the
# deliberate nonblocking-by-flag syscalls (sendto/recvfrom with
# MSG_DONTWAIT, clock_gettime, sched_getcpu, the log ring) are wrapped
# in EC_RT_TRUSTED_BEGIN/END with justification comments — audit them
# with: grep -rn RT_TRUSTED master/ transport/. Each RT translation
# unit below is compiled with the annotated public header injected, so
# ANY blocking call added to the cyclic path fails this check.
#
# Usage: script/rt-effects-check.sh [builddir]
# Needs a configured tree (reads the generated include/ecrt.h from the
# build directory, default: the source tree for in-tree builds).  The
# pinned clang is provided by script/rt-clang.sh (override: RT_CLANG).
#
# Copyright (C) 2026 Sascha Ittner <sascha.ittner@modusoft.de>
# License: GPL Version 2

set -eu

TOP="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$TOP}"

if [ ! -f "$BUILD/include/ecrt.h" ]; then
    echo "rt-effects-check: $BUILD/include/ecrt.h not found" >&2
    echo "rt-effects-check: run configure first (or pass the builddir)" >&2
    exit 1
fi

CLANG="$("$TOP/script/rt-clang.sh")"
echo "rt-effects-check: using $CLANG"

CFLAGS="-std=gnu11 -fsyntax-only -Wfunction-effects -Werror=function-effects \
    -I$BUILD/include -I$TOP/include"

fail=0

# 1. Positive: the full rt_safe subset is callable from an annotated
#    cyclic function.
if "$CLANG" $CFLAGS "$TOP/tests/rt-effects/rt_ok.c"; then
    echo "rt-effects-check: PASS rt_ok.c (rt_safe subset verifies clean)"
else
    echo "rt-effects-check: FAIL rt_ok.c (rt_safe subset did not verify)" >&2
    fail=1
fi

# 2. Negative: non-RT-safe calls from an annotated function must all be
#    diagnosed.  Assert the compile fails AND each expected callee is
#    named in a -Wfunction-effects diagnostic.
BAD_LOG="$(mktemp)"
trap 'rm -f "$BAD_LOG"' EXIT
if "$CLANG" $CFLAGS "$TOP/tests/rt-effects/rt_bad.c" 2>"$BAD_LOG"; then
    echo "rt-effects-check: FAIL rt_bad.c compiled clean (annotations inert?)" >&2
    fail=1
else
    bad_fail=0
    for callee in ecrt_master_sdo_download ecrt_master_sdo_upload \
            ecrt_slave_config_sdo8 malloc; do
        if ! grep -q "non-'nonblocking' function '$callee'" "$BAD_LOG"; then
            echo "rt-effects-check: FAIL rt_bad.c: no diagnostic for $callee" >&2
            bad_fail=1
            fail=1
        fi
    done
    if [ "$bad_fail" -eq 0 ]; then
        echo "rt-effects-check: PASS rt_bad.c (all violations diagnosed)"
    fi
fi

# 3. Override hook: a framework-side ECRT_RT_ATTR definition must win.
if "$CLANG" $CFLAGS "$TOP/tests/rt-effects/rt_override.c"; then
    echo "rt-effects-check: PASS rt_override.c (hand-over hook works)"
else
    echo "rt-effects-check: FAIL rt_override.c (override did not win)" >&2
    fail=1
fi

# 4. Implementation verification: compile the RT translation units with
#    the annotated public header injected. The analysis then verifies
#    the definitions of every rt_safe function and everything they call.
IMPL_CFLAGS="-std=gnu11 -fsyntax-only -Wfunction-effects \
    -Werror=function-effects \
    -DEC_USPACE_MASTER -D_GNU_SOURCE -DHAVE_CONFIG_H \
    -I$BUILD -I$TOP/master/uspace -I$TOP/master \
    -I$BUILD/include -I$TOP/include \
    -include $BUILD/include/ecrt.h"

IMPL_TUS="master/master.c master/domain.c master/device.c \
    master/datagram.c master/utils.c \
    master/uspace/device_uspace.c master/uspace/pal.c \
    transport/transport.c transport/transport_raw.c \
    transport/transport_ccat.c transport/transport_sim.c"

for tu in $IMPL_TUS; do
    if "$CLANG" $IMPL_CFLAGS "$TOP/$tu"; then
        echo "rt-effects-check: PASS $tu"
    else
        echo "rt-effects-check: FAIL $tu (blocking call in the RT path?)" >&2
        fail=1
    fi
done

# The XDP transport needs the libxdp headers; skip with a note if absent.
if echo '#include <xdp/xsk.h>' | "$CLANG" -fsyntax-only -x c - 2>/dev/null; then
    if "$CLANG" $IMPL_CFLAGS -DHAVE_XDP "$TOP/transport/transport_xdp.c"; then
        echo "rt-effects-check: PASS transport/transport_xdp.c"
    else
        echo "rt-effects-check: FAIL transport/transport_xdp.c" >&2
        fail=1
    fi
else
    echo "rt-effects-check: SKIP transport/transport_xdp.c (no libxdp headers)"
fi

if [ "$fail" -ne 0 ]; then
    echo "rt-effects-check: FAILED" >&2
    exit 1
fi
echo "rt-effects-check: OK"
