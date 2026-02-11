CC = gcc
CFLAGS = -Wall -I. -I.. -I../..

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
	../voe_handler.c

# Userspace sources
USPACE_SRCS = master_main.c pal.c

# All sources combined
SRCS = $(MASTER_SRCS) $(USPACE_SRCS)
OBJS = $(SRCS:.c=.o)

TARGET = ec_master

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)
