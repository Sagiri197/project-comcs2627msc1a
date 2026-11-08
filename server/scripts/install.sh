#!/usr/bin/env bash
# One-time setup of a Raspberry Pi 5 (Raspberry Pi OS Bookworm 64-bit) as the cell's server:
# build tools, network utilities, the alert server binary and its systemd unit.
# Re-running it is safe.
#
# Usage: server/scripts/install.sh        (asks for sudo; can also be run directly as root)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

# The user who owns the checkout builds the server; root is fine (e.g. a checkout in /root).
user="$(invoking_user)"

[[ "$(uname -m)" == aarch64 ]] || warn "expected a 64-bit OS (aarch64), got $(uname -m)"

log "Installing build tools and network utilities"
apt-get update
apt-get install -y --no-install-recommends \
    git cmake build-essential ca-certificates curl iw rfkill dnsmasq-base mosquitto-clients sqlite3 chrony

log "Building the alert server as $user"
if [[ "$user" == root ]]; then
    bash "$SCRIPTS_DIR/build.sh" --no-install
else
    sudo -u "$user" bash "$SCRIPTS_DIR/build.sh" --no-install
fi
install -m 0755 "$REPO_DIR/server/build/bin/alert_server" "$SERVER_BIN"

log "Installing the $SERVICE systemd unit"
install -m 0644 "$PI_DIR/$SERVICE.service" "/etc/systemd/system/$SERVICE.service"
systemctl daemon-reload
systemctl enable "$SERVICE"

log "Done. Next: server/scripts/up.sh"
