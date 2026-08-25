#!/usr/bin/env pwsh
#Requires -Version 5.1
# Fully unattended release workflow for Titanfall2HeadTracking.
# Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
#
# Running this command IS the authorization. There is no second gate: the
# release runs end to end with zero prompts. The preconditions below (clean
# tree, on main, tag absent, valid semver) are the safety net in place of
# any interactive confirmation - each fails fast with a non-zero exit.

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$Version,
    [switch]$AllowDirty,
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    exit $LASTEXITCODE
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

function Write-NoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# --- 1. Resolve and validate the target version ------------------------
$cmakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$cmakeText = Get-Content $cmakePath -Raw
if ($cmakeText -notmatch 'project\(Titanfall2HeadTracking VERSION (\d+\.\d+\.\d+)') {
    Write-Error "Could not parse current version from CMakeLists.txt"
    exit 1
}
$current = $Matches[1]

try {
    $target = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Error "Error: $($_.Exception.Message)"
    exit 1
}

$tag = "v$target"
$changelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'

# --- 2. Preconditions (these stand in for interactive confirmation) ----
$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Error "Releases must run on 'main' (currently on '$branch')."
    exit 1
}

if (-not $AllowDirty) {
    $status = git -C $ProjectRoot status --porcelain
    if ($status) {
        Write-Error "Working tree is not clean. Commit or stash changes before releasing."
        exit 1
    }
}

if (git -C $ProjectRoot tag --list $tag) {
    Write-Error "Tag $tag already exists."
    exit 1
}

Write-Host "Releasing $current -> $target" -ForegroundColor Cyan

# --- 3. Changelog from commits since the last tag ----------------------
# This is the gate that aborts when there are no user-facing commits, so run
# it BEFORE mutating any version files or building - a failure here then
# leaves a clean tree instead of stranding a half-applied version bump with
# no tag.
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$hasExistingTags = git -C $ProjectRoot tag -l 2>$null
if (-not $hasExistingTags) {
    if (-not (Test-Path $changelogPath)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        Set-Content $changelogPath "# Changelog`n`n## [$target] - $date`n`nFirst release.`n"
        Write-Host "  Wrote initial CHANGELOG.md" -ForegroundColor Gray
    }
} else {
    try {
        $changelogArgs = @{
            ChangelogPath = $changelogPath
            Version       = $target
            ArtifactPaths = @(
                'src/',
                'cameraunlock-core',
                'scripts/install.cmd',
                'scripts/uninstall.cmd'
            )
        }
        New-ChangelogFromCommits @changelogArgs | Out-Null
    } catch {
        if (-not $Force) {
            Write-Error "Error: $($_.Exception.Message)"
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $target
    }
}

# --- 4. Bump the canonical version (CMakeLists.txt) + derived copies ---
# src/version.h is what the .asi logs at attach and what package-release.ps1
# stamps the shipped manifest from; MOD_VERSION is what install.cmd writes
# into .headtracking-state.json; launcher-manifest.json's mod_info.version is
# what the launcher reads out of the repo. All of them must move together or a
# shipped artifact reports a stale version.
$versionFiles = @(
    @{ Path = 'CMakeLists.txt';         Pattern = 'project\(Titanfall2HeadTracking VERSION \d+\.\d+\.\d+'; Replacement = "project(Titanfall2HeadTracking VERSION $target" },
    @{ Path = 'pixi.toml';              Pattern = '(?m)^version = "\d+\.\d+\.\d+"';                        Replacement = "version = `"$target`"" },
    @{ Path = 'scripts/install.cmd';    Pattern = '(?m)^set "MOD_VERSION=\d+\.\d+\.\d+"';                  Replacement = "set `"MOD_VERSION=$target`"" },
    @{ Path = 'launcher-manifest.json'; Pattern = '("version":\s*")\d+\.\d+\.\d+(")';                      Replacement = "`${1}$target`$2" }
)
foreach ($vf in $versionFiles) {
    $vfPath = Join-Path $ProjectRoot $vf.Path
    $vfText = Get-Content $vfPath -Raw
    if ($vfText -notmatch $vf.Pattern) {
        Write-Error "Version pattern not found in $($vf.Path) - cannot bump."
        exit 1
    }
    Write-NoBom -Path $vfPath -Text ($vfText -replace $vf.Pattern, $vf.Replacement)
}

# version.h carries the number four times over; rebuilding it wholesale is
# simpler than four targeted replaces and cannot leave the parts disagreeing.
$parts = $target.Split('.')
$versionHeader = @"
#pragma once

#define HEADTRACKING_VERSION_MAJOR $($parts[0])
#define HEADTRACKING_VERSION_MINOR $($parts[1])
#define HEADTRACKING_VERSION_PATCH $($parts[2])
#define HEADTRACKING_VERSION_STRING "$target"
"@
Write-NoBom -Path (Join-Path $ProjectRoot 'src/version.h') -Text ($versionHeader + "`n")

# scripts/install.cmd must stay CRLF or it fails silently on Windows.
$installPath = Join-Path $ProjectRoot 'scripts/install.cmd'
$installText = [System.IO.File]::ReadAllText($installPath) -replace "`r`n", "`n" -replace "`n", "`r`n"
[System.IO.File]::WriteAllText($installPath, $installText)

# --- 5. Release-config build + package ---------------------------------
Write-Host "Building and packaging release configuration..." -ForegroundColor Cyan
pixi run package
if ($LASTEXITCODE -ne 0) {
    Write-Error "Release build/package failed."
    exit 1
}

# --- 6. Validate the packaged manifest ---------------------------------
# A manifest naming a file the ZIP does not contain deploys the mod into
# folders nothing loads while the launcher reports "Installed". Catching that
# here rather than in CI means it never reaches a tag.
$installerZip = Get-ChildItem (Join-Path $ProjectRoot 'release') -Filter '*-installer.zip' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $installerZip) {
    Write-Error "No installer ZIP in release/ after packaging."
    exit 1
}
node (Join-Path $ProjectRoot 'cameraunlock-core/scripts/validate-manifest.mjs') $installerZip.FullName
if ($LASTEXITCODE -ne 0) {
    Write-Error "launcher-manifest.json validation failed."
    exit 1
}

# --- 7. Commit the version bump + changelog ----------------------------
git -C $ProjectRoot add CMakeLists.txt pixi.toml src/version.h scripts/install.cmd launcher-manifest.json CHANGELOG.md
git -C $ProjectRoot commit -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git commit failed."; exit 1 }

# --- 8. Annotated tag --------------------------------------------------
git -C $ProjectRoot tag -a $tag -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error "git tag failed."; exit 1 }

# --- 9. Push commits + tag (triggers .github/workflows/release.yml) ----
git -C $ProjectRoot push origin HEAD
if ($LASTEXITCODE -ne 0) { Write-Error "git push (commits) failed."; exit 1 }
git -C $ProjectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { Write-Error "git push (tag) failed."; exit 1 }

Write-Host "Released $tag" -ForegroundColor Green
