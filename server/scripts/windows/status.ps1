<#
Show the Windows stand-in for the Pi's network: hotspot, address, clients, names, NTP and
which COMCS ports have something listening. Does not need administrator rights.

Usage: server\scripts\windows\status.cmd
#>
. "$PSScriptRoot\common.ps1"

function Show([string]$Label, [string]$Value, [bool]$Ok = $true) {
    $color = 'Green'
    if (-not $Ok) { $color = 'Red' }
    Write-Host ('  {0,-22} ' -f $Label) -NoNewline
    Write-Host $Value -ForegroundColor $color
}

$cfg = Read-PiEnv
$address = $cfg['AP_ADDRESS']

Write-Host ''
Write-Host 'Hotspot'
try {
    $manager = Get-TetheringManager
    $state = $manager.TetheringOperationalState.ToString()
    $ap = $manager.GetCurrentAccessPointConfiguration()
    $band = '?'
    try { $band = $ap.Band.ToString() } catch { }
    Show 'state' $state ($state -eq 'On')
    Show 'SSID / band' "$($ap.Ssid) / $band" (($ap.Ssid -eq $cfg['AP_SSID']) -and ($band -ne 'FiveGigahertz'))
    Show 'clients' "$($manager.ClientCount) of $($manager.MaxClientCount)"
    foreach ($client in $manager.GetTetheringClients()) {
        $names = @($client.HostNames | ForEach-Object { $_.DisplayName }) -join ', '
        Write-Host "    $($client.MacAddress)  $names"
    }
} catch {
    Show 'state' "unavailable: $($_.Exception.Message)" $false
}
$scope = (Get-ItemProperty $IcsParams -ErrorAction SilentlyContinue).ScopeAddress
Show 'configured address' "$scope" ($scope -eq $address)
$live = Get-NetIPAddress -IPAddress $address -ErrorAction SilentlyContinue
if ($live) { Show 'address in use' "$address on $($live.InterfaceAlias)" } else { Show 'address in use' "$address not assigned" $false }

Write-Host 'Names (hosts file)'
$block = @(Get-HostsBlock)
if ($block.Count -eq 0) { Show 'entries' 'none (run up.cmd)' $false }
foreach ($line in $block) { Write-Host "    $line" }

Write-Host 'Services on this machine'
$ntpOn = (Get-ItemProperty $W32TimeNtpServer -ErrorAction SilentlyContinue).Enabled -eq 1
Show 'NTP server (w32time)' $(if ($ntpOn) { "enabled, service $((Get-Service w32time).Status)" } else { 'disabled' }) $ntpOn
$udp = @(Get-NetUDPEndpoint -ErrorAction SilentlyContinue)
$tcp = @(Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue)
Show 'alert server udp/8080' $(if ($udp | Where-Object LocalPort -eq 8080) { 'listening' } else { 'nothing listening (WSL/Docker not running?)' }) ([bool]($udp | Where-Object LocalPort -eq 8080))
Show 'MQTT broker tcp/1883' $(if ($tcp | Where-Object LocalPort -eq 1883) { 'listening' } else { 'nothing listening (docker compose up?)' }) ([bool]($tcp | Where-Object LocalPort -eq 1883))
Show 'Node-RED tcp/1880' $(if ($tcp | Where-Object LocalPort -eq 1880) { 'listening' } else { 'nothing listening' }) ([bool]($tcp | Where-Object LocalPort -eq 1880))
$rules = @(Get-NetFirewallRule -Name "$FirewallPrefix*" -ErrorAction SilentlyContinue)
Show 'firewall rules' "$($rules.Count) COMCS rules" ($rules.Count -gt 0)

Write-Host ''
Write-Host "ESP32 configs: server_address = $($cfg['AP_DNS_NAME']), mqtt_host = $($cfg['COMMAND_CENTRE_DNS_NAME']) (both = $address here)"
