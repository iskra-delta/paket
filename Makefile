# PAKET host validation and Iskra Delta Partner cross build.

IMAGE ?= wischner/xcc-z80-idp:latest
LIBSQUID_DIR ?= $(abspath ../../retro-plastics/libsquid)
SQUID_SERVER_DIR ?= $(abspath ../../retro-plastics/squid-server)

ROOT := $(CURDIR)
BUILD_DIR := $(ROOT)/tests/dump/build
CONTAINER_WORKDIR := /work
DOCKER_RUN := docker run --rm \
	--user "$(shell id -u):$(shell id -g)" \
	-v "$(ROOT):$(CONTAINER_WORKDIR)" \
	-v "$(LIBSQUID_DIR):/deps/libsquid:ro" \
	-v "$(SQUID_SERVER_DIR):/deps/squid-server:ro" \
	-w $(CONTAINER_WORKDIR) \
	$(IMAGE)

.DEFAULT_GOAL := all

.PHONY: all test cpm check-dependencies clean docker-pull

all: test cpm

test:
	$(MAKE) --no-print-directory -f tests/Makefile ROOT=$(ROOT) test

cpm: check-dependencies
	$(DOCKER_RUN) make --no-print-directory -f src/Makefile \
		ROOT=. LIBSQUID_DIR=/deps/libsquid \
		SQUID_SERVER_DIR=/deps/squid-server all

check-dependencies:
	@test -f "$(LIBSQUID_DIR)/include/squid/snet.h" || \
		{ echo "libsquid not found at $(LIBSQUID_DIR)" >&2; exit 1; }
	@test -f "$(SQUID_SERVER_DIR)/include/squid_client/retrovault.h" || \
		{ echo "squid-server not found at $(SQUID_SERVER_DIR)" >&2; exit 1; }

docker-pull:
	docker pull $(IMAGE)

clean:
	rm -rf $(ROOT)/tests/dump/build $(ROOT)/bin
