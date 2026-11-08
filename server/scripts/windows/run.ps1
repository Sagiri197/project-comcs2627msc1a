<#
Run the natively built alert server in this console (Ctrl+C to stop). It listens on UDP 8080
on every address of this machine, including the hotspot from up.cmd. The readings database
(readings.sqlite3 + WAL files) is kept in server\build-windows\data.

Usage: server\scripts\windows\run.cmd      (build first with build.cmd)
#>
$ErrorActionPreference = 'Stop'

$ServerDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = Join-Path $ServerDir 'build-windows'
$exe = Join-Path $BuildDir 'bin\Release\alert_server.exe'
$DataDir = Join-Path $BuildDir 'data'

if (-not (Test-Path $exe)) { throw "$exe is missing. Run server\scripts\windows\build.cmd first." }

# Only one process can own UDP 8080 (e.g. the WSL copy started by up.sh).
$owner = Get-NetUDPEndpoint -LocalPort 8080 -ErrorAction SilentlyContinue | Select-Object -First 1
if ($owner) {
    $name = (Get-Process -Id $owner.OwningProcess -ErrorAction SilentlyContinue).ProcessName
    throw "UDP 8080 is already in use by $name (pid $($owner.OwningProcess)). Stop it first."
}
# With WSL mirrored networking, a server in WSL holds the port without Windows listing it.
if (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
    $inWsl = wsl.exe -e sh -c "ss -Hlun 'sport = :8080' 2>/dev/null" 2>$null
    if ("$inWsl".Trim()) {
        throw 'UDP 8080 is held by the alert server in WSL. Stop it: wsl -u root systemctl stop comcs-alert-server (add "disable" to keep it off after reboots).'
    }
}

# The server logs UTF-8 (°C); make the console show it.
[Console]::OutputEncoding = [Text.Encoding]::UTF8

New-Item -ItemType Directory -Force $DataDir | Out-Null
Write-Host "==> $exe (database in $DataDir; Ctrl+C to stop)" -ForegroundColor Blue
Push-Location $DataDir
try { & $exe } finally { Pop-Location }
