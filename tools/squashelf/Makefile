# wolfBoot Squash ELF Tool

CC      = gcc
LD      = gcc
CFLAGS  = -Wall -Wextra -Werror
LDFLAGS =
TARGET  = squashelf
CFLAGS_EXTRA = # Allow additional flags to be passed via command line

# option variables
DEBUG_FLAGS     = -g -DDEBUG
OPTIMIZE        = -O2

# Options
ifeq ($(DEBUG),1)
  CFLAGS+=$(DEBUG_FLAGS)
else
  CFLAGS+=$(OPTIMIZE)
endif

.PHONY: clean all debug

all: $(TARGET)

debug: CFLAGS+=$(DEBUG_FLAGS)
debug: all

$(TARGET): $(TARGET).o
	@echo "Building squashelf tool"
	$(CC) -o $@ $< $(LDFLAGS) $(CFLAGS_EXTRA)

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o
