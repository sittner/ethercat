#-----------------------------------------------------------------------------
#
#  Copyright (C) 2006-2026  Florian Pose, Ingenieurgemeinschaft IgH
#
#  This file is part of the IgH EtherCAT Master.
#
#  The IgH EtherCAT Master is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License version 2, as
#  published by the Free Software Foundation.
#
#  The IgH EtherCAT Master is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
#  Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with the IgH EtherCAT Master; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
#-----------------------------------------------------------------------------

# Unit tests for the master core, built in userspace-master mode only
# (the shared core is identical in kernel mode, so these tests cover the
# kernel-mode logic as well). Run with `make check`.

AM_CPPFLAGS = \
	-DEC_USPACE_MASTER \
	-D_GNU_SOURCE \
	-I$(top_srcdir)/master/uspace \
	-I$(top_srcdir)/master \
	-I$(top_srcdir) \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/include

check_PROGRAMS = \
	test_datagram \
	test_emerg_ring \
	test_pdo_print \
	test_shared \
	test_sim_coe \
	test_sim_dc \
	test_sim_domain \
	test_sim_link \
	test_sim_multi \
	test_sim_pdo_assign \
	test_sim_scan \
	test_sim_sdo_info \
	test_tool_ipc

TESTS = $(check_PROGRAMS)

# Link the libtool library statically so tests can reach internal
# ec_* symbols that the shared library's version script hides.
LDADD = $(top_builddir)/master/uspace/libethercat.la
AM_LDFLAGS = -static

test_datagram_SOURCES = test_datagram.c test.h
test_emerg_ring_SOURCES = test_emerg_ring.c test.h
test_pdo_print_SOURCES = test_pdo_print.c test.h
test_shared_SOURCES = test_shared.c test.h
test_sim_coe_SOURCES = test_sim_coe.c transport_sim.c transport_sim.h test.h
test_sim_dc_SOURCES = test_sim_dc.c transport_sim.c transport_sim.h test.h
test_sim_domain_SOURCES = test_sim_domain.c transport_sim.c transport_sim.h \
	test.h
test_sim_link_SOURCES = test_sim_link.c transport_sim.c transport_sim.h \
	test.h
test_sim_multi_SOURCES = test_sim_multi.c transport_sim.c transport_sim.h \
	test.h
test_sim_pdo_assign_SOURCES = test_sim_pdo_assign.c transport_sim.c \
	transport_sim.h test.h
test_sim_scan_SOURCES = test_sim_scan.c transport_sim.c transport_sim.h test.h
test_sim_sdo_info_SOURCES = test_sim_sdo_info.c transport_sim.c \
	transport_sim.h test.h
test_tool_ipc_SOURCES = test_tool_ipc.c transport_sim.c transport_sim.h \
	test.h

# test_tool_ipc runs the real ethercat binary against the IPC socket
# (skips itself if the tool was not built).
AM_TESTS_ENVIRONMENT = EC_TOOL_BIN='$(abs_top_builddir)/tool/ethercat'; \
	export EC_TOOL_BIN;

# Self-test TUs for script/rt-effects-check.sh (syntax-only, driven by
# the script with a pinned clang — not part of make check).
EXTRA_DIST = \
	rt-effects/rt_bad.c \
	rt-effects/rt_ok.c \
	rt-effects/rt_override.c

#-----------------------------------------------------------------------------
