#!/usr/bin/env bash
# Set the Pi's clock by hand, for when it has no Internet (the AP's NTP server serves this
# clock to the ESP32s). Also saves it to the RTC, so it survives a reboot if the Pi 5 RTC
# has a battery.
#
# Usage: access-point/linux/set-time.sh "2026-11-07 14:30:00"     local time
#        access-point/linux/set-time.sh @1794580200               Unix epoch seconds
#
# From the Command Centre laptop, push its clock to the Pi (PowerShell):
#   ssh <user>@as.comcs.lan "sudo ~/comcs/access-point/linux/set-time.sh @$([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())"
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_root "$@"

[[ $# -eq 1 ]] || die "usage: access-point/linux/set-time.sh \"YYYY-MM-DD HH:MM:SS\" | @EPOCH"

log "Clock before: $(date '+%Y-%m-%d %H:%M:%S %Z')"
date -s "$1" >/dev/null || die "could not parse '$1'"
log "Clock now:    $(date '+%Y-%m-%d %H:%M:%S %Z')"

if [[ -e /dev/rtc0 ]] && command -v hwclock >/dev/null; then
    hwclock --systohc && log "Saved to the RTC"
fi
if command -v fake-hwclock >/dev/null; then
    fake-hwclock save
fi

# chrony keeps serving the system clock; restart so it picks up the step immediately.
systemctl is-active --quiet chrony && systemctl restart chrony
log "ESP32s get this time from the Pi's NTP server on their next sync"
