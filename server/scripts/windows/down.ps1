<#
Undo up.ps1: stop the hotspot, give it back Windows' default address (192.168.137.1), remove
the hosts entries, turn off the Windows Time NTP server and remove the COMCS firewall rules.

Usage: server\scripts\windows\down.cmd      (asks for administrator rights)
#>
. "$PSScriptRoot\common.ps1"
Invoke-Elevated $PSCommandPath @()

$manager = Get-TetheringManager
if ($manager.TetheringOperationalState.ToString() -eq 'On') {
    Write-Step 'Stopping the hotspot'
    $null = Wait-WinRtOperation ($manager.StopTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
}

Write-Step "Hotspot address -> $IcsDefaultAddress (Windows default)"
Set-ItemProperty -Path $IcsParams -Name ScopeAddress -Value $IcsDefaultAddress -Type String
Set-ItemProperty -Path $IcsParams -Name ScopeAddressBackup -Value $IcsDefaultAddress -Type String
Restart-IcsService

Write-Step 'Removing the comcs hosts entries'
Set-HostsBlock @()

Write-Step 'Turning off the Windows Time NTP server'
Set-ItemProperty -Path $W32TimeNtpServer -Name Enabled -Value 0 -Type DWord
Set-ItemProperty -Path $W32TimeConfig -Name AnnounceFlags -Value 10 -Type DWord   # Windows default
if ((Get-Service w32time).Status -eq 'Running') { Restart-Service w32time -Force }

Write-Step 'Removing the COMCS firewall rules'
Get-NetFirewallRule -Name "$FirewallPrefix*" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
if (Get-Command Get-NetFirewallHyperVRule -ErrorAction SilentlyContinue) {
    Get-NetFirewallHyperVRule -Name "$FirewallPrefix*" -ErrorAction SilentlyContinue | Remove-NetFirewallHyperVRule
}

Write-Step 'Done. The hotspot SSID/password stay as configured; change them in Settings > Mobile hotspot.'
