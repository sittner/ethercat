CC = gcc
CFLAGS = -Wall -g -O0 -I. -I.. -I../.. -Itransport -D_GNU_SOURCE -DEC_USPACE_MASTER
LDFLAGS = -lpthread

ENABLE_XDP = 1

# Optional XDP support (set ENABLE_XDP=1 to enable)
ifdef ENABLE_XDP
CFLAGS += -DHAVE_XDP
LDFLAGS += -lxdp -lbpf
TRANSPORT_XDP_SRCS = transport/transport_xdp.c
else
TRANSPORT_XDP_SRCS =
endif

# Transport sources
TRANSPORT_SRCS = \
	transport/transport.c \
	transport/transport_raw.c \
	$(TRANSPORT_XDP_SRCS)

# All .c files from parent /master/ directory
MASTER_SRCS = \
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
	../voe_handler.c

# Userspace sources
USPACE_SRCS = main.c device_uspace.c module.c pal.c pal_eoe.c pal_work.c pal_irq_work.c pal_thread.c $(TRANSPORT_SRCS)

# All sources combined
SRCS = $(MASTER_SRCS) $(USPACE_SRCS)
OBJS = $(SRCS:.c=.o)

TARGET = ec_master

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
