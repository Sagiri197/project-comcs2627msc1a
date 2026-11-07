#!/usr/bin/env bash
# One-time setup of a Raspberry Pi 5 (Raspberry Pi OS Bookworm 64-bit) as the cell's server:
# build tools, Docker, the alert server binary and its systemd unit.
# Re-running it is safe.
#
# Usage: server/install.sh        (asks for sudo; can also be run directly as root)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

# The user who owns the checkout builds the server; root is fine (e.g. a checkout in /root).
user="$(invoking_user)"

[[ "$(uname -m)" == aarch64 ]] || warn "expected a 64-bit OS (aarch64), got $(uname -m)"

log "Installing build tools and network utilities"
apt-get update
apt-get install -y --no-install-recommends \
    git cmake build-essential ca-certificates curl iw rfkill dnsmasq-base mosquitto-clients

if ! command -v docker >/dev/null || ! docker compose version >/dev/null 2>&1; then
    log "Installing Docker Engine + compose plugin"
    curl -fsSL https://get.docker.com | sh
fi
systemctl enable --now docker
if [[ "$user" != root ]] && ! id -nG "$user" | grep -qw docker; then
    usermod -aG docker "$user"
    warn "added $user to the docker group: log out and back in to use docker without sudo"
fi

# Docker publishes 1883 for the broker; a native mosquitto service would hold the port.
if systemctl is-enabled --quiet mosquitto 2>/dev/null; then
    log "Disabling the native mosquitto service (the broker runs in Docker)"
    systemctl disable --now mosquitto
fi

# Node-RED runs as uid 1000 in its container and writes flows.json into the checkout.
chown -R 1000:1000 "$COMPOSE_DIR/nodered"

log "Building the alert server as $user"
if [[ "$user" == root ]]; then
    bash "$PI_DIR/build.sh" --no-install
else
    sudo -u "$user" bash "$PI_DIR/build.sh" --no-install
fi
install -m 0755 "$REPO_DIR/server/build/bin/alert_server" "$SERVER_BIN"

log "Installing the $SERVICE systemd unit"
install -m 0644 "$PI_DIR/$SERVICE.service" "/etc/systemd/system/$SERVICE.service"
systemctl daemon-reload
systemctl enable "$SERVICE"

log "Done. Next: server/up.sh"
