<#
Build the alert server natively on Windows with MSVC (Visual Studio 2022/2026 or its Build
Tools, "Desktop development with C++"). Uses the CMake that ships with Visual Studio when
cmake is not on PATH. Output: server\build-windows\bin\Release\alert_server.exe

Usage: server\scripts\windows\build.cmd [-Clean]
#>
param([switch]$Clean)
$ErrorActionPreference = 'Stop'

$ServerDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$RepoDir = (Resolve-Path (Join-Path $ServerDir '..')).Path
$BuildDir = Join-Path $ServerDir 'build-windows'

function Find-CMake {
    $onPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) {
            $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $cmake) { return $cmake }
        }
    }
    throw 'CMake not found. Install Visual Studio Build Tools with "Desktop development with C++".'
}

$cmake = Find-CMake
Write-Host "==> Using $cmake" -ForegroundColor Blue

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }

if (Get-Command git -ErrorAction SilentlyContinue) {
    Write-Host '==> Updating submodules' -ForegroundColor Blue
    git -C $RepoDir submodule update --init --recursive
}

Write-Host '==> Configuring' -ForegroundColor Blue
& $cmake -S $ServerDir -B $BuildDir -A x64 -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host '==> Building' -ForegroundColor Blue
& $cmake --build $BuildDir --config Release --target alert_server --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

$exe = Join-Path $BuildDir 'bin\Release\alert_server.exe'
Write-Host "==> Built $exe" -ForegroundColor Blue
Write-Host '    Run it with server\scripts\windows\run.cmd'
