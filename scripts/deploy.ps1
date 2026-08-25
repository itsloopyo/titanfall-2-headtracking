#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built Titanfall2HeadTracking.asi to the game directory for local
# testing. scripts/install.cmd is the release-ZIP installer and reads its
# payload from a sibling plugins/ folder that only exists inside the packaged
# ZIP, so the dev tree deploys straight from bin/<Configuration>.
#
# Usage: deploy.ps1 [Debug|Release] [GamePath]
# Defaults to Debug. An explicit GamePath wins over auto-detection
# (same contract as install.cmd).

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$asi = Join-Path $projectDir "bin/$Configuration/Titanfall2HeadTracking.asi"
if (-not (Test-Path $asi)) {
    throw "Build output not found: $asi. Run 'pixi run build' or 'pixi run build-release' first."
}

if ($GamePath) {
    if (-not (Test-Path $GamePath)) {
        throw "Explicit game path does not exist: $GamePath"
    }
    $gamePath = $GamePath
} else {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $gamePath = Find-GamePath -GameId 'titanfall-2'
    if (-not $gamePath) {
        throw "Could not locate Titanfall 2. Set TITANFALL_2_PATH, install via Steam, or pass the game path: deploy.ps1 $Configuration <path>"
    }
}

if (Get-Process -Name 'Titanfall2' -ErrorAction SilentlyContinue) {
    throw "Titanfall 2 is running - the loaded .asi is locked. Close the game and re-run."
}

# Titanfall2.exe sits at the game root, so the loader and .asi land there too.
# Titanfall 2 imports dsound.dll, so the loader is renamed to that.
$loader = Join-Path $gamePath 'dsound.dll'
$installedLoader = $false
if (-not (Test-Path $loader)) {
    $vendored = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
    if (-not (Test-Path $vendored)) {
        throw "Vendored Ultimate ASI Loader not found at $vendored. Run 'pixi run update-deps' to fetch it."
    }
    Copy-Item $vendored -Destination $loader -Force
    $installedLoader = $true
    Write-Host "Deployed: $vendored -> $loader" -ForegroundColor Green
}

Copy-Item $asi -Destination $gamePath -Force
Write-Host "Deployed: $asi -> $gamePath" -ForegroundColor Green

# uninstall.cmd only removes the loader when this file says we put it there, so
# a dev deploy that installs the loader has to record it or `pixi run uninstall`
# leaves a dsound.dll behind proxying nothing.
$statePath = Join-Path $gamePath '.headtracking-state.json'
$installedByUs = $installedLoader
if ((-not $installedByUs) -and (Test-Path $statePath)) {
    $installedByUs = (Get-Content $statePath -Raw | ConvertFrom-Json).framework.installed_by_us
}
$versionMatch = Select-String -Path (Join-Path $projectDir 'CMakeLists.txt') `
    -Pattern 'project\(Titanfall2HeadTracking VERSION ([0-9]+\.[0-9]+\.[0-9]+)'
if (-not $versionMatch) {
    throw "Could not read the project version from CMakeLists.txt."
}
[ordered]@{
    schema_version = 1
    framework      = [ordered]@{ type = 'ASILoader'; installed_by_us = [bool]$installedByUs }
    mod            = [ordered]@{
        id      = 'titanfall-2'
        name    = 'Titanfall2HeadTracking'
        version = $versionMatch.Matches[0].Groups[1].Value
    }
} | ConvertTo-Json -Depth 3 | ForEach-Object {
    # Windows PowerShell's -Encoding UTF8 writes a BOM, and install.cmd's `echo >`
    # form does not. The launcher's legacy-state import parses this file strictly.
    [System.IO.File]::WriteAllText($statePath, $_, (New-Object System.Text.UTF8Encoding $false))
}
Write-Host "Wrote: $statePath" -ForegroundColor Green
