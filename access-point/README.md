# Access point

Creates the cell's 2.4 GHz Wi-Fi network (`comcs-cell1`) that the ESP32s, the alert server and
the Command Centre share, with DHCP, DNS names and NTP. There is one program per OS:

| OS | Program | Host |
|----|---------|------|
| Linux | `access-point/linux/access-point.sh` | Raspberry Pi 5 (or any Debian with NetworkManager) |
| Windows | `access-point\windows\access-point.cmd` | a laptop's Mobile hotspot, standing in for the Pi |

Both work the same way:

1. **Start it**: it creates the access point and prints the network details and the values to
   put in the ESP32 configs.
2. **Leave it running**: it logs every device that connects, gets an address or leaves.
3. **Ctrl+C**: it removes the access point and undoes everything it changed.

```
COMCS access point is up   (Ctrl+C to stop and remove it)

  Wi-Fi          comcs-cell1  password comcs2627  (WPA2, 2.4 GHz, channel 6, wlan0)
  This host      192.168.50.1/24   DHCP for the clients
  Internet       shared from eth0
  NTP            192.168.50.1 udp/123
  DNS            7 names from access-point/dns.txt (edit it any time: reloaded automatically)
                   as.comcs.lan           192.168.50.1
                   pool.ntp.org           192.168.50.1
                   ...

  ESP32 config (nodes/configs/*.h):
    wifi_ssid        "comcs-cell1"
    wifi_password    "comcs2627"
    server_address   alert server name from dns.txt, UDP 8080
    mqtt_host        broker name from dns.txt, TCP 1883

Devices
  21:34:56  + connected     aa:bb:cc:00:00:01  signal -48 dBm
  21:34:57  = address       aa:bb:cc:00:00:01 (esp32-001)  ->  192.168.50.23
  21:34:58  + connected     4c:5f:70:44:0c:be
  21:34:58  = address       4c:5f:70:44:0c:be (CC-LAPTOP)  ->  192.168.50.87
  21:34:59  - disconnected  aa:bb:cc:00:00:01 (esp32-001)  192.168.50.23  after 2s
  21:35:10  ~ dns.txt       reloaded: 8 names
  21:35:10      + cc.comcs.lan  ->  192.168.50.87
```

[ap.env](ap.env) holds the Wi-Fi settings (SSID, password, channel) and the ports shown at
start-up. Put local overrides, such as passwords, in `ap.env.local` (git-ignored).

The DNS names are in one file per host, used exactly as written:

| File | Host | Line format |
|------|------|-------------|
| [dns.txt](dns.txt) | Linux (Pi, `192.168.50.0/24`) | `hostname ipAddress` |
| [dns.windows.txt](dns.windows.txt) | Windows (hotspot, normally `192.168.137.1`) | `hostname ipAddress` |

Blank lines and `# comments` are allowed. The program checks its file every 2 s while it runs:
edit and save it, and the change is applied and logged without restarting the access point.
Lines in the wrong format are reported and skipped. NTP is always on, and the public NTP names
(`pool.ntp.org`, ...) point at the access point so the ESP32s sync without Internet.

The ESP32 configs only use names (`as.comcs.lan`, `cc.comcs.lan`), so they work unchanged with
either host.

```
             Wi-Fi AP "comcs-cell1"  (192.168.50.0/24 on the Pi)
ESP32 A ─┐
ESP32 B ─┼──► AP host (Pi)            192.168.50.1  as.comcs.lan  DHCP, DNS, NTP 123
ESP32 C ─┘    (alert server runs here: UDP 8080)
   │  └────► Command Centre machine   192.168.50.2  cc.comcs.lan  MQTT 1883
   └───────────────────────────────────▲
```

## Linux (Raspberry Pi)

Requires **Raspberry Pi OS Bookworm** or newer (NetworkManager), plus:

```bash
sudo apt-get install iw rfkill dnsmasq-base chrony     # once; the program says if any are missing
```

Run it (it asks for sudo):

```bash
access-point/linux/access-point.sh            # Ctrl+C to stop
```

While it runs:

- **Wi-Fi**: a NetworkManager profile `comcs-ap` in AP mode on `WIFI_IFACE`. It does not start
  on boot: the access point exists only while the program runs. When it stops, the interface
  returns to its normal Wi-Fi connection.
- **DHCP and DNS**: NetworkManager's shared mode; this host is `192.168.50.1/24`. The names in
  `dns.txt` are copied to `/run/comcs-dns.hosts` (dnsmasq `addn-hosts`). When `dns.txt`
  changes, the file is rewritten and dnsmasq gets `SIGHUP` to re-read it; DHCP leases are
  kept. The Command Centre gets an ordinary DHCP address: once it joins, add
  `cc.comcs.lan <its address>` (from the device log) to `dns.txt`. Names match exactly: `pool.ntp.org` does not cover `0.pool.ntp.org`, so add
  subdomains as their own lines. If this host has an uplink (Ethernet), Internet is NAT-shared
  to the clients.
- **NTP**: chrony serves this host's time on udp/123.
- **Device log**: polls the associated stations (`iw station dump`) and the DHCP leases every
  2 s. Some drivers, like the Pi's, do not report signal strength.
- **Recovery**: if the access point drops, it is brought back up.

Its files (`/etc/NetworkManager/dnsmasq-shared.d/comcs.conf`, `/etc/chrony/conf.d/comcs.conf`,
`/run/comcs-dns.hosts`) are removed when it stops. If the program is killed hard
(`kill -9`, power loss), the next start cleans up the leftovers.

> **Over SSH:** starting it on `wlan0` drops an SSH session that came in over `wlan0`. Use
> Ethernet or a keyboard, or SSH in through the AP itself afterwards (`ssh <user>@192.168.50.1`).
> Run it inside `tmux` so it survives the SSH session closing.

### Time without Internet

With no uplink, chrony serves the Pi's own clock, so make sure it is right. The Pi 5 keeps
time across reboots only with an RTC battery. Otherwise set the clock after boot, for example
by pushing the laptop's time:

```bash
ssh <user>@as.comcs.lan "sudo ~/comcs/access-point/linux/set-time.sh @<unix-seconds>"
```

(the PowerShell form is in [linux/set-time.sh](linux/set-time.sh)). Check it with
`chronyc tracking` and `sudo chronyc clients`.

## Windows (stand-in for the Pi)

```powershell
access-point\windows\access-point.cmd          # asks for administrator rights; Ctrl+C to stop
```

If the terminal is not already elevated, it opens a new administrator window and runs there.

While it runs:

- **Wi-Fi**: the Mobile hotspot with `AP_SSID` / `AP_PASSWORD`, on 2.4 GHz. Windows picks its
  address (normally `192.168.137.1`, not the Pi's `192.168.50.1`); the nodes only use names.
- **Names**: the names in `dns.windows.txt` go into a block in the hosts file (the hotspot's DNS
  answers from it), which is rewritten when the file changes. The laptop stands in for both the
  Pi and the Command Centre, so the names point at the hotspot's address; the start-up screen
  warns if any don't. Keep `time.windows.com` out: the laptop syncs its own clock from it.
- **NTP**: Windows Time serves udp/123.
- **Firewall**: inbound UDP 8080, TCP 1883, TCP 1880 and UDP 123 are opened.
- **Device log**: polls the hotspot's client list every 2 s; addresses come from the ARP table.
- **Recovery**: Windows switches an idle hotspot off by itself; the program turns it back on.

On Ctrl+C it stops the hotspot, removes the hosts entries, turns the NTP server off and
deletes the firewall rules. Closing the window with **X** skips that cleanup: run it again
and stop it with Ctrl+C.

Then run the alert server natively ([server/README.md](../server/README.md)) and the Command
Centre with Docker. Not in WSL: WSL does not see the hotspot adapter, so hotspot clients cannot
reach a server running there.

Mobile hotspot needs another connection to share (e.g. Ethernet) and may be disabled by policy
on managed laptops. The program runs without changing the PowerShell execution policy.
