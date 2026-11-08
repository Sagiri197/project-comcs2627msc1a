#!/usr/bin/env bash
# Stop everything started by up.sh.
#
# Usage: server/scripts/down.sh [--keep-network]    (asks for sudo)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

log "Stopping $SERVICE"
systemctl stop "$SERVICE" || true

if [[ "${1:-}" != --keep-network ]]; then
    bash "$SCRIPTS_DIR/network/down.sh"
fi
