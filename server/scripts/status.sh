#!/usr/bin/env bash
# Show the state of the cell network and services, and where to point the ESP32s.
#
# Usage: server/scripts/status.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

state() { if "$@" >/dev/null 2>&1; then printf '\033[32mup\033[0m'; else printf '\033[31mdown\033[0m'; fi; }

active_con="$(nmcli -t -f GENERAL.CONNECTION device show "$WIFI_IFACE" 2>/dev/null | cut -d: -f2- || true)"
wifi_ip="$(iface_ip "$WIFI_IFACE")"
host_ip="${wifi_ip:-$({ hostname -I 2>/dev/null || true; } | awk '{print $1}')}"

echo
echo "Network"
printf '  %-22s %s\n' "$WIFI_IFACE connection" "${active_con:--}"
printf '  %-22s %s\n' "$WIFI_IFACE address" "${wifi_ip:--}"
if [[ "$active_con" == "$AP_CON" ]]; then
    printf '  %-22s %s\n' "AP SSID / password" "$AP_SSID / $AP_PASSWORD"
    clients="$(iw dev "$WIFI_IFACE" station dump 2>/dev/null | grep -c '^Station' || true)"
    printf '  %-22s %s\n' "AP clients" "${clients:-0}"
fi

echo "Services"
printf '  %-22s %b\n' "alert server" "$(state systemctl is-active --quiet "$SERVICE")"
if [[ "$NTP_SERVER" == yes ]]; then
    printf '  %-22s %b\n' "ntp (chrony)" "$(state systemctl is-active --quiet chrony)"
fi
# The Command Centre is another machine; only check that the alert server can reach its broker.
if [[ "$active_con" == "$AP_CON" && -n "$COMMAND_CENTRE_MAC" ]]; then
    printf '  %-22s %b\n' "command centre" "$(state ping -c1 -W1 "$COMMAND_CENTRE_ADDRESS")"
    printf '  %-22s %b\n' "  broker :$MQTT_PORT" "$(state timeout 2 bash -c "</dev/tcp/$COMMAND_CENTRE_ADDRESS/$MQTT_PORT")"
fi

echo "Endpoints"
if [[ "$active_con" == "$AP_CON" ]]; then
    printf '  %-22s %s\n' "ESP32 server_address" "$AP_DNS_NAME (or $AP_ADDRESS)"
else
    printf '  %-22s %s\n' "ESP32 server_address" "${host_ip:--}"
fi
printf '  %-22s %s\n' "Alert server (UDP)" "${host_ip:--}:$ALERT_SERVER_UDP_PORT"
if [[ "$active_con" == "$AP_CON" ]]; then
    [[ "$NTP_SERVER" == yes ]] && printf '  %-22s %s\n' "NTP server" "$AP_DNS_NAME ($AP_ADDRESS):123"
    [[ -n "$COMMAND_CENTRE_MAC" ]] && printf '  %-22s %s\n' "MQTT broker (CC)" \
        "$COMMAND_CENTRE_DNS_NAME:$MQTT_PORT (= $COMMAND_CENTRE_ADDRESS)"
fi
echo
echo "Logs: journalctl -u $SERVICE -f   |   live dashboard: server/scripts/monitor.sh"
