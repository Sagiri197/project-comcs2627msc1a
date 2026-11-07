# Raspberry Pi 5 server

Runs the whole back end of the cell on a Raspberry Pi 5:

- **Network**: by default the Pi is a 2.4 GHz Wi-Fi access point with DHCP and DNS, and the
  ESP32s connect to it directly. It can also join an existing Wi-Fi network instead.
- **Alert server**: UDP 8080, built natively and run by systemd as `comcs-alert-server`.
- **MQTT broker + Node-RED**: the `command-centre/` Docker stack. By default this runs on a
  separate Command Centre machine that joins the Pi's access point (see below). Set
  `RUN_COMMAND_CENTRE_ON_PI=yes` in `pi.env` to run it on the Pi instead.

```
             Wi-Fi AP "comcs-cell1"  (192.168.50.0/24), hosted by the Pi
ESP32 A ─┐
ESP32 B ─┼──► Alert Server: Raspberry Pi 5   192.168.50.1  comcs.lan     UDP 8080
ESP32 C ─┘
   │  └────► Command Centre machine          192.168.50.2  cc.comcs.lan  MQTT 1883, Node-RED 1880
   └───────────────────────────────────────────▲  (ESP32s and the alert server publish to it)
```

Requires **Raspberry Pi OS Bookworm 64-bit** or newer, which uses NetworkManager.

```
server/                         (alongside the CMake sources: daemon/, udp/, persistency/, vendor/)
├── pi.env                      settings (SSID, password, addresses, mode)
├── install.sh                  one-time setup: packages, Docker, build, systemd unit
├── build.sh                    rebuild + reinstall the alert server (after git pull)
├── up.sh / down.sh             whole cell up / down
├── network-up.sh / network-down.sh   only the network
├── status.sh                   state + endpoints to give the ESP32s
└── comcs-alert-server.service  systemd unit
```

## Setup

```bash
git clone --recursive <repo-url> ~/comcs && cd ~/comcs
nano server/pi.env           # or put overrides in server/pi.env.local (git-ignored)
server/install.sh            # once (~10 min: Docker + C++ build)
server/up.sh                 # network + broker + Node-RED + alert server
```

Every component is set to start at boot: the NetworkManager profile autoconnects, the containers use
`restart: unless-stopped`, and the systemd unit is enabled. After a reboot the cell comes back
by itself. Run `server/status.sh` to check it.

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

Without an uplink in `ap` mode the ESP32s have no NTP. Use `time_synchronized = false` in that case.

## Connecting the Command Centre

The Command Centre machine joins the Pi's Wi-Fi like any other client. It gets a fixed address
so the ESP32s and the alert server can always reach the broker:

1. On the Command Centre machine, find the MAC address of its Wi-Fi adapter
   (Windows: `getmac /v`, Linux: `ip link`).
2. On the Pi, set `COMMAND_CENTRE_MAC=<that MAC>` in `pi.env.local` and run `server/network-up.sh`.
   Its DHCP server then always gives that machine `192.168.50.2`, and `cc.comcs.lan`
   resolves to it.
3. Connect the Command Centre machine to `comcs-cell1` and start the stack:
   `cd command-centre && docker compose up -d --build`.
4. Windows only: set the `comcs-cell1` connection to **Private** and allow inbound TCP 1883
   (and 1880 for the dashboard) in Windows Firewall. If you use Docker inside WSL, also follow
   the WSL steps in [command-centre/README.md](../command-centre/README.md).
5. On the Pi, `server/status.sh` should show the Command Centre and broker port `up`.

ESP32s and the alert server use `cc.comcs.lan:1883` as the MQTT broker. A machine connected to
the AP doesn't get Internet unless the Pi has an Ethernet uplink.

## Day to day

```bash
server/status.sh                           # what is up, client count, endpoints
journalctl -u comcs-alert-server -f    # alert server logs
docker compose -f command-centre/docker-compose.yml logs -f
server/build.sh [--test]                   # after a git pull that touches server/ or common/
server/down.sh [--keep-network]            # stop everything (and remove the AP/client profile)
mosquitto_sub -h localhost -t 'comcs/#' -v
```

The readings database is `/var/lib/comcs-alert-server/readings.sqlite3`.
