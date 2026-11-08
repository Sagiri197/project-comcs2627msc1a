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

## Run

The Command Centre always runs with Docker Compose, directly on the Windows or Linux machine
that joins the cell's Wi-Fi. **Do not run it inside WSL:** the ESP32s and the alert server
connect to the broker at this machine's address (`cc.comcs.lan`), and WSL2 sits behind its own
NAT, so they cannot reach a broker running in it.

### Windows

1. Install **Docker Desktop** and start it. Its WSL 2 backend is fine: Docker Desktop publishes
   container ports on the Windows host. Docker Engine installed inside a WSL distro is not.
2. Allow the ports through Windows Firewall (PowerShell as administrator):
   ```powershell
   New-NetFirewallRule -DisplayName "COMCS MQTT 1883" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
   New-NetFirewallRule -DisplayName "COMCS Node-RED 1880" -Direction Inbound -Protocol TCP -LocalPort 1880 -Action Allow
   ```
3. Nothing else may listen on port 1883. If Mosquitto for Windows is installed, stop it
   (as administrator): `Stop-Service mosquitto; Set-Service mosquitto -StartupType Disabled`.
4. Start the stack from PowerShell:
   ```powershell
   cd command-centre
   docker compose up -d --build
   docker compose logs -f nodered     # "Connected to broker: comcs-command-centre@mqtt://mosquitto:1883"
   ```

### Linux

1. Install Docker Engine and the Compose plugin, and allow your user to use it:
   ```bash
   sudo apt install -y docker.io docker-compose-v2
   sudo usermod -aG docker "$USER"    # log out and back in afterwards
   ```
2. If a firewall is enabled, open the ports: `sudo ufw allow 1883/tcp && sudo ufw allow 1880/tcp`.
3. Nothing else may listen on port 1883: `sudo systemctl disable --now mosquitto` if it is installed.
4. Start the stack:
   ```bash
   cd command-centre
   docker compose up -d --build
   docker compose logs -f nodered     # "Connected to broker: comcs-command-centre@mqtt://mosquitto:1883"
   ```
   Node-RED inside the container runs as uid 1000. If it cannot write `flows.json`, run
   `sudo chown -R 1000:1000 nodered`.

### Joining the cell

Connect this machine's Wi-Fi to the Pi's access point (`AP_SSID` in `server/pi.env`). The Pi
gives it a fixed address, `192.168.50.2` / `cc.comcs.lan`, only if `COMMAND_CENTRE_MAC` in
`server/pi.env` is this machine's Wi-Fi MAC (Windows: `getmac /v`, Linux: `ip link`). The
ESP32s (`mqtt_host` in `nodes/configs/*.h`) and the alert server use that name.

| What      | Where                                          |
|-----------|------------------------------------------------|
| Editor    | http://localhost:1880                          |
| Dashboard | http://localhost:1880/dashboard                |
| Broker    | `cc.comcs.lan:1883` (no authentication)        |

Stop with `docker compose down`. Retained status messages and QoS 1 sessions are stored in
the `mosquitto-data` volume, so they survive restarts. Node-RED takes ~30 s to start; the
editor returns an empty response until then.

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
buffered reading and an invalid reading. From the command line (the MQTT clients come with the
broker container, so nothing needs installing on the host):

```bash
docker exec comcs-mosquitto mosquitto_sub -t 'comcs/#' -v
docker exec comcs-mosquitto mosquitto_pub -q 1 -t comcs/cell1/alerts -m '{"description":"test alert","severity":"low"}'
docker exec comcs-mosquitto mosquitto_sub -t '$SYS/broker/clients/#' -v   # broker statistics
docker compose logs -f mosquitto                                         # clients connecting/disconnecting
```
