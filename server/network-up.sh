#!/usr/bin/env bash
# Bring up the cell network on the Pi according to WIFI_MODE in pi.env:
#   ap     - Wi-Fi access point on WIFI_IFACE with DHCP + DNS (NetworkManager "shared" mode)
#   client - join CLIENT_SSID
#   none   - do nothing
# Both profiles autoconnect, so the network comes back after a reboot.
#
# Usage: sudo server/network-up.sh [ap|client|none]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

mode="${1:-$WIFI_MODE}"

command -v nmcli >/dev/null || die "nmcli not found. This needs Raspberry Pi OS Bookworm or newer (NetworkManager)."

set_wifi_country() {
    if command -v raspi-config >/dev/null; then
        raspi-config nonint do_wifi_country "$WIFI_COUNTRY" >/dev/null 2>&1 || true
    fi
    iw reg set "$WIFI_COUNTRY" 2>/dev/null || true
    rfkill unblock wifi 2>/dev/null || true
    nmcli radio wifi on
}

up_ap() {
    (( ${#AP_PASSWORD} >= 8 )) || die "AP_PASSWORD must have at least 8 characters"
    set_wifi_country

    # Recreate the profile every time so pi.env edits take effect.
    nmcli connection delete "$CLIENT_CON" >/dev/null 2>&1 || true
    nmcli connection delete "$AP_CON" >/dev/null 2>&1 || true

    log "Creating access point '$AP_SSID' on $WIFI_IFACE ($AP_ADDRESS/$AP_PREFIX, channel $AP_CHANNEL)"
    # pmf disable: the Pi's brcmfmac driver fails WPA2 handshakes with PMF on in AP mode.
    nmcli connection add type wifi ifname "$WIFI_IFACE" con-name "$AP_CON" ssid "$AP_SSID" \
        autoconnect yes connection.autoconnect-priority 100 \
        802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel "$AP_CHANNEL" \
        802-11-wireless-security.key-mgmt wpa-psk 802-11-wireless-security.psk "$AP_PASSWORD" \
        802-11-wireless-security.proto rsn 802-11-wireless-security.pairwise ccmp \
        802-11-wireless-security.group ccmp 802-11-wireless-security.pmf disable \
        ipv4.method shared ipv4.addresses "$AP_ADDRESS/$AP_PREFIX" \
        ipv6.method disabled >/dev/null

    # NetworkManager's dnsmasq (DHCP + DNS for shared mode) reads this directory, so the
    # ESP32s can use AP_DNS_NAME as server_address instead of a hard-coded IP.
    mkdir -p /etc/NetworkManager/dnsmasq-shared.d
    {
        printf 'address=/%s/%s\n' "$AP_DNS_NAME" "$AP_ADDRESS"
        # Fixed lease + DNS name for the Command Centre, so the broker address never changes.
        if [[ -n "$COMMAND_CENTRE_MAC" ]]; then
            printf 'dhcp-host=%s,%s\n' "$COMMAND_CENTRE_MAC" "$COMMAND_CENTRE_ADDRESS"
            printf 'address=/%s/%s\n' "$COMMAND_CENTRE_DNS_NAME" "$COMMAND_CENTRE_ADDRESS"
        fi
    } > /etc/NetworkManager/dnsmasq-shared.d/comcs.conf
    if [[ -n "$COMMAND_CENTRE_MAC" ]]; then
        log "Command Centre ($COMMAND_CENTRE_MAC) will get $COMMAND_CENTRE_ADDRESS ($COMMAND_CENTRE_DNS_NAME)"
    else
        warn "COMMAND_CENTRE_MAC is empty: the Command Centre gets a random DHCP address"
    fi

    nmcli connection up "$AP_CON" >/dev/null
    log "Access point is up. SSID '$AP_SSID', Pi at $AP_ADDRESS ($AP_DNS_NAME)"
    if [[ -n "$(ip -4 route show default 2>/dev/null | grep -v "dev $WIFI_IFACE" || true)" ]]; then
        log "Internet from the Pi's uplink is NAT-shared to AP clients (NTP works on the ESP32s)"
    else
        warn "No uplink (e.g. Ethernet): AP clients have no Internet, so ESP32 NTP sync will fail"
    fi
}

up_client() {
    [[ -n "$CLIENT_SSID" ]] || die "CLIENT_SSID is empty in pi.env"
    set_wifi_country

    nmcli connection delete "$AP_CON" >/dev/null 2>&1 || true
    nmcli connection delete "$CLIENT_CON" >/dev/null 2>&1 || true
    rm -f /etc/NetworkManager/dnsmasq-shared.d/comcs.conf

    log "Joining Wi-Fi '$CLIENT_SSID' on $WIFI_IFACE"
    nmcli device wifi rescan ifname "$WIFI_IFACE" >/dev/null 2>&1 || true
    nmcli connection add type wifi ifname "$WIFI_IFACE" con-name "$CLIENT_CON" ssid "$CLIENT_SSID" \
        autoconnect yes connection.autoconnect-priority 100 \
        802-11-wireless-security.key-mgmt wpa-psk 802-11-wireless-security.psk "$CLIENT_PASSWORD" \
        >/dev/null
    nmcli --wait 30 connection up "$CLIENT_CON" >/dev/null

    local ip
    ip="$(iface_ip "$WIFI_IFACE")"
    log "Connected. The Pi's address is ${ip:-unknown}: set it as server_address in nodes/configs/*.h"
}

case "$mode" in
    ap)     up_ap ;;
    client) up_client ;;
    none)   log "WIFI_MODE=none, leaving the network as it is" ;;
    *)      die "unknown mode '$mode' (expected ap, client or none)" ;;
esac
