CC = gcc
CFLAGS = -Wall -I. -I..

# All .c files from parent /master/ directory
MASTER_SRCS = $(wildcard ../*.c)

# Userspace sources
USPACE_SRCS = master_main.c

# All sources combined
SRCS = $(MASTER_SRCS) $(USPACE_SRCS)
OBJS = $(SRCS:.c=.o)

TARGET = ec_master

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)
