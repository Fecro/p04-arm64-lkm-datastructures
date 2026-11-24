# Cross-compilation Makefile for Modern C (C23) userspace test suite (aarch64)

CROSS_COMPILE ?= aarch64-linux-gnu-
CC            := $(CROSS_COMPILE)gcc
TARGET        := bin/kds_tester
SRC           := kds_tester.c

CFLAGS        := -std=c23 -O2 -g \
                 -Wall -Wextra -Wpedantic -Werror \
                 -Wshadow -Wconversion -Wformat=2 \
                 -pthread
LDFLAGS       := -static -pthread

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf bin compile_commands.json
