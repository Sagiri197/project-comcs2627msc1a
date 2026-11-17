#!/usr/bin/env bash
# COMCS cell access point for Linux (Raspberry Pi OS Bookworm or newer, NetworkManager).
#
# Runs in the foreground. Starting it creates the cell's Wi-Fi access point with DHCP, DNS
# names and NTP, prints the network details, then logs devices as they connect, get an
# address and leave. Ctrl+C (or SIGTERM) removes the access point, and the Wi-Fi interface
# goes back to its normal connection.
#
# Every new device must be accepted: until then all its traffic is blocked, and after
# ACCEPT_TIMEOUT seconds (ap.env) without an answer it is declined. Commands, typed while
# it runs (n = the device number shown in the log, or its MAC):
#   a [n]   accept (no n: the oldest waiting device)
#   d [n]   decline: kick it; it is refused until this program restarts
#   k n     disconnect a device; if it comes back it is asked about again
#   l       list the devices          h   help
#
# Usage: access-point/linux/access-point.sh      (asks for sudo)
# Settings:  access-point/ap.env (overrides in access-point/ap.env.local)
# DNS names: access-point/dns.txt, "hostname ipAddress" per line, reloaded when it changes
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

for arg in "$@"; do
    case "$arg" in
        -h|--help) sed -n '2,19s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)         die "unknown option '$arg' (see --help)" ;;
    esac
done

# The Pi's address on the AP; clients get 192.168.50.x by DHCP. dns.txt uses this network.
AP_ADDRESS=192.168.50.1
AP_PREFIX=24
NTP_LOCAL_STRATUM=10                # stratum advertised when serving the Pi's own clock

DNSMASQ_CONF=/etc/NetworkManager/dnsmasq-shared.d/comcs.conf
CHRONY_CONF=/etc/chrony/conf.d/comcs.conf
DNS_HOSTS=/run/comcs-dns.hosts      # names from dns.txt, read by dnsmasq (addn-hosts)
LEASES=/var/lib/NetworkManager/dnsmasq-$WIFI_IFACE.leases
POLL_SECS=2
NFT_TABLE=comcs                     # nftables table "inet comcs": blocks devices not accepted yet
ACCEPT_TIMEOUT=${ACCEPT_TIMEOUT:-60}
[[ "$ACCEPT_TIMEOUT" =~ ^[0-9]+$ ]] || die "ACCEPT_TIMEOUT must be a number of seconds (access-point/ap.env)"

if [[ -t 1 ]]; then
    B=$'\e[1m' D=$'\e[2m' R=$'\e[31m' G=$'\e[32m' Y=$'\e[33m' C=$'\e[36m' N=$'\e[0m'
else
    B= D= R= G= Y= C= N=
fi

# ---- checks -----------------------------------------------------------------------------
missing=()
for cmd in nmcli iw ip dnsmasq chronyd nft; do command -v "$cmd" >/dev/null || missing+=("$cmd"); done
if (( ${#missing[@]} )); then
    die "missing: ${missing[*]}. Install them with:" \
        "sudo apt-get install network-manager iw rfkill dnsmasq-base chrony nftables"
fi
(( ${#AP_PASSWORD} >= 8 )) || die "AP_PASSWORD must have at least 8 characters (access-point/ap.env)"
ip link show "$WIFI_IFACE" >/dev/null 2>&1 || die "no interface '$WIFI_IFACE' (WIFI_IFACE in access-point/ap.env)"

# ---- teardown (runs on Ctrl+C, SIGTERM, errors and normal exit) ---------------------------
started=0
cleanup() {
    (( started )) || return 0
    started=0
    trap - INT TERM
    echo
    log "Stopping: removing the access point"
    nmcli connection delete "$AP_CON" >/dev/null 2>&1 || true
    nft delete table inet "$NFT_TABLE" 2>/dev/null || true
    rm -f "$DNSMASQ_CONF" "$DNS_HOSTS" "$DNS_HOSTS.tmp"
    if [[ -f "$CHRONY_CONF" ]]; then
        rm -f "$CHRONY_CONF"
        systemctl restart chrony 2>/dev/null || true
    fi
    log "Access point removed. $WIFI_IFACE is back to its normal Wi-Fi."
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# ---- start ------------------------------------------------------------------------------
# Network address of ADDRESS/PREFIX, e.g. 192.168.50.1/24 -> 192.168.50.0/24.
subnet_of() {
    local a b c d mask n
    IFS=. read -r a b c d <<< "$1"
    n=$(( (a << 24) | (b << 16) | (c << 8) | d ))
    mask=$(( (0xFFFFFFFF << (32 - $2)) & 0xFFFFFFFF ))
    n=$(( n & mask ))
    echo "$(( n >> 24 & 255 )).$(( n >> 16 & 255 )).$(( n >> 8 & 255 )).$(( n & 255 ))/$2"
}

# ---- DNS names (dns.txt) ----------------------------------------------------------------
DNS_ENTRIES=()   # "ip name", as read from dns.txt
DNS_SUM=         # checksum of dns.txt when last read, to notice edits

# Reads DNS_FILE into DNS_ENTRIES. Blank lines and "# comments" are skipped; other lines that
# are not "hostname ipAddress" are reported with the function named by $1 and skipped.
read_dns_file() {
    local report=$1 n=0 line name ip extra
    DNS_ENTRIES=()
    if [[ ! -r "$DNS_FILE" ]]; then
        "$report" "$DNS_FILE not found: no DNS names are served"
        return 0
    fi
    while IFS= read -r line || [[ -n "$line" ]]; do
        n=$(( n + 1 ))
        line=${line//$'\r'/}
        line=${line%%#*}
        name= ip= extra=
        read -r name ip extra <<< "$line" || true
        [[ -n "$name" ]] || continue
        if [[ -n "$extra" || ! "$ip" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
            "$report" "dns.txt line $n ignored (expected 'hostname ipAddress'): $line"
            continue
        fi
        DNS_ENTRIES+=("$ip $name")
    done < "$DNS_FILE"
}

dns_sum() { { cksum < "$DNS_FILE"; } 2>/dev/null || echo missing; }

# Re-reads dns.txt and rewrites DNS_HOSTS (atomically, so dnsmasq never reads half a file).
write_dns_hosts() {
    DNS_SUM="$(dns_sum)"
    read_dns_file "$1"
    {
        echo "# Generated from $DNS_FILE by access-point/linux/access-point.sh; removed when it stops."
        printf '%s\n' "${DNS_ENTRIES[@]}"
    } > "$DNS_HOSTS.tmp"
    mv -f "$DNS_HOSTS.tmp" "$DNS_HOSTS"
}

# SIGHUP makes dnsmasq re-read its addn-hosts files and clear its cache (leases are kept).
reload_dnsmasq() {
    local pid
    pid="$(pgrep -f "dnsmasq.*--listen-address=$AP_ADDRESS" | head -n1 || true)"
    [[ -n "$pid" ]] || pid="$(pgrep -f 'dnsmasq.*dnsmasq-shared\.d' | head -n1 || true)"
    [[ -n "$pid" ]] && kill -HUP "$pid" 2>/dev/null
}

start_access_point() {
    started=1

    if command -v raspi-config >/dev/null; then
        raspi-config nonint do_wifi_country "$WIFI_COUNTRY" >/dev/null 2>&1 || true
    fi
    iw reg set "$WIFI_COUNTRY" 2>/dev/null || true
    rfkill unblock wifi 2>/dev/null || true
    nmcli radio wifi on

    # Leftovers from a run that was killed (kill -9, power loss), or from older versions.
    nmcli connection delete "$AP_CON" >/dev/null 2>&1 || true
    nmcli connection delete comcs-client >/dev/null 2>&1 || true
    nft delete table inet "$NFT_TABLE" 2>/dev/null || true

    # Admission control: WPA2 lets anyone with the password associate, so a new device is
    # held at the IP level instead: everything it sends to this host (DHCP, DNS, NTP, alert
    # server) or through it (Internet, the Command Centre) is dropped until it is accepted,
    # i.e. its MAC is added to the "approved" set. Created before the AP comes up, so no
    # device gets through in between.
    log "Blocking devices on $WIFI_IFACE until they are accepted (nftables table inet $NFT_TABLE)"
    nft -f - <<EOF
table inet $NFT_TABLE {
    set approved { type ether_addr; }
    chain input {
        type filter hook input priority -10; policy accept;
        iifname "$WIFI_IFACE" ether saddr != @approved drop
    }
    chain forward {
        type filter hook forward priority -10; policy accept;
        iifname "$WIFI_IFACE" ether saddr != @approved drop
    }
}
EOF

    log "Creating access point '$AP_SSID' on $WIFI_IFACE"
    # autoconnect no: the AP only exists while this program runs, not after a reboot.
    # pmf disable: the Pi's brcmfmac driver fails WPA2 handshakes with PMF on in AP mode.
    nmcli connection add type wifi ifname "$WIFI_IFACE" con-name "$AP_CON" ssid "$AP_SSID" \
        autoconnect no \
        802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel "$AP_CHANNEL" \
        802-11-wireless-security.key-mgmt wpa-psk 802-11-wireless-security.psk "$AP_PASSWORD" \
        802-11-wireless-security.proto rsn 802-11-wireless-security.pairwise ccmp \
        802-11-wireless-security.group ccmp 802-11-wireless-security.pmf disable \
        ipv4.method shared ipv4.addresses "$AP_ADDRESS/$AP_PREFIX" \
        ipv6.method disabled >/dev/null

    # DNS names come from dns.txt, through a hosts file that dnsmasq re-reads on SIGHUP, so
    # edits apply while the AP runs (config-file lines are only read at start).
    write_dns_hosts warn

    # NetworkManager's dnsmasq (DHCP + DNS in shared mode) reads this directory.
    mkdir -p "$(dirname "$DNSMASQ_CONF")"
    {
        echo "# Written by access-point/linux/access-point.sh; removed when it stops."
        printf 'addn-hosts=%s\n' "$DNS_HOSTS"
        # Advertise the AP as NTP server (DHCP option 42). The public NTP names in dns.txt
        # point at it too, so ESP32 code using the usual defaults syncs without Internet.
        # chrony resolves through the uplink, not this dnsmasq, so there is no loop.
        printf 'dhcp-option=option:ntp-server,%s\n' "$AP_ADDRESS"
    } > "$DNSMASQ_CONF"

    nmcli --wait 30 connection up "$AP_CON" >/dev/null

    # With Internet chrony relays the public pools; without, "local" keeps it serving this
    # machine's own clock instead of refusing.
    mkdir -p "$(dirname "$CHRONY_CONF")"
    cat > "$CHRONY_CONF" <<EOF
# Written by access-point/linux/access-point.sh; removed when it stops.
allow $(subnet_of "$AP_ADDRESS" "$AP_PREFIX")
local stratum $NTP_LOCAL_STRATUM
EOF
    systemctl enable --now chrony >/dev/null 2>&1 || true
    systemctl restart chrony
}

print_banner() {
    local uplink entry
    uplink="$(ip -4 route show default 2>/dev/null | grep -v "dev $WIFI_IFACE" | awk '{print $5; exit}' || true)"
    row() { printf '  %-14s %s\n' "$1" "$2"; }

    echo
    echo "${B}${G}COMCS access point is up${N}   ${D}(Ctrl+C to stop and remove it)${N}"
    echo
    row "Wi-Fi"        "${B}$AP_SSID${N}  password ${B}$AP_PASSWORD${N}  (WPA2, 2.4 GHz, channel $AP_CHANNEL, $WIFI_IFACE)"
    row "This host"    "$AP_ADDRESS/$AP_PREFIX   DHCP for the clients"
    if [[ -n "$uplink" ]]; then
        row "Internet"   "shared from $uplink"
    else
        row "Internet"   "${Y}none${N} (no uplink: clients only reach this network)"
    fi
    row "NTP"          "$AP_ADDRESS udp/123"
    [[ -n "$uplink" ]] || row "" "${Y}serving this host's own clock: check it with 'date' (fix: access-point/linux/set-time.sh)${N}"
    row "DNS"          "${#DNS_ENTRIES[@]} names from access-point/dns.txt ${D}(edit it any time: reloaded automatically)${N}"
    for entry in "${DNS_ENTRIES[@]}"; do
        printf '  %-14s   %-22s %s\n' "" "${entry#* }" "${entry%% *}"
    done
    echo
    echo "  ESP32 config (nodes/sketch/src/configs/*.h):"
    printf '    %-16s "%s"\n' wifi_ssid "$AP_SSID" wifi_password "$AP_PASSWORD"
    printf '    %-16s %s\n' server_address "${D}alert server name from dns.txt, UDP $ALERT_SERVER_UDP_PORT${N}"
    printf '    %-16s %s\n' mqtt_host "${D}broker name from dns.txt, TCP $MQTT_PORT${N}"
    echo
    echo "${B}Devices${N}   ${D}new devices are blocked until accepted (auto-decline after ${ACCEPT_TIMEOUT}s)${N}"
    echo "  ${D}type: a [n] accept   d [n] decline   k n disconnect   l list   h help   (then Enter)${N}"
}

# ---- device log -------------------------------------------------------------------------
declare -A CONNECTED=() IPS=() HOSTS=()

event() { printf '  %s  %b\n' "$(printf '%(%H:%M:%S)T' -1)" "$*"; }

fmt_duration() {
    local s=$1
    if (( s < 60 )); then printf '%ds' "$s"
    elif (( s < 3600 )); then printf '%dm%02ds' $(( s / 60 )) $(( s % 60 ))
    else printf '%dh%02dm' $(( s / 3600 )) $(( s % 3600 / 60 )); fi
}

name_of() {
    local mac=$1 name=${HOSTS[$1]:-}
    [[ "$name" == "*" ]] && name=
    printf '%s%s' "$mac" "${name:+ ($name)}"
}

# ---- admission: accept / decline / disconnect ----------------------------------------------
# Per associated device: STATE pending (blocked, waiting for an answer) or accepted.
declare -A STATE=() PENDING_SINCE=() ID=() DECLINED=() KICKED=() REFUSED_LOGGED=()
NEXT_ID=1           # devices get a short number, kept for the whole run, for the commands

# "[3] aa:bb:cc:00:00:03 (esp32-003)"
label() { printf '[%s] %s' "${ID[$1]:-?}" "$(name_of "$1")"; }

# Event with a coloured, aligned verb: verb_event COLOUR VERB TEXT.
verb_event() { event "$1$(printf '%-15s' "$2")${N} $3"; }

# Deauthenticates a station. The device may reconnect by itself; what happens then depends on
# the caller (declined: refused again; disconnected: asked about again).
kick_station() {
    iw dev "$WIFI_IFACE" station del "$1" 2>/dev/null && return 0
    event "${R}! could not disconnect $(label "$1") ('iw station del' failed); its traffic stays blocked${N}"
    return 1
}

nft_approve() {
    nft add element inet "$NFT_TABLE" approved "{ $1 }" && return 0
    event "${R}! could not unblock $(label "$1") (nft failed); is table inet $NFT_TABLE still there?${N}"
    return 1
}
nft_unapprove() { nft delete element inet "$NFT_TABLE" approved "{ $1 }" 2>/dev/null || true; }

accept_device() {
    local mac=$1
    if [[ "${STATE[$mac]}" == accepted ]]; then
        event "${D}$(label "$mac") is already accepted${N}"
        return 0
    fi
    nft_approve "$mac" || return 0
    STATE[$mac]=accepted
    unset 'PENDING_SINCE[$mac]'
    verb_event "$G" "+ accepted" "$(label "$mac")"
}

# decline_device MAC REASON: kick it and refuse it for the rest of this run.
decline_device() {
    local mac=$1
    DECLINED[$mac]=1
    KICKED[$mac]=$2
    STATE[$mac]=declined
    unset 'PENDING_SINCE[$mac]'
    nft_unapprove "$mac"
    kick_station "$mac" || true
}

# Disconnects an accepted device. If it comes back it is a new request.
disconnect_device() {
    local mac=$1 now
    printf -v now '%(%s)T' -1
    nft_unapprove "$mac"
    STATE[$mac]=pending
    PENDING_SINCE[$mac]=$now
    KICKED[$mac]="disconnected by you"
    kick_station "$mac" || true
}

# A new station: refused if declined earlier in this run, otherwise held until answered.
new_station() {
    local mac=$1 sig=$2 now=$3
    [[ -n "${ID[$mac]:-}" ]] || { ID[$mac]=$NEXT_ID; NEXT_ID=$(( NEXT_ID + 1 )); }

    if [[ -n "${DECLINED[$mac]:-}" ]]; then
        # Devices retry every few seconds: log the refusal at most once a minute.
        if (( now - ${REFUSED_LOGGED[$mac]:-0} >= 60 )); then
            REFUSED_LOGGED[$mac]=$now
            verb_event "$R" "x refused" "$(label "$mac")  ${D}(declined earlier; restart the program to allow it again)${N}"
        fi
        kick_station "$mac" || true
        return 1
    fi

    CONNECTED[$mac]=$now
    STATE[$mac]=pending
    PENDING_SINCE[$mac]=$now
    verb_event "$Y" "? request" "$(label "$mac")${sig:+  signal $sig dBm}   ${B}a ${ID[$mac]}${N} accept, ${B}d ${ID[$mac]}${N} decline ${D}(auto-decline in ${ACCEPT_TIMEOUT}s)${N}"
}

# A station that left (by itself or kicked).
station_gone() {
    local mac=$1 now=$2 reason=${KICKED[$mac]:-} after
    after="after $(fmt_duration $(( now - CONNECTED[$mac] )))"
    case "$reason" in
        declined|auto-declined)
            verb_event "$R" "x $reason" "$(label "$mac")" ;;
        "disconnected by you")
            verb_event "$R" "- disconnected" "$(label "$mac")${IPS[$mac]:+  ${IPS[$mac]}}  $after  ${D}(by you)${N}" ;;
        *)
            if [[ "${STATE[$mac]:-}" == accepted ]]; then
                verb_event "$R" "- disconnected" "$(label "$mac")${IPS[$mac]:+  ${IPS[$mac]}}  $after"
            else
                verb_event "$R" "- left" "$(label "$mac")  ${D}(was waiting to be accepted)${N}"
            fi ;;
    esac
    nft_unapprove "$mac"
    unset 'CONNECTED[$mac]' 'IPS[$mac]' 'STATE[$mac]' 'PENDING_SINCE[$mac]' 'KICKED[$mac]'
}

poll_devices() {
    local -A seen=()
    local mac sig ip host now
    printf -v now '%(%s)T' -1

    # Stations associated right now. Some drivers (e.g. the Pi's brcmfmac) report no signal.
    while read -r mac sig; do
        [[ -n "$mac" ]] && seen[$mac]=$sig
    done < <(iw dev "$WIFI_IFACE" station dump 2>/dev/null | awk '
        /^Station/ { mac = tolower($2); sig[mac] = "" }
        /signal:/  { sig[mac] = $2 }
        END        { for (m in sig) print m, sig[m] }')

    for mac in "${!seen[@]}"; do
        if [[ -z "${CONNECTED[$mac]:-}" ]]; then
            new_station "$mac" "${seen[$mac]}" "$now" || true
        elif [[ "${STATE[$mac]}" == declined ]]; then
            kick_station "$mac" >/dev/null 2>&1 || true      # still there: try again quietly
        fi
    done
    for mac in "${!CONNECTED[@]}"; do
        if [[ -z "${seen[$mac]+x}" ]]; then
            station_gone "$mac" "$now"
        elif [[ "${STATE[$mac]}" == pending ]] && (( now - PENDING_SINCE[$mac] >= ACCEPT_TIMEOUT )); then
            decline_device "$mac" auto-declined
        fi
    done

    # DHCP leases: "<expiry> <mac> <ip> <hostname> <client-id>". Only accepted devices get one.
    [[ -r "$LEASES" ]] || return 0
    while read -r _ mac ip host _; do
        [[ -n "${ip:-}" && -n "${CONNECTED[$mac]:-}" && "${STATE[$mac]:-}" == accepted ]] || continue
        if [[ "${IPS[$mac]:-}" != "$ip" ]]; then
            IPS[$mac]=$ip
            HOSTS[$mac]=$host
            event "${C}= address${N}       $(name_of "$mac")  ->  ${B}$ip${N}"
        fi
    done < "$LEASES"
}

dns_event() { event "${Y}! $*${N}"; }

# dns.txt edited (or created / deleted): regenerate the files, reload dnsmasq, log changes.
check_dns_file() {
    [[ "$(dns_sum)" != "$DNS_SUM" ]] || return 0
    local -A before=() after=()
    local entry
    for entry in "${DNS_ENTRIES[@]}"; do before[$entry]=1; done
    write_dns_hosts dns_event
    for entry in "${DNS_ENTRIES[@]}"; do after[$entry]=1; done

    if reload_dnsmasq; then
        event "${C}~ dns.txt${N}       reloaded: ${#DNS_ENTRIES[@]} names"
    else
        event "${R}! dns.txt changed but dnsmasq was not found to reload; it applies when the AP restarts${N}"
    fi
    for entry in "${DNS_ENTRIES[@]}"; do
        [[ -n "${before[$entry]:-}" ]] || event "    ${G}+${N} ${entry#* }  ->  ${entry%% *}"
    done
    for entry in "${!before[@]}"; do
        [[ -n "${after[$entry]:-}" ]] || event "    ${R}-${N} ${entry#* }  ->  ${entry%% *}"
    done
}

# The AP can drop (e.g. the driver resets); bring it back instead of silently serving nothing.
check_access_point() {
    local state
    state="$(nmcli -t -f GENERAL.STATE connection show "$AP_CON" 2>/dev/null | cut -d: -f2 || true)"
    if [[ "$state" != activated ]]; then
        event "${Y}! access point is ${state:-down}; restarting it${N}"
        nmcli --wait 30 connection up "$AP_CON" >/dev/null 2>&1 || event "${R}! could not restart it; check 'nmcli device'${N}"
    fi
}

# ---- commands typed while it runs -------------------------------------------------------
cmd_error() { event "${R}! $*${N}  ${D}(h for help)${N}"; }

# Sets RESOLVED to the connected device named by $1: its number, its MAC, or (empty) the
# device that has been waiting longest. Returns 1, after reporting why, if there is none.
RESOLVED=
resolve_device() {
    local arg=${1,,} mac best=
    RESOLVED=
    if [[ -z "$arg" ]]; then
        for mac in "${!PENDING_SINCE[@]}"; do
            if [[ -z "$best" ]] || (( PENDING_SINCE[$mac] < PENDING_SINCE[$best] )); then best=$mac; fi
        done
        [[ -n "$best" ]] || { cmd_error "no device is waiting to be accepted"; return 1; }
        RESOLVED=$best
        return 0
    fi
    for mac in "${!CONNECTED[@]}"; do
        if [[ "$arg" == "${ID[$mac]}" || "$arg" == "$mac" ]]; then RESOLVED=$mac; return 0; fi
    done
    cmd_error "no connected device '$1' (l lists them)"
    return 1
}

list_devices() {
    local mac now state
    printf -v now '%(%s)T' -1
    if (( ${#CONNECTED[@]} == 0 )); then
        event "${D}no devices connected${N}"
        return 0
    fi
    event "${B}connected devices${N}"
    while read -r _ mac; do
        state=${STATE[$mac]}
        if [[ "$state" == pending ]]; then
            state="${Y}waiting $(( now - PENDING_SINCE[$mac] ))s/${ACCEPT_TIMEOUT}s${N}"
        elif [[ "$state" == accepted ]]; then
            state="${G}accepted${N}"
        fi
        printf '            %-5s %-17s  %-20s %-15s  for %-7s  %b\n' "[${ID[$mac]}]" "$mac" \
            "$(h=${HOSTS[$mac]:-}; [[ "$h" == "*" ]] && h=; echo "${h:--}")" "${IPS[$mac]:--}" \
            "$(fmt_duration $(( now - CONNECTED[$mac] )))" "$state"
    done < <(for mac in "${!CONNECTED[@]}"; do echo "${ID[$mac]} $mac"; done | sort -n)
}

print_help() {
    event "${B}commands${N}  (n = device number from the log, or its MAC)"
    printf '            %-8s %s\n' \
        "a [n]" "accept (no n: the device waiting longest)" \
        "d [n]" "decline: disconnect it and refuse it until this program restarts" \
        "k n"   "disconnect a device; if it comes back it is asked about again" \
        "l"     "list the connected devices" \
        "h"     "this help"
}

handle_command() {
    local cmd= arg= extra=
    read -r cmd arg extra <<< "$1" || true
    [[ -n "$cmd" ]] || return 0
    if [[ -n "$extra" ]]; then cmd_error "too many arguments: $1"; return 0; fi
    case "${cmd,,}" in
        a|accept)
            resolve_device "$arg" || return 0
            if [[ "${STATE[$RESOLVED]}" == declined ]]; then
                cmd_error "$(label "$RESOLVED") was declined; restart the program to allow it again"
                return 0
            fi
            accept_device "$RESOLVED" ;;
        d|decline)
            resolve_device "$arg" || return 0
            if [[ "${STATE[$RESOLVED]}" == accepted ]]; then
                cmd_error "$(label "$RESOLVED") is already accepted: 'k ${ID[$RESOLVED]}' disconnects it"
                return 0
            fi
            decline_device "$RESOLVED" declined ;;
        k|kick|disconnect)
            [[ -n "$arg" ]] || { cmd_error "which device? e.g. 'k 3' (l lists them)"; return 0; }
            resolve_device "$arg" || return 0
            if [[ "${STATE[$RESOLVED]}" != accepted ]]; then
                cmd_error "$(label "$RESOLVED") is not accepted: 'd ${ID[$RESOLVED]}' declines it"
                return 0
            fi
            disconnect_device "$RESOLVED" ;;
        l|ls|list)   list_devices ;;
        h|help|\?)   print_help ;;
        *)           cmd_error "unknown command '$cmd'" ;;
    esac
}

# Waits POLL_SECS for a command line on the terminal (returns early when one is typed).
INTERACTIVE=0
[[ -t 0 ]] && INTERACTIVE=1
wait_for_command() {
    local line status
    if (( ! INTERACTIVE )); then
        sleep "$POLL_SECS"
        return 0
    fi
    if IFS= read -r -t "$POLL_SECS" line; then
        handle_command "$line"
    else
        status=$?
        if (( status <= 128 )); then        # end of input (Ctrl+D), not a timeout
            INTERACTIVE=0
            event "${Y}! input closed: commands are off, new devices are declined after ${ACCEPT_TIMEOUT}s${N}"
        fi
    fi
}

# ---- main -------------------------------------------------------------------------------
warn "If you are connected over SSH through $WIFI_IFACE, this drops the session; use Ethernet or the AP itself."
(( INTERACTIVE )) || warn "Not run from a terminal: nobody can accept devices, so every new device is declined after ${ACCEPT_TIMEOUT}s."
start_access_point
print_banner

ticks=0
while true; do
    poll_devices
    check_dns_file
    ticks=$(( ticks + 1 ))
    if (( ticks % 5 == 0 )); then check_access_point; fi
    wait_for_command
done
