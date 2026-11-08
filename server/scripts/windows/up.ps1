<#
Make this Windows machine stand in for the Pi's network, to test without the Pi:
  - Mobile hotspot with the cell's SSID / password (AP_SSID, AP_PASSWORD in server\pi.env), 2.4 GHz
  - the hotspot address moved from Windows' 192.168.137.1 to the Pi's AP_ADDRESS (192.168.50.1)
  - AP_DNS_NAME and COMMAND_CENTRE_DNS_NAME (comcs.lan, cc.comcs.lan) resolve to it, for this
    machine and for hotspot clients (Windows' hotspot DNS answers from the hosts file)
  - NTP server (Windows Time) on udp/123, and the public NTP names the ESP32s use point to it
  - firewall rules for UDP 8080 (alert server), TCP 1883 (MQTT), TCP 1880 (Node-RED), UDP 123

Both roles live on this machine, so the ESP32 configs work unchanged: server_address =
comcs.lan, mqtt_host = cc.comcs.lan. Run the alert server in WSL (mirrored networking) or
Docker, and the Command Centre with Docker. Undo everything with down.cmd.

Usage: server\scripts\windows\up.cmd [-NoNtp]      (asks for administrator rights)
#>
param([switch]$NoNtp)

. "$PSScriptRoot\common.ps1"
Invoke-Elevated $PSCommandPath $(if ($NoNtp) { @('-NoNtp') } else { @() })

$cfg = Read-PiEnv
$address = $cfg['AP_ADDRESS']
if ($cfg['AP_PASSWORD'].Length -lt 8) { throw 'AP_PASSWORD must have at least 8 characters' }

$manager = Get-TetheringManager

# 1. Stop the hotspot if it runs, so the new address and settings take effect.
if ($manager.TetheringOperationalState.ToString() -eq 'On') {
    Write-Step 'Stopping the running hotspot'
    $null = Wait-WinRtOperation ($manager.StopTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
}

# 2. Hotspot address: Internet Connection Sharing reads it when it starts.
Write-Step "Hotspot address -> $address (was $((Get-ItemProperty $IcsParams -ErrorAction SilentlyContinue).ScopeAddress))"
Set-ItemProperty -Path $IcsParams -Name ScopeAddress -Value $address -Type String
Set-ItemProperty -Path $IcsParams -Name ScopeAddressBackup -Value $address -Type String
Restart-IcsService

# 3. SSID, password and band (ESP32s only do 2.4 GHz).
Write-Step "Configuring hotspot '$($cfg['AP_SSID'])'"
$ap = $manager.GetCurrentAccessPointConfiguration()
$ap.Ssid = $cfg['AP_SSID']
$ap.Passphrase = $cfg['AP_PASSWORD']
try {
    $ap.Band = [Windows.Networking.NetworkOperators.TetheringWiFiBand]::TwoPointFourGigahertz
} catch {
    Write-Warn 'could not force 2.4 GHz (needs Windows 11); set Band = 2.4 GHz in Settings > Mobile hotspot'
}
Wait-WinRtAction ($manager.ConfigureAccessPointAsync($ap))

# 4. Names. Windows serves both roles here, so both names point to the hotspot address.
$names = @($cfg['AP_DNS_NAME'])
if ($cfg['COMMAND_CENTRE_DNS_NAME']) { $names += $cfg['COMMAND_CENTRE_DNS_NAME'] }
if (-not $NoNtp -and $cfg['NTP_HOSTNAMES']) {
    # Skip time.windows.com: this machine syncs its own clock from it.
    $names += @($cfg['NTP_HOSTNAMES'] -split '\s+' | Where-Object { $_ -and $_ -ne 'time.windows.com' })
}
Write-Step "hosts file: $($names -join ', ') -> $address"
Set-HostsBlock @($names | ForEach-Object { "$address`t$_" })

# 5. NTP server (Windows Time), always announced as reliable so it answers without Internet.
if (-not $NoNtp) {
    Write-Step 'Enabling the Windows Time NTP server (udp/123)'
    Set-ItemProperty -Path $W32TimeNtpServer -Name Enabled -Value 1 -Type DWord
    Set-ItemProperty -Path $W32TimeConfig -Name AnnounceFlags -Value 5 -Type DWord
    Set-Service w32time -StartupType Automatic
    if ((Get-Service w32time).Status -eq 'Running') { Restart-Service w32time -Force } else { Start-Service w32time }
    w32tm /config /update | Out-Null
}

# 6. Firewall (the hotspot network is usually classified as Public).
Write-Step 'Firewall rules'
$rules = @(
    @{ Name = 'UDP-8080'; Protocol = 'UDP'; Port = 8080; Text = 'alert server' },
    @{ Name = 'TCP-1883'; Protocol = 'TCP'; Port = 1883; Text = 'MQTT broker' },
    @{ Name = 'TCP-1880'; Protocol = 'TCP'; Port = 1880; Text = 'Node-RED' }
)
if (-not $NoNtp) { $rules += @{ Name = 'UDP-123'; Protocol = 'UDP'; Port = 123; Text = 'NTP' } }
foreach ($rule in $rules) {
    $name = $FirewallPrefix + $rule.Name
    if (-not (Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue)) {
        $null = New-NetFirewallRule -Name $name -DisplayName "COMCS $($rule.Text) ($($rule.Protocol) $($rule.Port))" `
            -Direction Inbound -Action Allow -Protocol $rule.Protocol -LocalPort $rule.Port -Profile Any
    }
    # WSL in mirrored networking mode is behind the Hyper-V firewall as well.
    if ((Get-Command New-NetFirewallHyperVRule -ErrorAction SilentlyContinue) -and
        -not (Get-NetFirewallHyperVRule -Name $name -ErrorAction SilentlyContinue)) {
        $null = New-NetFirewallHyperVRule -Name $name -DisplayName "COMCS $($rule.Text) (WSL)" -Direction Inbound `
            -VMCreatorId $WslVmCreatorId -Protocol $rule.Protocol -LocalPorts $rule.Port -Action Allow
    }
}

# 7. Start the hotspot.
Write-Step 'Starting the hotspot'
$result = Wait-WinRtOperation ($manager.StartTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
if ($result.Status.ToString() -ne 'Success') {
    throw "Could not start the hotspot: $($result.Status) $($result.AdditionalErrorMessage) (is Mobile hotspot allowed on this machine?)"
}

$deadline = (Get-Date).AddSeconds(20)
while (-not (Get-NetIPAddress -IPAddress $address -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
}
if (Get-NetIPAddress -IPAddress $address -ErrorAction SilentlyContinue) {
    Write-Step "Hotspot '$($cfg['AP_SSID'])' is up on $address"
} else {
    $actual = Get-NetIPAddress -IPAddress $IcsDefaultAddress -ErrorAction SilentlyContinue
    if ($actual) {
        Write-Warn "the hotspot still uses ${IcsDefaultAddress}: Windows only re-reads the address after a reboot here. Reboot, then run up.cmd again."
    } else {
        Write-Warn "the hotspot started but $address did not appear; check with status.cmd"
    }
}

& "$PSScriptRoot\status.ps1"
