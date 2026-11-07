#!/usr/bin/env bash
# Take the cell network down and remove its NetworkManager profiles, so the Pi goes back to
# its normal Wi-Fi at the next autoconnect.
#
# Usage: sudo server/network-down.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

for con in "$AP_CON" "$CLIENT_CON"; do
    if nmcli connection show "$con" >/dev/null 2>&1; then
        log "Removing $con"
        nmcli connection delete "$con" >/dev/null
    fi
done
rm -f /etc/NetworkManager/dnsmasq-shared.d/comcs.conf
if [[ -f /etc/chrony/conf.d/comcs.conf ]]; then
    log "No longer serving NTP to the cell"
    rm -f /etc/chrony/conf.d/comcs.conf
    systemctl restart chrony 2>/dev/null || true
fi
log "Cell network is down"
