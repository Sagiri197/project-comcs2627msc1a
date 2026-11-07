#!/usr/bin/env bash
# Bring the cell up on the Pi: Wi-Fi AP, alert server, and the MQTT broker + Node-RED
# (Docker) only when RUN_COMMAND_CENTRE_ON_PI=yes.
# Everything is set to start again on boot, so this only needs re-running after changes.
#
# Usage: server/up.sh [ap|client|none]      (asks for sudo; defaults to WIFI_MODE from pi.env)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

[[ -x "$SERVER_BIN" ]] || die "$SERVER_BIN is missing. Run server/install.sh first."

bash "$PI_DIR/network-up.sh" "$@"

if [[ "$RUN_COMMAND_CENTRE_ON_PI" == yes ]]; then
    log "Starting the MQTT broker and Node-RED"
    compose up -d --build
else
    log "Command Centre runs on its own machine (RUN_COMMAND_CENTRE_ON_PI=no): not starting Docker"
fi

log "Starting $SERVICE"
systemctl restart "$SERVICE"

bash "$PI_DIR/status.sh"
