#!/usr/bin/env bash
# Shared helpers. Sourced by the other server/*.sh scripts, not run directly.
set -euo pipefail

PI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$PI_DIR/.." && pwd)"
COMPOSE_DIR="$REPO_DIR/command-centre"

AP_CON=comcs-ap
CLIENT_CON=comcs-client
SERVICE=comcs-alert-server
SERVER_BIN=/usr/local/bin/comcs-alert-server

# shellcheck source=pi.env
source "$PI_DIR/pi.env"
if [[ -f "$PI_DIR/pi.env.local" ]]; then
    # shellcheck disable=SC1091
    source "$PI_DIR/pi.env.local"
fi

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# Re-run the calling script with sudo when not already root.
require_root() {
    if [[ $EUID -ne 0 ]]; then
        exec sudo bash "$0" "$@"
    fi
}

# The non-root user who invoked sudo (owns the checkout and builds the server).
invoking_user() {
    if [[ -n "${SUDO_USER:-}" && "$SUDO_USER" != root ]]; then
        echo "$SUDO_USER"
    else
        stat -c %U "$REPO_DIR"
    fi
}

compose() {
    docker compose --project-directory "$COMPOSE_DIR" -f "$COMPOSE_DIR/docker-compose.yml" "$@"
}

# IPv4 address of an interface, empty if none.
iface_ip() {
    { ip -4 -o addr show dev "$1" 2>/dev/null || true; } | awk '{split($4, a, "/"); print a[1]; exit}'
}
