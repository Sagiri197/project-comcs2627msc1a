#!/usr/bin/env bash
# Stop everything started by up.sh.
#
# Usage: server/down.sh [--keep-network]    (asks for sudo)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

log "Stopping $SERVICE"
systemctl stop "$SERVICE" || true

if [[ "$RUN_COMMAND_CENTRE_ON_PI" == yes ]]; then
    log "Stopping the MQTT broker and Node-RED"
    compose down || true
fi

if [[ "${1:-}" != --keep-network ]]; then
    bash "$PI_DIR/network-down.sh"
fi
