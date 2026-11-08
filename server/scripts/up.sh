#!/usr/bin/env bash
# Bring the Pi's part of the cell up: network (Wi-Fi AP, DHCP/DNS, NTP) and the alert server.
# Everything is set to start again on boot, so this only needs re-running after changes.
#
# Usage: server/scripts/up.sh [ap|client|none]      (asks for sudo; defaults to WIFI_MODE from pi.env)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

[[ -x "$SERVER_BIN" ]] || die "$SERVER_BIN is missing. Run server/scripts/install.sh first."

bash "$SCRIPTS_DIR/network/up.sh" "$@"

log "Starting $SERVICE"
systemctl restart "$SERVICE"

bash "$SCRIPTS_DIR/status.sh"
