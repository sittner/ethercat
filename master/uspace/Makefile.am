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

lib_LTLIBRARIES = libethercat.la

libethercat_la_SOURCES = \
	../coe_emerg_ring.c \
	../datagram.c \
	../datagram_pair.c \
	../device.c \
	../domain.c \
	../doxygen.c \
	../eoe_request.c \
	../ethernet.c \
	../flag.c \
	../fmmu_config.c \
	../foe_request.c \
	../fsm_change.c \
	../fsm_coe.c \
	../fsm_eoe.c \
	../fsm_foe.c \
	../fsm_master.c \
	../fsm_pdo.c \
	../fsm_pdo_entry.c \
	../fsm_sii.c \
	../fsm_slave.c \
	../fsm_slave_config.c \
	../fsm_slave_scan.c \
	../fsm_soe.c \
	../mailbox.c \
	../master.c \
	../pdo.c \
	../pdo_entry.c \
	../pdo_list.c \
	../reg_request.c \
	../sdo.c \
	../sdo_entry.c \
	../sdo_request.c \
	../slave.c \
	../slave_config.c \
	../soe_errors.c \
	../soe_request.c \
	../sync.c \
	../sync_config.c \
	../utils.c \
	../voe_handler.c \
	pal.c \
	pal_eoe.c \
	pal_work.c \
	pal_irq_work.c \
	pal_thread.c \
	device_uspace.c \
	module.c \
	cdev.c \
	tool_api.c \
	$(top_srcdir)/lib/shared.c \
	$(top_srcdir)/transport/transport.c \
	$(top_srcdir)/transport/transport_raw.c \
	$(top_srcdir)/transport/transport_ccat.c \
	$(top_srcdir)/transport/irq_pin.c

if HAVE_XDP
libethercat_la_SOURCES += $(top_srcdir)/transport/transport_xdp.c
endif

libethercat_la_CFLAGS = \
	-DEC_USPACE_MASTER \
	-D_GNU_SOURCE \
	-Dethercat_EXPORTS \
	-I$(srcdir) \
	-I$(srcdir)/.. \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include

if HAVE_XDP
libethercat_la_CFLAGS += -DHAVE_XDP
endif

#
# Libtool library versioning (current:revision:age)
# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
#
# - If the library source code has changed at all since the last update, then
#   increment revision ('c:r:a' becomes 'c:r+1:a').
# - If any interfaces have been added, removed, or changed since the last
#   update, increment current, and set revision to 0.
# - If any interfaces have been added since the last public release, then
#   increment age.
# - If any interfaces have been removed or changed since the last public
#   release, then set age to 0.
#
libethercat_la_LDFLAGS = -version-info 0:0:0 \
	-Wl,--version-script=$(srcdir)/libethercat.map \
	-fvisibility=hidden

libethercat_la_DEPENDENCIES = libethercat.map

libethercat_la_LIBADD = -lpthread

if HAVE_XDP
libethercat_la_LIBADD += -lxdp
endif

EXTRA_DIST = libethercat.map

bin_PROGRAMS = ec_master

ec_master_SOURCES = main.c

ec_master_CFLAGS = \
	-DEC_USPACE_MASTER \
	-D_GNU_SOURCE \
	-I$(srcdir) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include

ec_master_LDADD = libethercat.la

#-----------------------------------------------------------------------------
