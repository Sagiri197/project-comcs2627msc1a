# Alert server

The program that receives the ESP32 readings over UDP (port 8080), stores them and raises alerts.
It is a plain terminal program (Ctrl+C to stop). It does not set up any network: the Wi-Fi
access point, DHCP/DNS and NTP are in [access-point/](../access-point/README.md).

```
server/
├── daemon/        main(): the receive loop
├── udp/           UDP socket (POSIX and Windows)
├── mqtt/          status + heartbeat to the Command Centre's broker (libmosquitto)
├── application/   alert logic (thresholds, differentials)
├── persistency/   SQLite reading store
└── vendor/        third-party code (googletest, spdlog, sqlite, ...)
```

The message format is shared with the nodes in [common/protocol.h](../common/protocol.h).
See [ARCHITECTURE.md](ARCHITECTURE.md) for how the pieces fit together.

## Build and test (Linux, WSL, Raspberry Pi)

```sh
git submodule update --init --recursive
cmake -S server -B server/build -DBUILD_TESTING=ON
cmake --build server/build -j2
ctest --test-dir server/build --output-on-failure
```

## Run (Linux, Raspberry Pi)

The server keeps its files in the directory it is started from: `readings.sqlite3` and
`logs/alert_server.log`. Pick one directory and always start it there, for example:

```sh
mkdir -p server/build/data && cd server/build/data
../bin/alert_server                      # Ctrl+C to stop
```

To keep it running after you close the SSH session, start it inside `tmux` (or `screen`).

## Options

```
alert_server [--udp-port PORT] [--mqtt-host HOST] [--mqtt-port PORT] [--status-interval SECONDS] [--no-mqtt]
  --udp-port PORT           port for the ESP32 readings (default 8080)
  --mqtt-host HOST          Command Centre broker (default cc.comcs.lan)
  --mqtt-port PORT          broker port (default 1883)
  --status-interval SECONDS heartbeat period (default 5)
  --no-mqtt                 do not connect to the broker
```

## MQTT status

The server reports its own state to the Command Centre's broker (topics as in the
[MQTT contract](../command-centre/README.md#mqtt-contract)):

| Topic | When | Payload |
|-------|------|---------|
| `comcs/cell1/alert-server/status` | on every (re)connect | `online` (retained) |
| | on Ctrl+C, or by the broker as Last Will if the process dies | `offline` (retained) |
| `comcs/cell1/alert-server/heartbeat` | every `--status-interval` seconds | JSON: uptime, readings received, invalid datagrams, storage errors, last reading |

This runs on its own thread: if the broker is down or unreachable, the server keeps receiving
and storing readings, logs one warning, and connects as soon as the broker is reachable.
Watch it with `docker exec comcs-mosquitto mosquitto_sub -t 'comcs/cell1/alert-server/#' -v`.

## Build and run on Windows

Needs Visual Studio (or its Build Tools) with "Desktop development with C++". Run from a
"Developer PowerShell for VS", which has `cmake` on PATH:

```powershell
git submodule update --init --recursive
cmake -S server -B server\build-windows -A x64 -DBUILD_TESTING=OFF
cmake --build server\build-windows --config Release --target alert_server --parallel
mkdir server\build-windows\data -Force; cd server\build-windows\data
..\bin\Release\alert_server.exe     # Ctrl+C to stop; database and logs stay in this directory
```

The unit tests are POSIX-only and are skipped in the Windows build.
