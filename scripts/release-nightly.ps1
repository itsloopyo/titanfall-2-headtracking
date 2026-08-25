[CmdletBinding()]
param([switch]$AllowDirty)

$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$cmakeLists = Get-Content (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(Titanfall2HeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]

Publish-NightlyBuild `
    -ModId 'titanfall-2' `
    -ModName 'Titanfall2HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build-release' `
    -AllowDirty:$AllowDirty
