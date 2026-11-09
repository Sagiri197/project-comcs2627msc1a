SHELL := /bin/sh
# Run from this folder (cd nodes) or from the repo root with: make -C nodes <target>

ARDUINO_CLI ?= arduino-cli
FQBN ?= esp32:esp32:esp32s3
FQBN_1 ?= esp32:esp32:esp32
FQBN_2 ?= esp32:esp32:esp32s3
FQBN_3 ?= esp32:esp32:esp32s3
SKETCH ?= sketch
BUILD_ROOT ?= build

NODES ?= 1 2 3
# Node for `make monitor` / `make flash-monitor`, and the baud rate (serial_baud in
# configs/*.h).
NODE ?= 1
BAUD ?= 9600

# Serial ports per platform. PLATFORM is detected and can be forced: make flash PLATFORM=linux
# Windows: OS=Windows_NT (native make), or uname MINGW*/MSYS*/CYGWIN* (Git Bash / MSYS2 make,
# which does not pass OS through).
UNAME_S := $(shell uname -s 2>/dev/null)
ifneq ($(or $(filter Windows_NT,$(OS)),$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S))),)
PLATFORM ?= windows
else
PLATFORM ?= linux
endif

ifeq ($(PLATFORM),windows)
PORT_1 ?= COM3
PORT_2 ?= COM9
PORT_3 ?= COM5
else ifeq ($(PLATFORM),linux)
PORT_1 ?= /dev/ttyUSB0
PORT_2 ?= /dev/ttyACM0
PORT_3 ?= /dev/ttyACM1
else
$(error Unknown PLATFORM '$(PLATFORM)' (expected windows or linux))
endif

# Serial monitor ports. Each one defaults to the node's flashing port; set it when the board
# has a separate USB port for serial output (e.g. ESP32-S3 native USB vs. UART bridge).
MONITOR_PORT_1 ?= $(PORT_1)
MONITOR_PORT_2 ?= $(PORT_2)
MONITOR_PORT_3 ?= $(PORT_3)

.PHONY: help ports prepare compile compile-node flash flash-node monitor flash-monitor clean

help:
	@printf '%s\n' \
		'make flash                         Flash all three nodes' \
		'make flash NODES="1 3"            Flash only nodes 1 and 3' \
		'make compile NODES="2"            Compile only node 2' \
		'make prepare                       Copy dependencies into the sketch for Arduino IDE' \
		'make ports                         List connected boards and the configured ports' \
		'make monitor NODE=2               Serial monitor of node 2 (Ctrl+C to quit)' \
		'make flash-monitor NODE=2         Flash node 2, then open its serial monitor' \
		'make monitor NODE=1 BAUD=115200   Monitor at another baud rate' \
		'make flash NODES="1" PORT_1=COM7  Override a board port' \
		'make flash-monitor NODE=2 PORT_2=COM10 MONITOR_PORT_2=COM11' \
		'                                   Flash on one port, monitor on another' \
		'make flash PLATFORM=linux          Force the Linux (or windows) port defaults' \
		'make flash NODES="2" FQBN_2=esp32:esp32:esp32s3' \
		'                                   Override a node board type' \
		'make clean                         Remove Arduino build artifacts'

ports:
	@printf 'PLATFORM=%s\n' "$(PLATFORM)"
	@printf 'flash:   PORT_1=%s  PORT_2=%s  PORT_3=%s\n' \
		"$(PORT_1)" "$(PORT_2)" "$(PORT_3)"
	@printf 'monitor: MONITOR_PORT_1=%s  MONITOR_PORT_2=%s  MONITOR_PORT_3=%s\n\n' \
		"$(MONITOR_PORT_1)" "$(MONITOR_PORT_2)" "$(MONITOR_PORT_3)"
	$(ARDUINO_CLI) board list

# One node at a time: the monitor is interactive and holds the port until Ctrl+C.
monitor:
	@test -n "$(MONITOR_PORT_$(strip $(NODE)))" || \
		{ printf 'No monitor port configured for node %s\n' "$(strip $(NODE))" >&2; exit 1; }
	$(ARDUINO_CLI) monitor \
		--port "$(MONITOR_PORT_$(strip $(NODE)))" \
		--config "baudrate=$(strip $(BAUD))"

flash-monitor:
	@$(MAKE) --no-print-directory flash-node NODE=$(strip $(NODE))
	@$(MAKE) --no-print-directory monitor NODE=$(strip $(NODE))

compile:
	@set -e; \
	for node in $(NODES); do \
		$(MAKE) --no-print-directory compile-node NODE=$$node; \
	done

prepare:
	mkdir -p "$(SKETCH)/src/configs" "$(SKETCH)/src/common/nlohmann" "$(SKETCH)/src/vendor/GotekLEDC68"
	cp config.h config_types.h "$(SKETCH)/src/"
	cp configs/*.h "$(SKETCH)/src/configs/"
	cp ../common/protocol.h ../common/protocol.cpp "$(SKETCH)/src/common/"
	cp ../common/nlohmann/json.hpp ../common/nlohmann/LICENSE.MIT "$(SKETCH)/src/common/nlohmann/"
	cp vendor/GotekLEDC68/TM1651.h vendor/GotekLEDC68/TM1651.cpp vendor/GotekLEDC68/LICENSE "$(SKETCH)/src/vendor/GotekLEDC68/"

compile-node: prepare
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
