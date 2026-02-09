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

# For now, only build userspace-specific stub files
# The master/*.c files will be migrated in later PRs as they are adapted
# to work with the Platform Abstraction Layer (PAL)
USERSPACE_SOURCES = \
	pal_user.c

libethercat_master_la_SOURCES = \
	$(USERSPACE_SOURCES)

# Include paths for master headers
libethercat_master_la_CFLAGS = \
	-I$(top_srcdir)/master \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/userspace/include \
	-Wall

# Libraries needed for userspace
libethercat_master_la_LIBADD = -lpthread

# Library version info (current:revision:age)
libethercat_master_la_LDFLAGS = \
	-version-info 1:0:0

# Headers to install
include_HEADERS = \
	include/ecrt_user.h

#-----------------------------------------------------------------------------
