SHELL := /bin/sh

ARDUINO_CLI ?= arduino-cli
FQBN ?= esp32:esp32:esp32s3
FQBN_1 ?= esp32:esp32:esp32
FQBN_2 ?= esp32:esp32:esp32s3
FQBN_3 ?= esp32:esp32:esp32s3
SKETCH ?= nodes/sketch
BUILD_ROOT ?= build/arduino

NODES ?= 1 2 3
PORT_1 ?= /dev/ttyUSB0
PORT_2 ?= /dev/ttyACM0
PORT_3 ?= /dev/ttyACM1

.PHONY: help compile compile-node flash flash-node clean

help:
	@printf '%s\n' \
		'make flash                         Flash all three nodes' \
		'make flash NODES="1 3"            Flash only nodes 1 and 3' \
		'make compile NODES="2"            Compile only node 2' \
		'make flash NODES="1" PORT_1=/dev/ttyACM0' \
		'                                   Override a board port' \
		'make flash NODES="2" FQBN_2=esp32:esp32:esp32s3' \
		'                                   Override a node board type' \
		'make clean                         Remove Arduino build artifacts'

compile:
	@set -e; \
	for node in $(NODES); do \
		$(MAKE) --no-print-directory compile-node NODE=$$node; \
	done

compile-node:
	@test "$(NODE)" = 1 || test "$(NODE)" = 2 || test "$(NODE)" = 3 || \
		{ printf 'Invalid NODE: %s (expected 1, 2, or 3)\n' "$(NODE)" >&2; exit 1; }
	@mkdir -p "$(BUILD_ROOT)/node-$(NODE)"
	$(ARDUINO_CLI) compile \
		--fqbn "$(FQBN_$(NODE))" \
		--build-path "$(BUILD_ROOT)/node-$(NODE)" \
		--build-property "build.extra_flags=-DNODE_PROFILE=$(NODE)" \
		"$(SKETCH)"

flash: 
	@set -e; \
	for node in $(NODES); do \
		$(MAKE) --no-print-directory flash-node NODE=$$node; \
	done

flash-node: compile-node
	@test -n "$(PORT_$(NODE))" || \
		{ printf 'No port configured for node %s\n' "$(NODE)" >&2; exit 1; }
	$(ARDUINO_CLI) upload \
		--fqbn "$(FQBN_$(NODE))" \
		--port "$(PORT_$(NODE))" \
		--input-dir "$(BUILD_ROOT)/node-$(NODE)" \
		"$(SKETCH)"

clean:
	rm -rf "$(BUILD_ROOT)"
