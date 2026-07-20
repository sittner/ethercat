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
	test_shared

TESTS = $(check_PROGRAMS)

# Link the libtool library statically so tests can reach internal
# ec_* symbols that the shared library's version script hides.
LDADD = $(top_builddir)/master/uspace/libethercat.la
AM_LDFLAGS = -static

test_datagram_SOURCES = test_datagram.c test.h
test_emerg_ring_SOURCES = test_emerg_ring.c test.h
test_pdo_print_SOURCES = test_pdo_print.c test.h
test_shared_SOURCES = test_shared.c test.h

#-----------------------------------------------------------------------------
