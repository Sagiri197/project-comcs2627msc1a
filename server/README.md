# Raspberry Pi 5 server

Runs the Alert Server side of the cell on a Raspberry Pi 5 (see
[docs/system.png](../docs/system.png)):

- **Network**: the Pi is the cell's 2.4 GHz Wi-Fi access point (`comcs-cell1`) with DHCP, DNS
  and NTP. The ESP32s and the Command Centre connect to it. It can also join an existing
  Wi-Fi network instead.
- **Alert server**: UDP 8080, built natively and run by systemd as `comcs-alert-server`.
  It publishes alerts to the Command Centre's MQTT broker.

The Command Centre (MQTT broker + Node-RED) is a separate machine on the AP, set up from
[command-centre/](../command-centre/README.md). The Pi only gives it a fixed address.

```
             Wi-Fi AP "comcs-cell1"  (192.168.50.0/24), hosted by the Pi
ESP32 A ─┐
ESP32 B ─┼──► Alert Server: Raspberry Pi 5   192.168.50.1  comcs.lan     UDP 8080, NTP 123
ESP32 C ─┘
   │  └────► Command Centre machine          192.168.50.2  cc.comcs.lan  MQTT 1883
   └───────────────────────────────────────────▲  (ESP32s and the alert server publish to it)
```

Requires **Raspberry Pi OS Bookworm 64-bit** or newer, which uses NetworkManager.

```
server/                         (alongside the CMake sources: daemon/, udp/, persistency/, vendor/)
├── pi.env                      settings (SSID, password, addresses, mode)
├── install.sh                  one-time setup: packages, build, systemd unit
├── build.sh                    rebuild + reinstall the alert server (after git pull)
├── up.sh / down.sh             network + alert server up / down
├── status.sh                   state + endpoints to give the ESP32s
├── monitor.sh                  live dashboard: nodes, Wi-Fi stations, readings as they arrive
├── comcs-alert-server.service  systemd unit
└── network/
    ├── up.sh / down.sh         only the network: AP or client Wi-Fi, DHCP/DNS, NTP
    └── set-time.sh             set the clock by hand (served by NTP when offline)
```

## Setup

```bash
git clone --recursive <repo-url> ~/comcs && cd ~/comcs
nano server/pi.env           # or put overrides in server/pi.env.local (git-ignored)
server/install.sh            # once (packages + C++ build)
server/up.sh                 # network (AP, DHCP/DNS, NTP) + alert server
```

Everything is set to start at boot: the NetworkManager profile autoconnects, and chrony and the
systemd unit are enabled. After a reboot the Pi comes back by itself. Run `server/status.sh`
to check it.

> **Over SSH:** switching `wlan0` to AP or client mode drops an SSH session that came in over
> `wlan0`. Use Ethernet or a keyboard for the first `up.sh`. Afterwards you can SSH in through
> the AP itself (`ssh <user>@192.168.50.1`).

## Network modes

Set `WIFI_MODE` in `pi.env`, or pass the mode as an argument: `server/up.sh client`.

| Mode     | What happens | ESP32 `server_address` |
|----------|--------------|------------------------|
| `ap`     | Pi creates SSID `comcs-cell1` on `192.168.50.1/24` and hands out DHCP leases. If the Pi also has an uplink (Ethernet), Internet is NAT-shared, so the ESP32s can sync NTP. | `comcs.lan` or `192.168.50.1` |
| `client` | Pi joins `CLIENT_SSID` (e.g. the phone hotspot) | the IP printed by `status.sh` |
| `none`   | networking is left alone | the Pi's LAN IP |

In `nodes/configs/esp32_00X.h`, set `wifi_ssid` and `wifi_password` to the AP's values and
`server_address` as shown in the table. The ports stay the same: UDP 8080 and MQTT 1883.

## Time (NTP)

In `ap` mode the Pi is the cell's NTP server (chrony, `comcs.lan` / `192.168.50.1`, UDP 123),
and it is also advertised by DHCP (option 42). ESP32s sync from it with
`configTime(0, 0, "comcs.lan")`, with or without Internet. Public NTP names such as
`pool.ntp.org` or `time.google.com` (`NTP_HOSTNAMES` in `pi.env`) also resolve to the Pi for
AP clients, so code using those defaults works too.

- **With an uplink** (Ethernet): chrony syncs from the public pools and relays that time.
- **Without an uplink**: chrony serves the Pi's own clock, so make sure it is right. The Pi 5
  keeps time across reboots only with an RTC battery. Otherwise, set the clock after boot,
  for example by pushing the laptop's time:
  `ssh <user>@comcs.lan "sudo ~/comcs/server/network/set-time.sh @<unix-seconds>"`
  (the PowerShell form is in [network/set-time.sh](network/set-time.sh)).

Check it with `chronyc tracking` (time source) and `sudo chronyc clients` (who is asking).
`server/monitor.sh` shows whether chrony is up and how many clients it has. Set `NTP_SERVER=no` in `pi.env` to turn it off.

## Command Centre address

The Command Centre joins `comcs-cell1` like any other client. The Pi gives it a fixed address
so the ESP32s and the alert server can always reach its broker:

1. Set `COMMAND_CENTRE_MAC` in `pi.env` (or `pi.env.local`) to the MAC of the Command Centre's
   Wi-Fi adapter (Windows: `getmac /v`, Linux: `ip link`; either format works).
2. Run `server/network/up.sh`. That machine then always gets `192.168.50.2`, and
   `cc.comcs.lan` resolves to it.
3. `server/status.sh` shows whether the Command Centre and its broker port are reachable.

ESP32s and the alert server use `cc.comcs.lan:1883` as the MQTT broker. Setting up the broker
itself is covered in [command-centre/README.md](../command-centre/README.md).

## Day to day

```bash
server/status.sh                           # what is up, client count, endpoints
server/monitor.sh                          # live dashboard: active nodes + readings feed (q quits; --once = snapshot)
journalctl -u comcs-alert-server -f        # alert server logs
server/build.sh [--test]                   # after a git pull that touches server/ or common/
server/down.sh [--keep-network]            # stop the alert server (and remove the AP/client profile)
mosquitto_sub -h cc.comcs.lan -t 'comcs/#' -v   # watch the Command Centre's broker from the Pi
```

The readings database is `/var/lib/comcs-alert-server/readings.sqlite3`.
