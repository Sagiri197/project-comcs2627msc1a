# Command Centre (Node-RED + MQTT)

Runs the MQTT broker (Mosquitto 2.0.22) and the Node-RED command centre
(Node-RED 5.0.8, Dashboard 2.0 `@flowfuse/node-red-dashboard` 1.34.0).

```
command-centre/
├── docker-compose.yml
├── mosquitto/mosquitto.conf
└── nodered/              mounted as Node-RED's /data
    ├── Dockerfile        Node-RED image + dashboard
    ├── settings.js
    └── flows.json        the command-centre flow (commit changes made in the editor)
```

## Run (Ubuntu / WSL)

Needs Docker: either Docker Desktop with WSL integration enabled for the distro, or
`sudo apt install docker.io docker-compose-v2`. Keep the repo on the Linux filesystem
(`~/...`), not under `/mnt/c`.

```bash
cd command-centre
docker compose up -d --build
docker compose logs -f nodered     # "Connected to broker: comcs-command-centre@mqtt://mosquitto:1883"
```

| What      | Where                                 |
|-----------|---------------------------------------|
| Editor    | http://localhost:1880                 |
| Dashboard | http://localhost:1880/dashboard       |
| Broker    | `<host IP>:1883` (no authentication)  |

Stop with `docker compose down`. Retained status messages and QoS 1 sessions are stored in
the `mosquitto-data` volume, so they survive restarts.

Node-RED inside the container runs as uid 1000. If it cannot write `flows.json`, run
`sudo chown -R 1000:1000 nodered`.

### Making the broker reachable from the ESP32s

By default WSL2 is behind NAT, so devices on the Wi-Fi can't reach port 1883 inside WSL.
- **Docker Desktop:** published ports are already exposed on the Windows host IP.
- **Docker Engine inside WSL:** enable mirrored networking in `%UserProfile%\.wslconfig`
  (`[wsl2]` then `networkingMode=mirrored`) and run `wsl --shutdown`. The alternative is
  `netsh interface portproxy add v4tov4 listenport=1883 connectport=1883 connectaddress=<WSL IP>`.
- In both cases, allow inbound TCP 1883 in Windows Firewall. The ESP32s use the
  **Windows** LAN IP (`ipconfig`).

### Without Docker (Ubuntu / WSL)

Node-RED 5 needs **Node.js 22.9 or newer**, and Ubuntu's `apt install nodejs` is too old. Use nvm:

```bash
# 1. Node.js
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
source ~/.bashrc
nvm install 22 && node -v

# 2. Mosquitto broker. Ubuntu's default config only listens on localhost, so open it to the LAN
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
printf 'listener 1883 0.0.0.0\nallow_anonymous true\n' | sudo tee /etc/mosquitto/conf.d/comcs.conf
sudo service mosquitto restart        # or: sudo systemctl enable --now mosquitto

# 3. Node-RED + dashboard, installed locally in command-centre/nodered (git-ignored)
cd command-centre/nodered
npm install --no-audit --no-fund node-red@5.0.8 @flowfuse/node-red-dashboard@1.34.0
npx node-red --userDir .              # MQTT_HOST defaults to localhost
```

The editor and dashboard URLs are the same as above. Stop Node-RED with Ctrl+C. Retained
messages are persisted by Ubuntu's default `/etc/mosquitto/mosquitto.conf` in `/var/lib/mosquitto`.

## MQTT contract

All messages use QoS 1. `<nodeId>` matches `node_id` in `nodes/configs/*.h` (for example `esp32-001`).

| Topic                          | Publisher     | Payload                                   | Retained |
|--------------------------------|---------------|-------------------------------------------|----------|
| `comcs/cell1/<nodeId>/weather` | ESP32         | WeatherObserved (see below)               | no       |
| `comcs/cell1/<nodeId>/status`  | ESP32         | `online` on connect, `offline` as LWT     | yes      |
| `comcs/cell1/alerts`           | alert server  | Alert (see below)                         | no       |
| `comcs/cell1/alert-server/status` | alert server | `online` / `offline` (LWT)              | yes      |
| `comcs/cell1/command-centre/status` | Node-RED | `online` / `offline` (birth/LWT)          | yes      |

**Reading:** Smart Data Model `WeatherObserved` key-values, the same object as the UDP
`payload` (see `docs/04 - protocol.md`). The full UDP envelope (`{"kind":"reading", ...}`)
is accepted too. `relativeHumidity` is a fraction (`0.56` = 56 %).

```json
{"id":"urn:ngsi-ld:WeatherObserved:esp32-001:<session>:42","type":"WeatherObserved",
 "dateObserved":"2026-11-02T14:30:01Z","location":{"type":"Point","coordinates":[-8.608,41.178]},
 "refDevice":"urn:ngsi-ld:Device:esp32-001","temperature":23.4,"relativeHumidity":0.56}
```

**Alert:** Smart Data Model `Alert` key-values. Only `description` is required for
display. `id` is used to discard duplicates.

```json
{"id":"urn:ngsi-ld:Alert:cell1:<unique>","type":"Alert","category":"environment",
 "subCategory":"temperatureDifferential","severity":"medium","dateIssued":"2026-11-02T14:30:02Z",
 "description":"Temperature differential 2.6 °C between esp32-001 and esp32-002 exceeds 2 °C",
 "alertSource":"urn:ngsi-ld:Device:alert-server",
 "data":{"differential":2.6,"threshold":2,"nodes":["esp32-001","esp32-002"]}}
```

Suggested `subCategory` values: `temperatureDifferential`, `humidityDifferential`,
`temperatureOutOfRange`, `humidityOutOfRange`.

## What the flow does

- **Validation:** checks the `WeatherObserved` type, URN, numeric ranges and timestamp.
  Rejected messages appear in the `rejected readings` debug node.
- **Duplicates:** discarded by `id`, which covers QoS 1 redelivery and rolling-window resends.
- **Message validity by time:** readings older than 10 s are treated as rolling-window
  replays. They are plotted at their observation time as `<nodeId> (buffered)` but are
  excluded from the live average. If `timeSynchronized` is false, or the clock is more
  than 5 s ahead, the reception time is used instead.
- **Average:** recomputed every second from nodes whose last live reading is newer than 5 s.
- **Dashboard:** average gauges, temperature and humidity history (per node plus average, last
  hour), alert indicator with an Acknowledge button, a recent-alerts table and popups.
- **Failure detection:** retained LWT status plus a 5 s data watchdog per component
  (states OK / STALE / OFFLINE / NO DATA), a popup on state changes, and the MQTT broker
  connection state.

The expected components default to `esp32-001,esp32-002,alert-server`. Override them with
the `EXPECTED_COMPONENTS` environment variable.

## Testing without hardware

The **Simulator** tab publishes to the broker: one reading per node, a continuous 1 s
stream (enable the disabled `every 1 s` inject), an alert, LWT offline/online, a 2-minute-old
buffered reading and an invalid reading. From the command line:

```bash
mosquitto_sub -h localhost -t 'comcs/#' -v
mosquitto_pub -h localhost -q 1 -t comcs/cell1/alerts -m '{"description":"test alert","severity":"low"}'
```
