#!/usr/bin/env bash
# Shared helpers and settings for the Linux access point. Sourced by access-point.sh and
# set-time.sh, not run directly.
set -euo pipefail

AP_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # access-point/linux
AP_DIR="$(cd "$AP_SCRIPTS_DIR/.." && pwd)"                        # access-point (ap.env)

# NetworkManager profile created by access-point.sh while it runs.
AP_CON=comcs-ap

# shellcheck source=../ap.env
source "$AP_DIR/ap.env"
if [[ -f "$AP_DIR/ap.env.local" ]]; then
    # shellcheck disable=SC1091
    source "$AP_DIR/ap.env.local"
fi

# DNS names served to the AP clients: one "hostname ipAddress" per line.
DNS_FILE="$AP_DIR/dns.txt"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# Re-run the calling script with sudo when not already root.
require_root() {
    if [[ $EUID -ne 0 ]]; then
        exec sudo bash "$0" "$@"
    fi
}
