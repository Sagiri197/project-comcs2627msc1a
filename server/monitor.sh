#!/usr/bin/env bash
# Live dashboard of the cell: the ESP32 nodes talking to the alert server, the Wi-Fi stations
# on the AP and the readings as they arrive (followed from the alert server's journal).
#
# Usage: server/monitor.sh [-t SECONDS] [-b LINES] [-n ROWS] [--once]      (asks for sudo)
#   -t SECONDS  a node is ACTIVE if it sent a reading in the last SECONDS (default 15)
#   -b LINES    journal lines replayed at start-up to fill in history (default 3000)
#   -n ROWS     cap the readings feed at ROWS lines (default: fill the terminal)
#   --once      print one snapshot and exit instead of the live view
#   Keys in the live view: q quit, c clear the readings feed
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"
shopt -s extglob

usage() { sed -n '2,10s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"; exit "${1:-0}"; }

active_secs=15
backlog=3000
max_rows=0
once=0
while (( $# )); do
    case "$1" in
        -t|--active)
            [[ "${2:-}" =~ ^[0-9]+$ ]] || die "-t needs a number of seconds"
            active_secs="$2"; shift ;;
        -b|--backlog)
            [[ "${2:-}" =~ ^[0-9]+$ ]] || die "-b needs a number of lines"
            backlog="$2"; shift ;;
        -n|--rows)
            [[ "${2:-}" =~ ^[0-9]+$ ]] || die "-n needs a number"
            max_rows="$2"; shift ;;
        -w|--watch)  # old flag: live is now the default
            if [[ "${2:-}" =~ ^[0-9]+$ ]]; then shift; fi ;;
        --once) once=1 ;;
        -h|--help) usage ;;
        *) warn "unknown option '$1'"; usage 1 ;;
    esac
    shift
done

DB=/var/lib/$SERVICE/readings.sqlite3
LEASES=/var/lib/NetworkManager/dnsmasq-$WIFI_IFACE.leases
CC_MAC="$(tr 'A-F-' 'a-f:' <<< "$COMMAND_CENTRE_MAC")"

if [[ -t 1 ]]; then
    B=$'\e[1m' D=$'\e[2m' R=$'\e[31m' G=$'\e[32m' Y=$'\e[33m' C=$'\e[36m' H=$'\e[1;34m' N=$'\e[0m'
else
    B= D= R= G= Y= C= H= N=
fi

# ---- state ------------------------------------------------------------------------------
# Per node, keyed by node id.
declare -A N_SEEN=() N_IP=() N_PORT=() N_SESSION=() N_SEQ=() N_COUNT=() N_LOSS=()
declare -A N_TEMP=() N_HUM=() N_ALERT=()
FEED=()     # "ts|node|seq|temp|hum|flags", oldest first
EVENTS=()   # "ts|level|text", oldest first
FEED_MAX=500 EVENTS_MAX=50
# Reading block currently being parsed from the log.
cur_node= cur_session= cur_seq= cur_temp= cur_hum= in_block=0
# Network / service state, refreshed every few seconds.
declare -A STA=() IP_MAC=() MAC_HOST=() DB_COUNT=()
SVC=? UDP=? NTP= WIFI_CON= DB_TOTAL=

re_journal='^([0-9]+)\.[0-9]+ [^ ]+ ([^:]+): (.*)$'
re_spdlog='^\[[^]]*\] \[[^]]*\] \[([a-z]+)\] (.*)$'
re_recv='^Received from ([0-9.]+):([0-9]+): node=([^ ]+) session=([^ ]+) seq=([0-9]+)'

add_event() {
    EVENTS+=("$1|$2|$3")
    if (( ${#EVENTS[@]} > EVENTS_MAX )); then EVENTS=("${EVENTS[@]:1}"); fi
}

# Tag the newest reading (and its node) with an alert reason.
flag_last() {
    local last=$(( ${#FEED[@]} - 1 )) node
    (( last >= 0 )) || return 0
    FEED[last]="${FEED[last]}$1 "
    node="$(cut -d'|' -f2 <<< "${FEED[last]}")"
    N_ALERT[$node]="${FEED[last]%%|*}"
}

commit_reading() {
    local ts=$1 node=$cur_node prev_seq
    [[ -n "$node" ]] || return 0
    FEED+=("$ts|$node|$cur_seq|$cur_temp|$cur_hum|")
    if (( ${#FEED[@]} > FEED_MAX )); then FEED=("${FEED[@]:1}"); fi

    prev_seq="${N_SEQ[$node]:-}"
    if [[ -n "${N_SESSION[$node]:-}" && "${N_SESSION[$node]}" != "$cur_session" ]]; then
        add_event "$ts" info "$node started a new session ($cur_session): node rebooted"
    elif [[ -n "$prev_seq" && -n "$cur_seq" ]] && (( cur_seq > prev_seq + 1 )); then
        N_LOSS[$node]=$(( ${N_LOSS[$node]:-0} + cur_seq - prev_seq - 1 ))
    fi
    N_SESSION[$node]=$cur_session
    N_SEQ[$node]=$cur_seq
    N_SEEN[$node]=$ts
    N_COUNT[$node]=$(( ${N_COUNT[$node]:-0} + 1 ))
    N_TEMP[$node]=$cur_temp
    N_HUM[$node]=$cur_hum
}

# One journal line (journalctl -o short-unix).
handle_line() {
    local line=${1//$'\e'\[*([0-9;])m/} ts src text level
    [[ "$line" =~ $re_journal ]] || return 0
    ts=${BASH_REMATCH[1]} src=${BASH_REMATCH[2]} text=${BASH_REMATCH[3]}

    if [[ ! "$text" =~ $re_spdlog ]]; then
        # systemd's own lines about the unit: started, stopped, crashed, restarting.
        if [[ "$src" == systemd* ]]; then add_event "$ts" info "$text"; fi
        return 0
    fi
    level=${BASH_REMATCH[1]} text=${BASH_REMATCH[2]}

    case "$text" in
        *"READING RECEIVED"*)
            in_block=1 cur_node= cur_session= cur_seq= cur_temp= cur_hum= ;;
        "Node: "*)        (( in_block )) && cur_node=${text#Node: } ;;
        "Session: "*)     (( in_block )) && cur_session=${text#Session: } ;;
        "Sequence: "*)    (( in_block )) && cur_seq=${text#Sequence: } ;;
        "Temperature: "*) (( in_block )) && { cur_temp=${text#Temperature: }; cur_temp=${cur_temp%%[!0-9.-]*}; } ;;
        "Humidity: "*)    (( in_block )) && { cur_hum=${text#Humidity: }; cur_hum=${cur_hum%%[!0-9.-]*}; } ;;
        =====*)           if (( in_block )); then commit_reading "$ts"; in_block=0; fi ;;
        *"ALERT"*)        flag_last ALERT ;;
        *"Temperature differential"*EXCEEDS*) flag_last ΔT ;;
        *"Humidity differential"*EXCEEDS*)    flag_last ΔH ;;
        *"Temperature OUT OF RANGE"*)         flag_last T-range ;;
        *"Humidity OUT OF RANGE"*)            flag_last H-range ;;
        *"Previous:"*|*"Current:"*|"Time difference"*|*"differential"*|"First reading"*) ;;
        *)
            if [[ "$text" =~ $re_recv ]]; then
                local node=${BASH_REMATCH[3]}
                N_IP[$node]=${BASH_REMATCH[1]}
                N_PORT[$node]=${BASH_REMATCH[2]}
                if (( ts > ${N_SEEN[$node]:-0} )); then N_SEEN[$node]=$ts; fi
            elif [[ "$level" == warn* || "$level" == err* || "$level" == crit* ]]; then
                add_event "$ts" "$level" "$text"
            elif [[ "$text" == *listening* ]]; then
                add_event "$ts" info "$text"
            fi ;;
    esac
    return 0
}

refresh_net() {
    if systemctl is-active --quiet "$SERVICE" 2>/dev/null; then SVC=up; else SVC=down; fi
    if ss -Hlun "sport = :$ALERT_SERVER_UDP_PORT" 2>/dev/null | grep -q .; then UDP=up; else UDP=down; fi
    NTP=
    if [[ "$NTP_SERVER" == yes ]] && command -v chronyc >/dev/null; then
        if systemctl is-active --quiet chrony 2>/dev/null; then
            NTP="up, $(chronyc -n clients 2>/dev/null | awk 'NR > 2' | grep -c . || true) clients"
        else
            NTP=down
        fi
    fi
    WIFI_CON=
    if command -v nmcli >/dev/null; then
        WIFI_CON="$(nmcli -t -f GENERAL.CONNECTION device show "$WIFI_IFACE" 2>/dev/null | cut -d: -f2- || true)"
    fi

    STA=() IP_MAC=() MAC_HOST=()
    if [[ "$WIFI_CON" == "$AP_CON" ]] && command -v iw >/dev/null; then
        local mac sig up _ ip host
        while read -r mac sig up; do
            [[ -n "$mac" ]] && STA[$mac]="$sig|$up"
        done < <(iw dev "$WIFI_IFACE" station dump 2>/dev/null | awk '
            /^Station/        { mac = $2; sig[mac] = "-"; up[mac] = 0 }
            /signal:/         { sig[mac] = $2 }
            /connected time:/ { up[mac] = $3 }
            END { for (m in sig) print m, sig[m], up[m] }')
        if [[ -r "$LEASES" ]]; then
            while read -r _ mac ip host _; do
                [[ -n "${ip:-}" ]] || continue
                IP_MAC[$ip]=$mac
                MAC_HOST[$mac]=$host
            done < "$LEASES"
        fi
    fi

    DB_COUNT=() DB_TOTAL=
    if [[ -f "$DB" ]] && command -v sqlite3 >/dev/null; then
        local node count
        DB_TOTAL=0
        while IFS='|' read -r node count; do
            [[ -n "$node" ]] || continue
            DB_COUNT[$node]=$count
            DB_TOTAL=$(( DB_TOTAL + count ))
        done < <(sqlite3 -readonly "$DB" 'SELECT node_id, COUNT(*) FROM readings GROUP BY node_id;' 2>/dev/null || true)
    fi
}

# ---- formatting ---------------------------------------------------------------------------
fmt_age() {
    local s=$1
    if (( s < 60 )); then printf '%ds' "$s"
    elif (( s < 3600 )); then printf '%dm%02ds' $(( s / 60 )) $(( s % 60 ))
    else printf '%dh%02dm' $(( s / 3600 )) $(( s % 3600 / 60 )); fi
}

# The nodes send humidity as a 0-1 fraction; show it as a percentage either way.
fmt_hum() {
    local h=$1
    if [[ "$h" =~ ^0\.([0-9])([0-9])$ ]]; then printf '%s%%' "${BASH_REMATCH[1]#0}${BASH_REMATCH[2]}"
    elif [[ "$h" == 1.00 ]]; then printf '100%%'
    elif [[ -n "$h" ]]; then printf '%.0f%%' "$h" 2>/dev/null || printf '%s' "$h"
    else printf -- '-'; fi
}

# Some Wi-Fi drivers (e.g. the Pi's brcmfmac in AP mode) do not report station signal.
fmt_sig() { if [[ "$1" == -[0-9]* ]]; then printf '%s dBm' "$1"; else printf -- '-'; fi; }

fmt_temp() { if [[ -n "$1" ]]; then printf '%.1f°C' "$1" 2>/dev/null || printf '%s' "$1"; else printf -- '-'; fi; }

updown() { if [[ "$1" == up* ]]; then printf '%s%s%s' "$G" "$1" "$N"; else printf '%s%s%s' "$R" "${1:--}" "$N"; fi; }

draw() {
    local lines=50 cols=120 now out=() node age status ip mac sig active=0 rate=0 i
    if [[ -t 0 ]]; then read -r lines cols < <(stty size 2>/dev/null || echo "50 120"); fi
    printf -v now '%(%s)T' -1

    local -a nodes=()
    if (( ${#N_SEEN[@]} )); then mapfile -t nodes < <(printf '%s\n' "${!N_SEEN[@]}" | sort); fi
    for node in "${nodes[@]}"; do (( now - N_SEEN[$node] <= active_secs )) && active=$(( active + 1 )); done
    for (( i = ${#FEED[@]} - 1; i >= 0; i-- )); do
        (( now - ${FEED[i]%%|*} <= 60 )) || break
        rate=$(( rate + 1 ))
    done

    out+=("${B}COMCS cell monitor${N}  $(hostname)  $(printf '%(%Y-%m-%d %H:%M:%S)T' -1)")
    local wifi="${WIFI_CON:--}"
    [[ "$WIFI_CON" == "$AP_CON" ]] && wifi="AP $AP_SSID, ${#STA[@]} stations"
    out+=("alert server $(updown "$SVC")  udp/$ALERT_SERVER_UDP_PORT $(updown "$UDP")  ${NTP:+ntp $(updown "$NTP")  }wifi ${C}$wifi${N}  stored ${B}${DB_TOTAL:--}${N}  rate ${B}$rate${N}/min")

    # -- connections
    out+=("" "${H}Nodes${N}  ${G}$active active${N} / ${#nodes[@]} seen  ${D}(active = reading in the last ${active_secs}s)${N}")
    out+=("$(printf "  ${B}%-14s %-21s %-12s %7s %6s %6s %5s %8s %5s %8s %-8s %s${N}" \
        NODE ADDRESS SESSION SEQ RX STORED LOSS TEMP HUM SIGNAL LAST STATUS)")
    local -A shown_mac=()
    for node in "${nodes[@]}"; do
        age=$(( now - N_SEEN[$node] ))
        if (( age <= active_secs )); then status="${G}● ACTIVE${N}"
        elif (( age <= 4 * active_secs )); then status="${Y}● STALE${N}"
        else status="${R}○ LOST${N}"; fi
        if [[ -n "${N_ALERT[$node]:-}" ]] && (( now - N_ALERT[$node] <= 60 )); then status+="  ${R}${B}ALERT${N}"; fi
        ip=${N_IP[$node]:-} mac=${IP_MAC[$ip]:-} sig=-
        if [[ -n "$mac" && -n "${STA[$mac]:-}" ]]; then sig="$(fmt_sig "${STA[$mac]%%|*}")"; shown_mac[$mac]=1; fi
        out+=("$(printf '  %-14s %-21s %-12s %7s %6s %6s %5s %8s %5s %8s %-8s %s' \
            "$node" "${ip:+$ip:${N_PORT[$node]}}" "${N_SESSION[$node]:0:12}" "${N_SEQ[$node]:--}" \
            "${N_COUNT[$node]:-0}" "${DB_COUNT[$node]:--}" "${N_LOSS[$node]:-0}" \
            "$(fmt_temp "${N_TEMP[$node]:-}")" "$(fmt_hum "${N_HUM[$node]:-}")" "$sig" \
            "$(fmt_age "$age")" "$status")")
    done
    (( ${#nodes[@]} )) || out+=("  ${D}no readings in the journal yet${N}")
    # Wi-Fi stations that are not (yet) sending readings, e.g. the Command Centre.
    for mac in "${!STA[@]}"; do
        [[ -n "${shown_mac[$mac]:-}" ]] && continue
        ip=-
        for i in "${!IP_MAC[@]}"; do [[ "${IP_MAC[$i]}" == "$mac" ]] && ip=$i; done
        local name="${MAC_HOST[$mac]:-}"; [[ "$name" == "*" ]] && name=
        [[ "$mac" == "$CC_MAC" ]] && name=command-centre
        out+=("$(printf "  ${D}%-14s %-21s %-12s %7s %6s %6s %5s %8s %5s %8s %-8s %s${N}" \
            "${name:-?}" "$ip" "" "" "" "" "" "" "" "$(fmt_sig "${STA[$mac]%%|*}")" \
            "$(fmt_age "${STA[$mac]##*|}")" "wifi only")")
    done

    # -- events
    if (( ${#EVENTS[@]} )); then
        out+=("" "${H}Events${N}")
        local ts level text color
        for (( i = ${#EVENTS[@]} > 4 ? ${#EVENTS[@]} - 4 : 0; i < ${#EVENTS[@]}; i++ )); do
            IFS='|' read -r ts level text <<< "${EVENTS[i]}"
            case "$level" in warn*) color=$Y ;; err*|crit*) color=$R ;; *) color=$D ;; esac
            out+=("  $(printf '%(%H:%M:%S)T' "$ts")  ${color}$text${N}")
        done
    fi

    # -- readings feed: fills the rest of the screen
    local rows=$(( lines - ${#out[@]} - 4 ))
    (( once )) && rows=$(( max_rows > 0 ? max_rows : 15 ))
    (( max_rows > 0 && rows > max_rows )) && rows=$max_rows
    (( rows < 3 )) && rows=3
    out+=("" "${H}Readings${N}  ${D}newest first${N}")
    out+=("$(printf "  ${B}%-8s %-14s %7s %8s %5s  %s${N}" TIME NODE SEQ TEMP HUM FLAGS)")
    local ts fnode seq temp hum flags style shown=0
    for (( i = ${#FEED[@]} - 1; i >= 0 && shown < rows; i--, shown++ )); do
        IFS='|' read -r ts fnode seq temp hum flags <<< "${FEED[i]}"
        style=
        (( now - ts <= 3 )) && style=$B
        [[ -n "$flags" ]] && style+=$R
        out+=("$(printf "  ${style}%(%H:%M:%S)T %-14s %7s %8s %5s  %s${N}" \
            "$ts" "$fnode" "$seq" "$(fmt_temp "$temp")" "$(fmt_hum "$hum")" "$flags")")
    done
    (( ${#FEED[@]} )) || out+=("  ${D}waiting for readings...${N}")

    if (( once )); then
        printf '%s\n' "${out[@]}"
        return
    fi
    # Pad so the footer sits on the last line, then redraw in place (no flicker).
    while (( ${#out[@]} < lines - 1 )); do out+=(""); done
    out=("${out[@]:0:lines-1}")
    out+=("${D}q quit · c clear feed · following journalctl -u $SERVICE${N}")
    local frame= l
    for l in "${out[@]}"; do frame+="$l"$'\e[K\n'; done
    printf '\e[H%s\e[J' "${frame%$'\n'}"
}

# ---- main -----------------------------------------------------------------------------------
command -v journalctl >/dev/null || die "journalctl is missing (needs systemd)"

if (( once )); then
    while IFS= read -r line; do handle_line "$line"; done \
        < <(journalctl -u "$SERVICE" -n "$backlog" -o short-unix --no-pager 2>/dev/null || true)
    refresh_net
    draw
    exit 0
fi

[[ -t 1 ]] || die "the live view needs a terminal; use --once to print a snapshot"

exec {jfd}< <(exec journalctl -u "$SERVICE" -f -n "$backlog" -o short-unix --no-pager 2>/dev/null)
jpid=$!

cleanup() {
    kill "$jpid" 2>/dev/null || true
    printf '\e[?7h\e[?25h\e[?1049l'
    stty echo 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 0' INT TERM
printf '\e[?1049h\e[?25l\e[?7l\e[2J'   # alternate screen, hide cursor, clip long lines

last_net=0
while true; do
    # Take everything the journal has for us (the backlog first, then new lines).
    n=0
    while (( n < 5000 )) && IFS= read -r -t 0.05 -u "$jfd" line; do
        handle_line "$line"
        n=$(( n + 1 ))
    done
    if (( jpid )) && ! kill -0 "$jpid" 2>/dev/null; then
        printf -v ts '%(%s)T' -1
        add_event "$ts" error "journalctl stopped; restart the monitor"
        jpid=0
    fi

    printf -v now '%(%s)T' -1
    if (( now - last_net >= 5 )); then refresh_net; last_net=$now; fi
    draw

    if IFS= read -rsn1 -t 0.5 key; then
        case "$key" in
            q|Q) exit 0 ;;
            c|C) FEED=() ;;
        esac
    fi
done
