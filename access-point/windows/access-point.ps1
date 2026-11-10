<#
COMCS cell access point for Windows: the laptop's Mobile hotspot stands in for the Pi's network.

Runs in the foreground. Starting it:
  - configures and starts the Mobile hotspot with the cell's SSID / password (access-point\ap.env), 2.4 GHz
  - serves the DNS names in access-point\dns.windows.txt ("hostname ipAddress" per line) to this
    machine and its clients (hosts file; the hotspot's DNS answers from it). Edits to the file are
    applied while it runs.
  - serves NTP (Windows Time) on udp/123
  - opens the firewall for UDP 8080 (alert server), TCP 1883 (MQTT), TCP 1880 (Node-RED), UDP 123
then prints what the ESP32s need and logs devices as they connect and leave.
Ctrl+C stops the hotspot and undoes all of the above.

Usage: access-point\windows\access-point.cmd     (asks for administrator rights)
Windows PowerShell 5.1 compatible (no ternary, no ??).
#>

$ErrorActionPreference = 'Stop'

$ApDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path          # access-point\ (ap.env)
$DnsFile = Join-Path $ApDir 'dns.windows.txt'
$HostsFile = Join-Path $env:windir 'System32\drivers\etc\hosts'
$HostsBegin = '# BEGIN comcs (server/scripts/windows)'   # old marker, kept so older blocks are still found
$HostsEnd = '# END comcs'
$W32TimeNtpServer = 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\TimeProviders\NtpServer'
$W32TimeConfig = 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\Config'
$FirewallPrefix = 'COMCS-Win-'
$PollSeconds = 2

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Blue }
function Write-Warn([string]$Message) { Write-Host "warning: $Message" -ForegroundColor Yellow }

# ---- elevation ------------------------------------------------------------------------------------
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    # Re-launch in an elevated window (UAC). -NoExit keeps it open after Ctrl+C so the
    # shutdown messages stay readable.
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', "`"$PSCommandPath`"")
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    exit
}

# ---- settings -------------------------------------------------------------------------------------
# KEY=VALUE settings from access-point\ap.env, overridden by ap.env.local (same files as Linux).
function Read-ApEnv {
    $settings = @{}
    foreach ($name in @('ap.env', 'ap.env.local')) {
        $path = Join-Path $ApDir $name
        if (-not (Test-Path $path)) { continue }
        foreach ($line in Get-Content $path) {
            if ($line -notmatch '^\s*([A-Za-z_][A-Za-z0-9_]*)=(.*)$') { continue }
            $raw = $Matches[2]
            $key = $Matches[1]
            if ($raw -match '^"([^"]*)"') { $value = $Matches[1] }
            elseif ($raw -match "^'([^']*)'") { $value = $Matches[1] }
            else { $value = ($raw -replace '\s+#.*$', '').Trim() }
            $settings[$key] = $value
        }
    }
    foreach ($required in @('AP_SSID', 'AP_PASSWORD')) {
        if (-not $settings.ContainsKey($required)) { throw "$required is missing from $ApDir\ap.env" }
    }
    return $settings
}

function Get-Setting($Cfg, [string]$Key, [string]$Default) {
    if ($Cfg.ContainsKey($Key) -and $Cfg[$Key]) { return $Cfg[$Key] }
    return $Default
}

# ---- WinRT (Mobile hotspot API) -------------------------------------------------------------------
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]
$TetheringResult = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult]

$script:AsTaskOperation = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$script:AsTaskAction = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' } | Select-Object -First 1

function Wait-WinRtOperation($Operation, [Type]$ResultType) {
    $task = $script:AsTaskOperation.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $null = $task.Wait(-1)
    return $task.Result
}

function Wait-WinRtAction($Action) {
    $task = $script:AsTaskAction.Invoke($null, @($Action))
    $null = $task.Wait(-1)
}

# The hotspot shares one of this machine's connections (Ethernet, another Wi-Fi, ...).
function Get-TetheringManager {
    $connection = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
    if (-not $connection) {
        $connection = [Windows.Networking.Connectivity.NetworkInformation]::GetConnectionProfiles() |
            Where-Object { $_.GetNetworkConnectivityLevel().ToString() -ne 'None' } | Select-Object -First 1
    }
    if (-not $connection) {
        throw 'Mobile hotspot needs another network connection to share (e.g. Ethernet). Connect one and retry.'
    }
    return [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($connection)
}

# The hotspot's virtual adapter (Wi-Fi Direct), or $null.
function Get-HotspotAdapter {
    Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue |
        Where-Object { $_.InterfaceDescription -like '*Wi-Fi Direct Virtual Adapter*' -and $_.Status -eq 'Up' } |
        Where-Object { Get-NetIPAddress -InterfaceIndex $_.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
                       Where-Object { $_.IPAddress -notlike '169.254.*' } } |
        Select-Object -First 1
}

# ---- hosts file -----------------------------------------------------------------------------------
function Set-HostsBlock([string[]]$Entries) {
    $result = New-Object System.Collections.Generic.List[string]
    $inBlock = $false
    foreach ($line in @(Get-Content $HostsFile -ErrorAction SilentlyContinue)) {
        if ($line -eq $HostsBegin) { $inBlock = $true; continue }
        if ($line -eq $HostsEnd) { $inBlock = $false; continue }
        if (-not $inBlock) { $result.Add($line) }
    }
    while ($result.Count -gt 0 -and $result[$result.Count - 1].Trim() -eq '') { $result.RemoveAt($result.Count - 1) }
    if ($Entries.Count -gt 0) {
        $result.Add('')
        $result.Add($HostsBegin)
        foreach ($entry in $Entries) { $result.Add($entry) }
        $result.Add($HostsEnd)
    }
    # Antivirus / the DNS client often hold the hosts file for a moment: retry for ~10 s.
    $text = ($result -join "`r`n") + "`r`n"
    for ($attempt = 1; ; $attempt++) {
        try {
            [System.IO.File]::WriteAllText($HostsFile, $text, [System.Text.Encoding]::ASCII)
            break
        } catch [System.IO.IOException] {
            if ($attempt -ge 20) { throw "Could not write $HostsFile (locked for 10 s; antivirus?): $($_.Exception.Message)" }
            Start-Sleep -Milliseconds 500
        }
    }
    ipconfig /flushdns | Out-Null
}

# ---- DNS names (dns.windows.txt) ------------------------------------------------------------------
$script:DnsText = $null       # dns.windows.txt content when last applied, to notice edits
$script:DnsEntries = @()      # "ip<TAB>name" lines as written to the hosts file

# The current dns.windows.txt content, '' if it does not exist, or $null if it cannot be read right
# now (an editor holding it mid-save): then the next poll tries again.
function Get-DnsText {
    if (-not (Test-Path $DnsFile)) { return '' }
    try { return [System.IO.File]::ReadAllText($DnsFile) } catch { return $null }
}

# Hosts entries ("ip<TAB>name") from dns.windows.txt text, used as written. Blank lines and
# "# comments" are skipped; other lines that are not "hostname ipAddress" are reported with $Report
# and skipped.
function ConvertTo-HostsEntries([string]$Text, [scriptblock]$Report) {
    if (-not $Text) { & $Report "$DnsFile is missing or empty: no DNS names are served" }
    $entries = New-Object System.Collections.Generic.List[string]
    $n = 0
    foreach ($line in ($Text -split "`r?`n")) {
        $n++
        $fields = @(($line -replace '#.*$', '').Trim() -split '\s+' | Where-Object { $_ })
        if ($fields.Count -eq 0) { continue }
        if ($fields.Count -ne 2 -or $fields[1] -notmatch '^(\d{1,3}\.){3}\d{1,3}$') {
            & $Report "dns.windows.txt line $n ignored (expected 'hostname ipAddress'): $line"
            continue
        }
        $entries.Add("$($fields[1])`t$($fields[0])")
    }
    return ,$entries.ToArray()
}

# Applies dns.windows.txt to the hosts file if it changed since the last call (or $Force).
# Returns $true when it applied it.
function Sync-DnsFile([scriptblock]$Report, [switch]$Force) {
    $text = Get-DnsText
    if ($null -eq $text) { return $false }
    if (-not $Force -and $text -eq $script:DnsText) { return $false }
    $entries = ConvertTo-HostsEntries $text $Report
    Set-HostsBlock $entries
    $script:DnsText = $text
    $script:DnsEntries = $entries
    return $true
}

# ---- start / stop ---------------------------------------------------------------------------------
function Start-AccessPoint($Cfg, $Manager) {
    if ($Manager.TetheringOperationalState.ToString() -eq 'On') {
        Write-Step 'Stopping the running hotspot to apply the settings'
        $null = Wait-WinRtOperation ($Manager.StopTetheringAsync()) $TetheringResult
    }

    Write-Step "Configuring hotspot '$($Cfg['AP_SSID'])'"
    $ap = $Manager.GetCurrentAccessPointConfiguration()
    $ap.Ssid = $Cfg['AP_SSID']
    $ap.Passphrase = $Cfg['AP_PASSWORD']
    try {
        $ap.Band = [Windows.Networking.NetworkOperators.TetheringWiFiBand]::TwoPointFourGigahertz
    } catch {
        Write-Warn 'could not force 2.4 GHz (needs Windows 11); set Band = 2.4 GHz in Settings > Mobile hotspot'
    }
    Wait-WinRtAction ($Manager.ConfigureAccessPointAsync($ap))

    # Always announced as a reliable time source, so it answers without Internet.
    Write-Step 'Starting the NTP server (Windows Time, udp/123)'
    Set-ItemProperty -Path $W32TimeNtpServer -Name Enabled -Value 1 -Type DWord
    Set-ItemProperty -Path $W32TimeConfig -Name AnnounceFlags -Value 5 -Type DWord
    Set-Service w32time -StartupType Automatic
    if ((Get-Service w32time).Status -eq 'Running') { Restart-Service w32time -Force } else { Start-Service w32time }
    w32tm /config /update | Out-Null

    # The hotspot network is usually classified as Public.
    Write-Step 'Opening the firewall'
    $rules = @(
        @{ Name = 'UDP-8080'; Protocol = 'UDP'; Port = 8080; Text = 'alert server' },
        @{ Name = 'TCP-1883'; Protocol = 'TCP'; Port = 1883; Text = 'MQTT broker' },
        @{ Name = 'TCP-1880'; Protocol = 'TCP'; Port = 1880; Text = 'Node-RED' },
        @{ Name = 'UDP-123'; Protocol = 'UDP'; Port = 123; Text = 'NTP' }
    )
    foreach ($rule in $rules) {
        $name = $FirewallPrefix + $rule.Name
        if (-not (Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue)) {
            $null = New-NetFirewallRule -Name $name -DisplayName "COMCS $($rule.Text) ($($rule.Protocol) $($rule.Port))" `
                -Direction Inbound -Action Allow -Protocol $rule.Protocol -LocalPort $rule.Port -Profile Any
        }
    }

    Write-Step 'Starting the hotspot'
    $result = Wait-WinRtOperation ($Manager.StartTetheringAsync()) $TetheringResult
    if ($result.Status.ToString() -ne 'Success') {
        throw "Could not start the hotspot: $($result.Status) $($result.AdditionalErrorMessage) (is Mobile hotspot allowed on this machine?)"
    }

    $adapter = $null
    $deadline = (Get-Date).AddSeconds(20)
    while (-not $adapter -and (Get-Date) -lt $deadline) {
        $adapter = Get-HotspotAdapter
        if (-not $adapter) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $adapter) { throw 'The hotspot started but its adapter got no IPv4 address' }
    $address = (Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 |
        Where-Object { $_.IPAddress -notlike '169.254.*' } | Select-Object -First 1).IPAddress

    Write-Step "Serving the DNS names in dns.windows.txt (hosts file)"
    $null = Sync-DnsFile { param($m) Write-Warn $m } -Force

    return @{ Adapter = $adapter; Address = $address }
}

function Stop-AccessPoint($Manager) {
    Write-Host ''
    Write-Step 'Stopping: removing the access point'
    try {
        if ($Manager -and $Manager.TetheringOperationalState.ToString() -eq 'On') {
            $null = Wait-WinRtOperation ($Manager.StopTetheringAsync()) $TetheringResult
        }
    } catch { Write-Warn "could not stop the hotspot: $($_.Exception.Message)" }
    try { Set-HostsBlock @() } catch { Write-Warn $_.Exception.Message }
    try {
        Set-ItemProperty -Path $W32TimeNtpServer -Name Enabled -Value 0 -Type DWord
        Set-ItemProperty -Path $W32TimeConfig -Name AnnounceFlags -Value 10 -Type DWord   # Windows default
        if ((Get-Service w32time).Status -eq 'Running') { Restart-Service w32time -Force }
    } catch { Write-Warn "could not turn off the NTP server: $($_.Exception.Message)" }
    Get-NetFirewallRule -Name "$FirewallPrefix*" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
    Write-Step 'Hotspot stopped; hosts entries, NTP server and firewall rules removed.'
}

# ---- banner and device log ------------------------------------------------------------------------
function Show-Banner($Cfg, [string]$Address) {
    $alertPort = Get-Setting $Cfg 'ALERT_SERVER_UDP_PORT' '8080'
    $mqttPort = Get-Setting $Cfg 'MQTT_PORT' '1883'
    function Row([string]$Label, [string]$Value) { Write-Host ('  {0,-14} {1}' -f $Label, $Value) }

    Write-Host ''
    Write-Host 'COMCS access point is up' -ForegroundColor Green -NoNewline
    Write-Host '   (Ctrl+C to stop and remove it)' -ForegroundColor DarkGray
    Write-Host ''
    Row 'Wi-Fi' "$($Cfg['AP_SSID'])  password $($Cfg['AP_PASSWORD'])  (Mobile hotspot, 2.4 GHz)"
    Row 'This host' "$Address   (DHCP by Windows)"
    Row 'NTP' "$Address udp/123 (Windows Time)"
    Row 'Firewall' "open: UDP $alertPort, TCP $mqttPort, TCP 1880, UDP 123"
    Row 'DNS' "$($script:DnsEntries.Count) names from access-point\dns.windows.txt (edit it any time: reloaded automatically)"
    foreach ($entry in $script:DnsEntries) {
        $ip, $name = $entry -split "`t", 2
        Write-Host ('  {0,-14}   {1,-22} {2}' -f '', $name, $ip)
    }
    if ($script:DnsEntries | Where-Object { ($_ -split "`t", 2)[0] -ne $Address }) {
        Write-Warn "some names in dns.windows.txt do not point at this hotspot ($Address)"
    }
    Write-Host ''
    Write-Host '  ESP32 config (nodes/sketch/src/configs/*.h):'
    Write-Host ('    {0,-16} "{1}"' -f 'wifi_ssid', $Cfg['AP_SSID'])
    Write-Host ('    {0,-16} "{1}"' -f 'wifi_password', $Cfg['AP_PASSWORD'])
    Write-Host ('    {0,-16} alert server name from dns.windows.txt, UDP {1} (run it on this machine)' -f 'server_address', $alertPort)
    Write-Host ('    {0,-16} broker name from dns.windows.txt, TCP {1} (Command Centre in Docker here)' -f 'mqtt_host', $mqttPort)
    Write-Host ''
    Write-Host 'Devices'
}

function Write-Event([string]$Text, [string]$Color) {
    Write-Host ('  {0}  ' -f (Get-Date -Format 'HH:mm:ss')) -NoNewline
    Write-Host $Text -ForegroundColor $Color
}

function Format-Duration([TimeSpan]$Span) {
    if ($Span.TotalSeconds -lt 60) { return '{0}s' -f [int]$Span.TotalSeconds }
    if ($Span.TotalHours -lt 1) { return '{0}m{1:00}s' -f [int][Math]::Floor($Span.TotalMinutes), $Span.Seconds }
    return '{0}h{1:00}m' -f [int][Math]::Floor($Span.TotalHours), $Span.Minutes
}

# dns.windows.txt edited (or created / deleted): rewrite the hosts entries and log what changed.
function Update-Dns {
    $before = @($script:DnsEntries)
    if (-not (Sync-DnsFile { param($m) Write-Event "! $m" Yellow })) { return }
    Write-Event "~ dns.windows.txt reloaded: $($script:DnsEntries.Count) names" Cyan
    foreach ($entry in $script:DnsEntries) {
        if ($before -notcontains $entry) { $ip, $name = $entry -split "`t", 2; Write-Event "    + $name  ->  $ip" Green }
    }
    foreach ($entry in $before) {
        if ($script:DnsEntries -notcontains $entry) { $ip, $name = $entry -split "`t", 2; Write-Event "    - $name  ->  $ip" Red }
    }
}

function Format-Mac([string]$Mac) { return ($Mac -replace '-', ':').ToLower() }

$script:Connected = @{}   # mac -> connected since
$script:Ips = @{}         # mac -> IPv4 address

function Update-Devices($Manager, $Adapter) {
    $now = Get-Date
    $clients = @{}
    foreach ($client in $Manager.GetTetheringClients()) {
        $names = @($client.HostNames | ForEach-Object { $_.DisplayName } | Where-Object { $_ -notmatch '^[0-9.]+$' -and $_ -notmatch ':' })
        $clients[(Format-Mac $client.MacAddress)] = ($names -join ', ')
    }

    foreach ($mac in $clients.Keys) {
        if (-not $script:Connected.ContainsKey($mac)) {
            $script:Connected[$mac] = $now
            $label = $mac
            if ($clients[$mac]) { $label += " ($($clients[$mac]))" }
            Write-Event "+ connected     $label" Green
        }
    }
    foreach ($mac in @($script:Connected.Keys)) {
        if (-not $clients.ContainsKey($mac)) {
            $ip = ''
            if ($script:Ips.ContainsKey($mac)) { $ip = "  $($script:Ips[$mac])" }
            Write-Event "- disconnected  $mac$ip  after $(Format-Duration ($now - $script:Connected[$mac]))" Red
            $script:Connected.Remove($mac)
            $script:Ips.Remove($mac)
        }
    }

    # The client's IPv4 address, from the hotspot adapter's neighbour (ARP) table.
    if ($Adapter) {
        $neighbours = Get-NetNeighbor -InterfaceIndex $Adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.State -ne 'Unreachable' -and $_.State -ne 'Incomplete' -and $_.LinkLayerAddress }
        foreach ($n in $neighbours) {
            $mac = Format-Mac $n.LinkLayerAddress
            if ($script:Connected.ContainsKey($mac) -and $script:Ips[$mac] -ne $n.IPAddress) {
                $script:Ips[$mac] = $n.IPAddress
                Write-Event "= address       $mac  ->  $($n.IPAddress)" Cyan
            }
        }
    }
}

# ---- main -----------------------------------------------------------------------------------------
$cfg = Read-ApEnv
if ($cfg['AP_PASSWORD'].Length -lt 8) { throw 'AP_PASSWORD must have at least 8 characters (access-point\ap.env)' }

$manager = Get-TetheringManager
try {
    $ap = Start-AccessPoint $cfg $manager
    Show-Banner $cfg $ap.Address
    $ticks = 0
    while ($true) {
        Update-Devices $manager $ap.Adapter
        try { Update-Dns } catch { Write-Event "! could not apply dns.windows.txt: $($_.Exception.Message)" Red }
        $ticks++
        if ($ticks % 5 -eq 0 -and $manager.TetheringOperationalState.ToString() -ne 'On') {
            # Windows turns the hotspot off by itself (e.g. power saving with no clients): restart it.
            Write-Event '! hotspot was turned off; restarting it' Yellow
            $null = Wait-WinRtOperation ($manager.StartTetheringAsync()) $TetheringResult
            $ap.Adapter = Get-HotspotAdapter
        }
        Start-Sleep -Seconds $PollSeconds
    }
} finally {
    # Runs on Ctrl+C and on errors. (Closing the window with X skips it: run the program again
    # and stop it with Ctrl+C to clean up.)
    Stop-AccessPoint $manager
}
