# Shared helpers for the Windows test scripts. Dot-sourced by up.ps1 / down.ps1 / status.ps1.
# Windows PowerShell 5.1 compatible (no ternary, no ??).

$ErrorActionPreference = 'Stop'

$ServerDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path   # server\ (pi.env lives here)
$HostsFile = Join-Path $env:windir 'System32\drivers\etc\hosts'
$HostsBegin = '# BEGIN comcs (server/scripts/windows)'
$HostsEnd = '# END comcs'
$IcsParams = 'HKLM:\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters'
$IcsDefaultAddress = '192.168.137.1'                                   # Windows' own hotspot default
$W32TimeNtpServer = 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\TimeProviders\NtpServer'
$W32TimeConfig = 'HKLM:\SYSTEM\CurrentControlSet\Services\W32Time\Config'
$FirewallPrefix = 'COMCS-Win-'
$WslVmCreatorId = '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}'           # fixed id of WSL for Hyper-V firewall rules

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Blue }
function Write-Warn([string]$Message) { Write-Host "warning: $Message" -ForegroundColor Yellow }

function Test-Admin {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Re-launch the calling script elevated (UAC prompt) and stop this copy.
function Invoke-Elevated([string]$ScriptPath, [string[]]$Arguments) {
    if (Test-Admin) { return }
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', "`"$ScriptPath`"") + $Arguments
    Start-Process powershell -Verb RunAs -ArgumentList $argList
    exit
}

# KEY=VALUE settings from server\pi.env, overridden by server\pi.env.local (same format as bash).
function Read-PiEnv {
    $settings = @{}
    foreach ($name in @('pi.env', 'pi.env.local')) {
        $path = Join-Path $ServerDir $name
        if (-not (Test-Path $path)) { continue }
        foreach ($line in Get-Content $path) {
            if ($line -notmatch '^\s*([A-Za-z_][A-Za-z0-9_]*)=(.*)$') { continue }
            $key = $Matches[1]
            $raw = $Matches[2]
            if ($raw -match '^"([^"]*)"') { $value = $Matches[1] }
            elseif ($raw -match "^'([^']*)'") { $value = $Matches[1] }
            else { $value = ($raw -replace '\s+#.*$', '').Trim() }
            $settings[$key] = $value
        }
    }
    foreach ($required in @('AP_SSID', 'AP_PASSWORD', 'AP_ADDRESS', 'AP_DNS_NAME')) {
        if (-not $settings.ContainsKey($required)) { throw "$required is missing from $ServerDir\pi.env" }
    }
    return $settings
}

# ICS reads ScopeAddress when sharing starts, which the hotspot start below triggers anyway.
# Restarting the service is only a best effort: Windows often refuses to stop it.
function Restart-IcsService {
    if ((Get-Service SharedAccess).Status -ne 'Running') { return }
    try {
        Restart-Service SharedAccess -Force -ErrorAction Stop
    } catch {
        Write-Warn 'could not restart Internet Connection Sharing (normal on some machines); the hotspot picks up the address when it starts'
    }
}

# ---- WinRT (Mobile hotspot API) ----------------------------------------------------------------
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]

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

# ---- hosts file ---------------------------------------------------------------------------------
function Get-HostsWithoutBlock {
    $lines = @(Get-Content $HostsFile -ErrorAction SilentlyContinue)
    $result = New-Object System.Collections.Generic.List[string]
    $inBlock = $false
    foreach ($line in $lines) {
        if ($line -eq $HostsBegin) { $inBlock = $true; continue }
        if ($line -eq $HostsEnd) { $inBlock = $false; continue }
        if (-not $inBlock) { $result.Add($line) }
    }
    # drop trailing blank lines left behind by an earlier block
    while ($result.Count -gt 0 -and $result[$result.Count - 1].Trim() -eq '') { $result.RemoveAt($result.Count - 1) }
    return , $result
}

function Set-HostsBlock([string[]]$Entries) {
    $lines = Get-HostsWithoutBlock
    if ($Entries.Count -gt 0) {
        $lines.Add('')
        $lines.Add($HostsBegin)
        foreach ($entry in $Entries) { $lines.Add($entry) }
        $lines.Add($HostsEnd)
    }
    Set-Content -Path $HostsFile -Value $lines -Encoding ASCII
    ipconfig /flushdns | Out-Null
}

function Get-HostsBlock {
    $inBlock = $false
    foreach ($line in @(Get-Content $HostsFile -ErrorAction SilentlyContinue)) {
        if ($line -eq $HostsBegin) { $inBlock = $true; continue }
        if ($line -eq $HostsEnd) { break }
        if ($inBlock) { $line }
    }
}
