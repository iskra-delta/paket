# Hosted PAKET validation with full warnings and sanitizers.

ROOT ?= ..
LIBSQUID_DIR ?= $(ROOT)/../../retro-plastics/libsquid
BUILD_DIR := $(ROOT)/tests/dump/build/host
TEST_BIN := $(BUILD_DIR)/test-paket
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
	-fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS ?= -fsanitize=address,undefined
CPPFLAGS := -I$(ROOT)/include

SOURCES := \
	$(ROOT)/src/command.c \
	$(ROOT)/src/file.c \
	$(ROOT)/src/messages.c \
	$(ROOT)/src/output.c \
	$(ROOT)/src/platform.c \
	$(ROOT)/src/retrovault.c \
	$(ROOT)/src/timebase.c \
	$(ROOT)/src/client.c \
	$(ROOT)/src/application.c \
	$(ROOT)/tests/test_paket.c
OBJECTS := \
	$(BUILD_DIR)/command.o \
	$(BUILD_DIR)/file.o \
	$(BUILD_DIR)/messages.o \
	$(BUILD_DIR)/output.o \
	$(BUILD_DIR)/platform.o \
	$(BUILD_DIR)/retrovault.o \
	$(BUILD_DIR)/timebase.o \
	$(BUILD_DIR)/client.o \
	$(BUILD_DIR)/application.o \
	$(BUILD_DIR)/test_paket.o
PROJECT_HEADERS := $(wildcard $(ROOT)/include/paket/*.h)

$(OBJECTS): $(ROOT)/tests/Makefile $(PROJECT_HEADERS)

.PHONY: test integration-server integration-download

test: $(TEST_BIN)
	ASAN_OPTIONS=detect_leaks=1 $(TEST_BIN)

# Optional real squid-server check. Point SQUID_SERVER_ROOT at a built clone.
integration-server: $(BUILD_DIR)/integration-server
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/integration-server

# Require the live server and vault to complete the entire download stream.
integration-download: $(BUILD_DIR)/integration-server
	PAKET_REQUIRE_DOWNLOAD=1 ASAN_OPTIONS=detect_leaks=1 \
		$(BUILD_DIR)/integration-server

$(BUILD_DIR)/integration-server: \
	$(BUILD_DIR)/command.o \
	$(BUILD_DIR)/messages.o \
	$(BUILD_DIR)/output.o \
	$(BUILD_DIR)/retrovault.o \
	$(BUILD_DIR)/client.o \
	$(BUILD_DIR)/integration_server.o
	@test -n "$(SQUID_SERVER_ROOT)" || \
		(echo "SQUID_SERVER_ROOT must name a built squid-server clone"; false)
	$(CC) $(LDFLAGS) -o $@ $^ \
		$(LIBSQUID_DIR)/bin/pc/lib/libsquid.a

$(TEST_BIN): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/command.o: $(ROOT)/src/command.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/file.o: $(ROOT)/src/file.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/messages.o: $(ROOT)/src/messages.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/output.o: $(ROOT)/src/output.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/platform.o: $(ROOT)/src/platform.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/retrovault.o: $(ROOT)/src/retrovault.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/timebase.o: $(ROOT)/src/timebase.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/client.o: $(ROOT)/src/client.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/application.o: $(ROOT)/src/application.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_paket.o: $(ROOT)/tests/test_paket.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/integration_server.o: \
	$(ROOT)/tests/integration_server.c | $(BUILD_DIR)
	@test -n "$(SQUID_SERVER_ROOT)" || \
		(echo "SQUID_SERVER_ROOT must name a built squid-server clone"; false)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(LIBSQUID_DIR)/include \
		-c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@
