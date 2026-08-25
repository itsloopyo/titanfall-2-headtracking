#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Template: <mod>/scripts/update-deps.ps1
# ============================================================================
# Bumps the vendored mod-loader copies under <mod>/vendor/<slug>/ to the
# latest upstream release within the pinned version range, and writes
# refreshed LICENSE + README.md sidecar metadata.
#
# Usage:    pixi run update-deps
# Frequency: manual. Vendored copies are the single source of truth at
# install time, so the dev runs this whenever they want a fresh upstream
# bump, then commits the updated vendor/ tree. CI does not refresh.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$moduleCandidates = @(
    (Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'),
    (Join-Path $projectDir '../cameraunlock-core/powershell/ModLoaderSetup.psm1')
)
$modulePath = $moduleCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $modulePath) {
    throw "ModLoaderSetup.psm1 not found. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $modulePath -Force

# --- CALL BLOCK ----------------------------------------------------------
# Titanfall 2 is 64-bit Source Engine -> Ultimate ASI Loader x64. The x64
# release asset (Ultimate-ASI-Loader_x64.zip) is a wrapper zip containing a
# single x64 dinput8.dll. Update-VendoredLoader cannot unwrap it, so it lands
# as vendor/ultimate-asi-loader/dinput8.dll *as a zip*; we extract the real DLL
# below. install.cmd renames the bundled dinput8.dll to dsound.dll in the
# game's exe directory.
$vendorDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
Update-VendoredLoader `
    -Name 'ultimate-asi-loader' `
    -OutputDir $vendorDir `
    -OutputFileName 'dinput8.dll' `
    -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
    -VersionPrefix 'v9.' `
    -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$' `
    -LicenseUrl 'https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/master/license' | Out-Null

# Unwrap: the saved dinput8.dll is the wrapper zip. Replace it with the x64 DLL
# it contains.
$saved = Join-Path $vendorDir 'dinput8.dll'
$bytes = [System.IO.File]::ReadAllBytes($saved)
if ($bytes.Length -ge 2 -and $bytes[0] -eq 0x50 -and $bytes[1] -eq 0x4B) {
    $tmp = Join-Path $vendorDir '_extract'
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    New-Item -ItemType Directory -Path $tmp | Out-Null
    $zipCopy = Join-Path $tmp 'loader.zip'
    Copy-Item $saved $zipCopy
    Expand-Archive -Path $zipCopy -DestinationPath $tmp -Force
    $dll = Get-ChildItem -Path $tmp -Recurse -Filter 'dinput8.dll' |
           Where-Object { $_.FullName -ne $zipCopy } | Select-Object -First 1
    if (-not $dll) { throw "x64 dinput8.dll not found inside Ultimate-ASI-Loader_x64.zip" }
    Copy-Item $dll.FullName $saved -Force
    Remove-Item $tmp -Recurse -Force
    Write-Host "Unwrapped x64 dinput8.dll from the release zip." -ForegroundColor Green
}

# The module's SHA-256 line covers the wrapper zip, which is not what we commit.
# Record the unwrapped DLL's hash too, so the committed artifact is verifiable.
$readme  = Join-Path $vendorDir 'README.md'
$dllHash = (Get-FileHash -Path $saved -Algorithm SHA256).Hash.ToLowerInvariant()
$lines   = Get-Content $readme
$anchor  = ($lines | Select-String -SimpleMatch '- SHA-256:' | Select-Object -First 1).LineNumber
if (-not $anchor) { throw "vendor README.md has no '- SHA-256:' line to anchor the DLL hash after." }
$lines = @($lines[0..($anchor - 1)]) + "- dinput8.dll SHA-256: ``$dllHash``" + @($lines[$anchor..($lines.Count - 1)])
Set-Content -Path $readme -Value $lines -Encoding utf8

# --- END CALL BLOCK ------------------------------------------------------

Write-Host ""
Write-Host "Vendored dependencies refreshed. Review and commit the changes under vendor/." -ForegroundColor Green
