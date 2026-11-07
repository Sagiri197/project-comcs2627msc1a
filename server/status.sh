#!/usr/bin/env bash
# Show the state of the cell network and services, and where to point the ESP32s.
#
# Usage: server/status.sh
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
if [[ "$RUN_COMMAND_CENTRE_ON_PI" == yes ]]; then
    printf '  %-22s %b\n' "mosquitto (docker)" "$(state test "$(docker inspect -f '{{.State.Running}}' comcs-mosquitto 2>/dev/null)" = true)"
    printf '  %-22s %b\n' "node-red (docker)" "$(state test "$(docker inspect -f '{{.State.Running}}' comcs-nodered 2>/dev/null)" = true)"
    cc_host="${host_ip:-<pi>}"
elif [[ "$active_con" == "$AP_CON" && -n "$COMMAND_CENTRE_MAC" ]]; then
    cc_host="$COMMAND_CENTRE_DNS_NAME"
    printf '  %-22s %b\n' "command centre (AP)" "$(state ping -c1 -W1 "$COMMAND_CENTRE_ADDRESS")"
    printf '  %-22s %b\n' "broker port $MQTT_PORT" "$(state timeout 2 bash -c "</dev/tcp/$COMMAND_CENTRE_ADDRESS/$MQTT_PORT")"
else
    cc_host="<command centre IP>"
fi

echo "Endpoints"
if [[ "$active_con" == "$AP_CON" ]]; then
    printf '  %-22s %s\n' "ESP32 server_address" "$AP_DNS_NAME (or $AP_ADDRESS)"
else
    printf '  %-22s %s\n' "ESP32 server_address" "${host_ip:--}"
fi
printf '  %-22s %s\n' "Alert server (UDP)" "${host_ip:--}:$ALERT_SERVER_UDP_PORT"
if [[ "$active_con" == "$AP_CON" && "$NTP_SERVER" == yes ]]; then
    printf '  %-22s %s  [chrony %b]\n' "NTP server" "$AP_DNS_NAME ($AP_ADDRESS):123" \
        "$(state systemctl is-active --quiet chrony)"
fi
if [[ "$cc_host" == "$COMMAND_CENTRE_DNS_NAME" ]]; then
    printf '  %-22s %s\n' "MQTT broker" "$cc_host:$MQTT_PORT (= $COMMAND_CENTRE_ADDRESS)"
else
    printf '  %-22s %s\n' "MQTT broker" "$cc_host:$MQTT_PORT"
fi
printf '  %-22s %s\n' "Node-RED editor" "http://$cc_host:1880"
printf '  %-22s %s\n' "Dashboard" "http://$cc_host:1880/dashboard"
echo
echo "Logs: journalctl -u $SERVICE -f   |   docker compose -f $COMPOSE_DIR/docker-compose.yml logs -f"
