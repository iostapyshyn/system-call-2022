# To be set:
TARGET ?=
SRCS ?=

CFLAGS ?= -O2 -g -Wall -Wextra -std=gnu11 -pedantic -static
LDFLAGS ?=

OBJS = $(SRCS:%.c=%.o)

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c $(DEPS)
	$(CC) -c $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	$(RM) $(OBJS) $(TARGET)
