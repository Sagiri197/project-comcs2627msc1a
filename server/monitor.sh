#!/usr/bin/env bash
# Live statistics of the cell: the Pi's address, the devices connected to it and the
# readings received by the alert server over UDP.
#
# Usage: server/monitor.sh [-w [SECONDS]] [-n ROWS]      (asks for sudo)
#   -w [SECONDS]  refresh every SECONDS (default 2) until Ctrl+C; without -w, print once
#   -n ROWS       number of latest readings and log lines to show (default 10)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

usage() { sed -n '2,7s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"; exit "${1:-0}"; }

interval=0
rows=10
while (( $# )); do
    case "$1" in
        -w|--watch)
            interval=2
            if [[ "${2:-}" =~ ^[0-9]+$ ]]; then interval="$2"; shift; fi ;;
        -n|--rows)
            [[ "${2:-}" =~ ^[0-9]+$ ]] || die "-n needs a number"
            rows="$2"; shift ;;
        -h|--help) usage ;;
        *) warn "unknown option '$1'"; usage 1 ;;
    esac
    shift
done

DB=/var/lib/$SERVICE/readings.sqlite3
LEASES=/var/lib/NetworkManager/dnsmasq-$WIFI_IFACE.leases
CC_MAC="$(tr 'A-F-' 'a-f:' <<< "$COMMAND_CENTRE_MAC")"

bold()    { printf '\033[1m%s\033[0m\n' "$*"; }
section() { printf '\n\033[1;34m%s\033[0m\n' "$*"; }
updown()  { if "$@" >/dev/null 2>&1; then printf '\033[32mup\033[0m'; else printf '\033[31mdown\033[0m'; fi; }

# Recent "Client <ip>:<port>: node=<id> ..." log lines -> "<ip> <node>", last one wins.
node_by_ip() {
    journalctl -u "$SERVICE" -n 2000 -o cat --no-pager 2>/dev/null \
        | sed -nE 's/.*Client ([0-9.]+):[0-9]+: node=([^ ]+).*/\1 \2/p' \
        | awk '{ node[$1] = $2 } END { for (ip in node) print ip, node[ip] }'
}

show_network() {
    local con ip
    con="$(nmcli -t -f GENERAL.CONNECTION device show "$WIFI_IFACE" 2>/dev/null | cut -d: -f2- || true)"
    ip="$(iface_ip "$WIFI_IFACE")"

    section "Network"
    printf '  %-20s %s\n' "$WIFI_IFACE" "${con:--} ${ip:+($ip)}"
    if [[ "$con" == "$AP_CON" ]]; then
        printf '  %-20s %s\n' "AP SSID" "$AP_SSID  (DNS: $AP_DNS_NAME)"
    fi
    printf '  %-20s %s\n' "all addresses" "$(hostname -I 2>/dev/null || echo -)"
    if command -v chronyc >/dev/null; then
        local ntp
        ntp="$(chronyc -n tracking 2>/dev/null | awk -F': *' '
            /^Reference ID/ { ref = $2 } /^Stratum/ { st = $2 } /^Leap status/ { leap = $2 }
            END { printf "stratum %s, ref %s, %s", st, ref, leap }')"
        printf '  %-20s %b  %s, %s clients\n' "ntp (chrony)" "$(updown systemctl is-active --quiet chrony)" \
            "${ntp:--}" "$(chronyc -n clients 2>/dev/null | awk 'NR > 2' | grep -c . || true)"
    fi
    printf '  %-20s %s\n' "clock" "$(date '+%Y-%m-%d %H:%M:%S %Z')"
    printf '  %-20s %b\n' "alert server""$(updown systemctl is-active --quiet "$SERVICE")  udp/$ALERT_SERVER_UDP_PORT $(updown sh -c "ss -Hlun 'sport = :$ALERT_SERVER_UDP_PORT' | grep -q .")"
}

show_devices() {
    local con
    con="$(nmcli -t -f GENERAL.CONNECTION device show "$WIFI_IFACE" 2>/dev/null | cut -d: -f2- || true)"

    if [[ "$con" == "$AP_CON" ]]; then
        # Wi-Fi stations joined with the DHCP leases (IP, hostname) and the alert server log (node).
        local stations
        stations="$(iw dev "$WIFI_IFACE" station dump 2>/dev/null | awk '
            /^Station/        { mac = $2; sig[mac] = "? dBm"; up[mac] = "-" }
            /signal:/         { sig[mac] = $2 " dBm" }
            /connected time:/ { s = $3; up[mac] = sprintf("%dm%02ds", s / 60, s % 60) }
            END { for (m in sig) print m, sig[m], up[m] }')"
        section "Connected devices ($(grep -c . <<< "$stations" || true) on $AP_SSID)"
        [[ -n "$stations" ]] || { echo "  none"; return; }

        printf '  \033[1m%-18s %-15s %-20s %-14s %-9s %s\033[0m\n' MAC IP HOSTNAME NODE SIGNAL CONNECTED
        # Three inputs, each starting with a marker line so empty ones still count (mawk-safe).
        awk -v cc="$CC_MAC" '
            /^#(leases|nodes|stations)$/ { part = substr($0, 2); next }
            part == "leases" { ip[$2] = $3; host[$2] = ($4 == "*" ? "-" : $4); next }
            part == "nodes"  { node[$1] = $2; next }
            part == "stations" {
                mac = $1; i = (mac in ip) ? ip[mac] : "-"
                n = (i in node) ? node[i] : (mac == cc ? "command-centre" : "-")
                printf "  %-18s %-15s %-20s %-14s %-9s %s\n", mac, i, (mac in host ? host[mac] : "-"), n, $2 " " $3, $4
            }' \
            <(echo "#leases"; cat "$LEASES" 2>/dev/null || true) \
            <(echo "#nodes"; node_by_ip) \
            <(echo "#stations"; echo "$stations")
    else
        # Not the AP: the best we have is the neighbour table.
        section "Neighbours on $WIFI_IFACE (not in AP mode)"
        ip neigh show dev "$WIFI_IFACE" | awk '{ printf "  %-15s %-18s %s\n", $1, $3, $NF }'
    fi
}

show_readings() {
    section "UDP readings"
    if [[ ! -f "$DB" ]]; then
        echo "  no database yet ($DB)"
        return
    fi

    # Per node: count, latest reading and whether it violates the equipment limits
    # (temperature outside [0, 50] C or humidity outside [20, 80] %).
    local q_common="
        WITH r AS (
            SELECT node_id, session_id, seq,
                   json_extract(datagram, '\$.qos') AS qos,
                   json_extract(datagram, '\$.payload.dateObserved') AS t,
                   json_extract(datagram, '\$.payload.temperature') AS temp,
                   json_extract(datagram, '\$.payload.relativeHumidity') AS hum
            FROM readings)"
    sqlite3 -readonly -header -column "$DB" "$q_common,
        ranked AS (
            SELECT *, ROW_NUMBER() OVER (PARTITION BY node_id ORDER BY t DESC, seq DESC) AS rn,
                      COUNT(*) OVER (PARTITION BY node_id) AS total
            FROM r)
        SELECT node_id AS node, total AS readings, seq AS last_seq, qos,
               t AS last_observed, printf('%.1f', temp) AS temp_c,
               printf('%.0f', hum * 100) AS hum_pct,
               CASE WHEN temp NOT BETWEEN 0 AND 50 OR hum NOT BETWEEN 0.2 AND 0.8
                    THEN 'ALERT' ELSE 'ok' END AS state
        FROM ranked WHERE rn = 1 ORDER BY node_id;" \
        | sed 's/^/  /'
    printf '  %s\n' "total: $(sqlite3 -readonly "$DB" 'SELECT COUNT(*) FROM readings;') readings"

    section "Latest $rows readings"
    sqlite3 -readonly -header -column "$DB" "$q_common
        SELECT node_id AS node, seq, qos, t AS observed,
               printf('%.1f', temp) AS temp_c, printf('%.0f', hum * 100) AS hum_pct
        FROM r ORDER BY t DESC, seq DESC LIMIT $rows;" \
        | sed 's/^/  /'

    section "Alert server log (last $rows)"
    journalctl -u "$SERVICE" -n "$rows" -o cat --no-pager 2>/dev/null | sed 's/^/  /' || true
}

render() {
    bold "COMCS cell monitor - $(hostname) - $(date '+%Y-%m-%d %H:%M:%S')"
    show_network
    show_devices
    show_readings
}

command -v sqlite3 >/dev/null || die "sqlite3 is missing: apt install sqlite3 (or re-run server/install.sh)"

if (( interval == 0 )); then
    render
else
    trap 'tput cnorm 2>/dev/null; echo; exit 0' INT TERM
    tput civis 2>/dev/null || true
    while true; do
        frame="$(render 2>&1)"
        clear
        printf '%s\n\n\033[2mRefreshing every %ss - Ctrl+C to quit\033[0m\n' "$frame" "$interval"
        sleep "$interval"
    done
fi
