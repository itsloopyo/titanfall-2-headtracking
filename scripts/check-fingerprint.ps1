# Prints the installed client.dll and engine.dll PE fingerprints as a
# paste-ready build-profile stub. Run this first whenever the game updates or a
# user reports the "no profile matches - staying dormant" log line.
#
#   pixi run check-fingerprint
#   pixi run check-fingerprint -- -GamePath "D:\Games\Titanfall2"
#
# A stub with its offsets left at zero is safe to commit: the mod recognises the
# build and stays dormant on it (see SelectProfile), so a new build can be named
# before anyone has rederived a single rva.

[CmdletBinding()]
param([string]$GamePath)

$ErrorActionPreference = 'Stop'

# Resolved, not guessed. This is the first thing a user runs when they report
# the dormant log line, and a hardcoded C:\Program Files (x86)\Steam path fails
# for everyone whose Steam library is on another drive - which is most people
# with a second drive, and exactly the population most likely to be reporting.
if (-not $GamePath) {
    $detection = Join-Path $PSScriptRoot '..\cameraunlock-core\powershell\GamePathDetection.psm1'
    if (-not (Test-Path $detection)) {
        throw "GamePathDetection.psm1 not found. Run 'pixi run sync' to fetch the cameraunlock-core submodule, or pass -GamePath <your Titanfall 2 folder>."
    }
    Import-Module $detection -Force
    $GamePath = Find-GamePath -GameId 'titanfall-2'
    if (-not $GamePath) {
        throw "Could not find your Titanfall 2 install. Pass -GamePath <your Titanfall 2 folder>."
    }
}

function Get-PeFingerprint {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        throw "not found: $Path (pass -GamePath <your Titanfall 2 folder>)"
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
        throw "not a PE image: $Path"
    }
    $fileHeader = $peOffset + 4
    $optional = $fileHeader + 20
    [pscustomobject]@{
        Path          = $Path
        TimeDateStamp = [BitConverter]::ToUInt32($bytes, $fileHeader + 4)
        SizeOfImage   = [BitConverter]::ToUInt32($bytes, $optional + 56)
        CheckSum      = [BitConverter]::ToUInt32($bytes, $optional + 64)
    }
}

$client = Get-PeFingerprint (Join-Path $GamePath 'bin\x64_retail\client.dll')
$engine = Get-PeFingerprint (Join-Path $GamePath 'bin\x64_retail\engine.dll')

$buildTxt = Join-Path $GamePath 'build.txt'
$build = if (Test-Path $buildTxt) { (Get-Content $buildTxt -Raw).Trim() } else { 'unknown' }
$date = [DateTimeOffset]::FromUnixTimeSeconds($client.TimeDateStamp).UtcDateTime

Write-Host "client.dll : $($client.Path)"
Write-Host "engine.dll : $($engine.Path)"
Write-Host "build.txt  : $build"
Write-Host ("linked     : {0:yyyy-MM-dd HH:mm:ss} UTC" -f $date)
Write-Host ''
Write-Host 'Paste into src/build_profile.cpp, ABOVE the existing entries, and fill in'
Write-Host 'the offsets (see .lab/NOTES.md for how they are rederived):'
Write-Host ''
Write-Host ("constexpr BuildProfile kSteamProfile_{0:yyyyMMdd} = {{" -f $date)
Write-Host ("    `"steam-win64-{0:yyyyMMdd}`"," -f $date)
Write-Host ("    {{ 0x{0:X8}u, 0x{1:X8}u, 0x{2:X8}u }},   // client.dll" -f `
    $client.TimeDateStamp, $client.SizeOfImage, $client.CheckSum)
Write-Host ("    {{ 0x{0:X8}u, 0x{1:X8}u, 0x{2:X8}u }},   // engine.dll" -f `
    $engine.TimeDateStamp, $engine.SizeOfImage, $engine.CheckSum)
# Field names, not positions: the table is twenty-one uint32_t in a row, and
# transposing two of them compiles clean and presents as a feature that silently
# does nothing.
#
# Every field in OffsetTable is listed, including the ones ProfileComplete does
# not gate on. A stub that omits a field still compiles - C++20 leaves an
# unnamed member value-initialised - so an omission here is a profile that goes
# live with a silently zeroed offset. Keep this list in step with
# src/build_profile.h.
Write-Host '    {'
Write-Host '        .render_view_rva      = 0u,'
Write-Host '        .origin               = 0u,'
Write-Host '        .basis                = 0u,   // rows forward/right/up, 4-float stride'
Write-Host '        .view_matrix          = 0u,'
Write-Host '        .proj_matrix          = 0u,'
Write-Host '        .viewproj_matrix      = 0u,'
Write-Host '        .tan_fov              = 0u,   // tan(fovX/2), tan(fovY/2), znear'
Write-Host '        .zoom                 = 0u,   // base FOV tangent / this frame''s'
Write-Host '        .world_view           = 0u,   // CViewRender + this: world view'
Write-Host '        .skybox_view          = 0u,   // CViewRender + this: 3D skybox'
Write-Host '        .main_view            = 0u,   // CViewRender + this: RenderView''s arg'
Write-Host '        .engine_client_ptr    = 0u,'
Write-Host '        .get_level_name_slot  = 0u,'
Write-Host '        .engine_paused_flag   = 0u,   // engine.dll rva, NOT client.dll'
Write-Host '        .crosshair_args_rva   = 0u,   // fills one crosshair RUI instance'
Write-Host '        .rui_instance_size    = 0u,   // (width, height) on the instance'
Write-Host '        .rui_instance_transform = 0u, // instance -> shared transform'
Write-Host '        .rui_transform_origin = 0u,   // crosshair position within it'
Write-Host '        .crosshair_state      = 0u,   // CROSSHAIR_STATE_* global'
Write-Host '        .cvar_interface_ptr   = 0u,'
Write-Host '        .find_var_slot        = 0u,   // ICvar::FindVar'
Write-Host '        .convar_float         = 0u,   // ConVar::m_fValue'
Write-Host '    },'
Write-Host '};'
