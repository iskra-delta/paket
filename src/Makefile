# PAKET.COM cross build. Invoked at repository root inside xcc-z80-idp.

ROOT ?= .
BUILD_DIR := $(ROOT)/tests/dump/build/cpm
BIN_DIR := $(ROOT)/bin
LIBSQUID_DIR ?= $(ROOT)/../../retro-plastics/libsquid
SQUID_SERVER_DIR ?= $(ROOT)/../../retro-plastics/squid-server
TARGET := $(BIN_DIR)/paket.com
FLOPPY := $(BIN_DIR)/fddb.img
PLATFORM := cpm3
XCC := xcc
XAS := xas
# The supplied hand-written libraries implement xcc 2.3.2's sdcccall(1) ABI.
CFLAGS := --platform $(PLATFORM) -std=c11 -Os --sdcccall 1 \
	-Wall -Wextra \
	-Wpedantic -Werror \
	-DPAKET_CPM \
	-I$(ROOT)/include \
	-I$(LIBSQUID_DIR)/include \
	-I$(SQUID_SERVER_DIR)/include
ASFLAGS := -g

PROJECT_SOURCES := \
	$(ROOT)/src/application.c \
	$(ROOT)/src/client_z80.c \
	$(ROOT)/src/command.c \
	$(ROOT)/src/file.c \
	$(ROOT)/src/main.c \
	$(ROOT)/src/messages.c \
	$(ROOT)/src/output.c \
	$(ROOT)/src/partner_io.c \
	$(ROOT)/src/platform.c \
	$(ROOT)/src/serial_link.c \
	$(ROOT)/src/timebase.c
OBJECTS := \
	$(BUILD_DIR)/application.rel \
	$(BUILD_DIR)/client_z80.rel \
	$(BUILD_DIR)/command.rel \
	$(BUILD_DIR)/file.rel \
	$(BUILD_DIR)/main.rel \
	$(BUILD_DIR)/messages.rel \
	$(BUILD_DIR)/output.rel \
	$(BUILD_DIR)/partner_io.rel \
	$(BUILD_DIR)/platform.rel \
	$(BUILD_DIR)/serial_link.rel \
	$(BUILD_DIR)/serial_isr.rel \
	$(BUILD_DIR)/timebase.rel \
	$(BUILD_DIR)/squid_state.rel \
	$(BUILD_DIR)/squid_core.rel \
	$(BUILD_DIR)/squid_client_core.rel \
	$(BUILD_DIR)/squid_client_internal.rel \
	$(BUILD_DIR)/squid_client_retrovault.rel \
	$(BUILD_DIR)/z80_io.rel
PROJECT_HEADERS := $(wildcard $(ROOT)/include/paket/*.h)
SQUID_HEADERS := $(wildcard $(LIBSQUID_DIR)/include/squid/*.h) \
	$(wildcard $(LIBSQUID_DIR)/lib/squid/z80/*.inc) \
	$(wildcard $(SQUID_SERVER_DIR)/include/squid_client/*.h) \
	$(wildcard $(SQUID_SERVER_DIR)/include/squid_server/*.h)

.PHONY: all

all: $(TARGET) $(FLOPPY)

# xcc does not emit dependency files.  Keep ABI and protocol header changes
# from silently reusing incompatible .rel objects in an incremental build.
$(OBJECTS): $(ROOT)/src/Makefile $(PROJECT_HEADERS) $(SQUID_HEADERS)

$(BUILD_DIR)/application.rel: $(ROOT)/src/application.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/client_z80.rel: $(ROOT)/src/client_z80.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/command.rel: $(ROOT)/src/command.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/file.rel: $(ROOT)/src/file.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/main.rel: $(ROOT)/src/main.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/messages.rel: $(ROOT)/src/messages.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/output.rel: $(ROOT)/src/output.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/partner_io.rel: $(ROOT)/src/partner_io.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/platform.rel: $(ROOT)/src/platform.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/serial_link.rel: $(ROOT)/src/serial_link.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/serial_isr.rel: $(ROOT)/src/serial_isr.s | $(BUILD_DIR)
	$(XAS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/timebase.rel: $(ROOT)/src/timebase.c | $(BUILD_DIR)
	$(XCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/squid_state.rel: $(LIBSQUID_DIR)/lib/squid/z80/state.s | $(BUILD_DIR)
	$(XAS) --mode=sdcc -I$(LIBSQUID_DIR)/lib/squid/z80 \
		-DSQUID_MAX_SOCKETS=1 -o $@ $<

$(BUILD_DIR)/squid_core.rel: $(LIBSQUID_DIR)/lib/squid/z80/core.s | $(BUILD_DIR)
	$(XAS) --mode=sdcc -I$(LIBSQUID_DIR)/lib/squid/z80 \
		-DSQUID_MAX_SOCKETS=1 -o $@ $<

$(BUILD_DIR)/squid_client_core.rel: \
	$(SQUID_SERVER_DIR)/lib/client/z80/client.s | $(BUILD_DIR)
	$(XAS) --mode=sdcc -o $@ $<

$(BUILD_DIR)/squid_client_internal.rel: \
	$(SQUID_SERVER_DIR)/lib/client/z80/internal.s | $(BUILD_DIR)
	$(XAS) --mode=sdcc -o $@ $<

$(BUILD_DIR)/squid_client_retrovault.rel: \
	$(SQUID_SERVER_DIR)/lib/client/z80/retrovault.s | $(BUILD_DIR)
	$(XAS) --mode=sdcc -o $@ $<

$(BUILD_DIR)/z80_io.rel: $(ROOT)/src/z80_io.s | $(BUILD_DIR)
	$(XAS) $(ASFLAGS) -o $@ $<

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(XCC) --platform $(PLATFORM) --oformat=binary \
		-Map=$(BUILD_DIR)/paket.map -o $@ $(OBJECTS)

$(FLOPPY): $(TARGET) | $(BIN_DIR)
	rm -f $@
	cpmdisk create $@ fdd
	cpmdisk add $@ -u 0 $(TARGET)

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@
