#-----------------------------------------------------------------------------
#
#  Copyright (C) 2006-2025  Florian Pose, Ingenieurgemeinschaft IgH
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

# Userspace master library
lib_LTLIBRARIES = libethercat_master.la

# Shared master sources (PAL-compatible)
# These files have been migrated to use the Platform Abstraction Layer
# and can be compiled for both kernel and userspace
MASTER_SOURCES = \
	$(top_srcdir)/master/datagram.c \
	$(top_srcdir)/master/pdo.c \
	$(top_srcdir)/master/pdo_entry.c \
	$(top_srcdir)/master/pdo_list.c \
	$(top_srcdir)/master/sdo.c \
	$(top_srcdir)/master/sdo_entry.c

# Userspace-specific sources
USERSPACE_SOURCES = \
	pal.c

# Transport layer sources
TRANSPORT_SOURCES = \
	transport/transport.c \
	transport/transport_raw.c

libethercat_master_la_SOURCES = \
	$(USERSPACE_SOURCES) \
	$(TRANSPORT_SOURCES) \
	$(MASTER_SOURCES)

# Include paths for master headers
libethercat_master_la_CFLAGS = \
	-I$(srcdir) \
	-I$(top_srcdir)/master \
	-I$(top_srcdir)/include \
	-I$(srcdir)/transport \
	-Wall

# Libraries needed for userspace
libethercat_master_la_LIBADD = -lpthread

# Library version info (current:revision:age)
libethercat_master_la_LDFLAGS = \
	-version-info 1:0:0

# Headers to install
include_HEADERS = \
	ecrt_user.h

# Additional files to distribute
EXTRA_DIST = \
	transport/ec_transport.h

# Userspace master daemon
bin_PROGRAMS = ethercat_master

ethercat_master_SOURCES = ethercat_master.c

ethercat_master_CFLAGS = \
	-I$(top_srcdir)/include \
	-I$(srcdir) \
	-Wall

ethercat_master_LDADD = \
	libethercat_master.la

#-----------------------------------------------------------------------------
