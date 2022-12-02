# To be set:
TARGET ?=
SRCS ?=

CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -pedantic
LDFLAGS ?=

OBJS = $(SRCS:%.c=%.o)

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $^ -o $@

.PHONY: clean
clean:
	$(RM) $(OBJS) $(TARGET)
