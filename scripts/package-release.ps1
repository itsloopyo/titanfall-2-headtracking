param([string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path "$PSScriptRoot\.."
$binDir   = Join-Path $repoRoot "bin\$Configuration"
$outDir   = Join-Path $repoRoot 'release'

if (-not (Test-Path $binDir)) {
    throw "Build output not found at $binDir. Run: pixi run build-release"
}

$versionHeader = Join-Path $repoRoot 'src\version.h'
$match = (Select-String -Path $versionHeader -Pattern 'HEADTRACKING_VERSION_STRING\s+"([^"]+)"').Matches
if (-not $match) { throw "Could not parse HEADTRACKING_VERSION_STRING from $versionHeader" }
$version = $match[0].Groups[1].Value

$modName = 'Titanfall2HeadTracking'
$modSlug = 'titanfall-2-headtracking'
$asi     = 'Titanfall2HeadTracking.asi'

$asiPath = Join-Path $binDir $asi
if (-not (Test-Path $asiPath)) { throw "Missing build output: $asiPath" }

$vendorDll = Join-Path $repoRoot 'vendor\ultimate-asi-loader\dinput8.dll'
if (-not (Test-Path $vendorDll)) {
    throw "Missing vendored loader: $vendorDll. Run: pixi run update-deps"
}

$manifestPath = Join-Path $repoRoot 'launcher-manifest.json'
if (-not (Test-Path $manifestPath)) {
    throw "launcher-manifest.json not found at: $manifestPath"
}

# THIRD-PARTY-NOTICES.md names the exact cameraunlock-core commit compiled into
# the .asi, and the file is copied verbatim into both ZIPs - so a wrong commit
# there is a wrong attribution shipped to every user. The submodule is bumped by
# an automated job that has no reason to touch the notices, which is how the two
# drift apart silently. Checked here rather than trusted.
$noticesPath = Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md'
$pinnedCore  = (& git -C $repoRoot rev-parse 'HEAD:cameraunlock-core').Trim()
if ($LASTEXITCODE -ne 0 -or -not $pinnedCore) {
    throw "Could not read the cameraunlock-core submodule pin from git."
}
if ((Get-Content $noticesPath -Raw) -notmatch [regex]::Escape($pinnedCore)) {
    throw "THIRD-PARTY-NOTICES.md does not record the cameraunlock-core commit that is actually built ($pinnedCore). Update both mentions of the pinned commit, then re-run."
}

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# ---------- Installer ZIP (GitHub Release) ----------
$installerStage = Join-Path $outDir "$modSlug-installer-stage"
if (Test-Path $installerStage) { Remove-Item $installerStage -Recurse -Force }
New-Item -ItemType Directory -Path $installerStage | Out-Null

# Mod payload deployed to the game root by install.cmd. HeadTracking.ini is
# created by the mod on first launch, so it is not shipped here.
$pluginsDir = Join-Path $installerStage 'plugins'
New-Item -ItemType Directory -Path $pluginsDir | Out-Null
Copy-Item $asiPath $pluginsDir

# Vendored Ultimate ASI Loader: install-time source of truth, copied to
# <game>\dsound.dll by install.cmd.
$vendorStage = Join-Path $installerStage 'vendor\ultimate-asi-loader'
New-Item -ItemType Directory -Path $vendorStage | Out-Null
foreach ($f in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $src = Join-Path $repoRoot "vendor\ultimate-asi-loader\$f"
    if (Test-Path $src) { Copy-Item $src $vendorStage }
}

Copy-Item (Join-Path $repoRoot 'scripts\install.cmd')   $installerStage
Copy-Item (Join-Path $repoRoot 'scripts\uninstall.cmd') $installerStage

Import-Module (Join-Path $repoRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
Copy-SharedBundle -StagingDir $installerStage -NoRefresh

foreach ($doc in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    $p = Join-Path $repoRoot $doc
    if (Test-Path $p) { Copy-Item $p $installerStage }
}

# Canonical launcher manifest. The launcher reads launcher-manifest.json from
# the package root (deploy/manifest.rs MANIFEST_FILE) to recognise the package
# and route install_cmd delivery. Stamp the version from the build so the
# shipped manifest can never disagree with the built .asi.
$stagedManifest = Join-Path $installerStage 'launcher-manifest.json'
$manifestText = Get-Content $manifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "Staged launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $outDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Compress-Archive -Path "$installerStage\*" -DestinationPath $installerZip
Remove-Item $installerStage -Recurse -Force
Write-Host "Packaged installer: $installerZip" -ForegroundColor Green

# ---------- Nexus ZIP (extract to game folder) ----------
# Deploy subtree only: the .asi lands in the game root. Nexus users supply
# their own ASI loader (dsound.dll), so no vendored loader is bundled here.
$nexusStage = Join-Path $outDir "$modSlug-nexus-stage"
if (Test-Path $nexusStage) { Remove-Item $nexusStage -Recurse -Force }
New-Item -ItemType Directory -Path $nexusStage | Out-Null
Copy-Item $asiPath $nexusStage

# MinHook is statically linked into the .asi under BSD-2-Clause, whose second
# condition binds a BINARY redistribution: the notice, the conditions and the
# disclaimer have to travel with it. This ZIP is a binary redistribution and
# carries nothing else, so without this it ships MinHook (and the Hacker
# Disassembler Engine inside it) with no attribution at all.
#
# One file rather than the installer's LICENSE + THIRD-PARTY-NOTICES.md pair,
# and named for the mod, because a Nexus user extracts this straight into the
# game root where a bare `LICENSE` would sit next to - or on top of - the game's
# own files.
$noticeFile = Join-Path $nexusStage "$modName-LICENSE.txt"
$noticeText = @(
    (Get-Content (Join-Path $repoRoot 'LICENSE') -Raw).TrimEnd(),
    '',
    ('=' * 79),
    '',
    (Get-Content (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Raw).TrimEnd(),
    ''
) -join "`r`n"
[System.IO.File]::WriteAllText($noticeFile, $noticeText, (New-Object System.Text.UTF8Encoding $false))

$nexusZip = Join-Path $outDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $repoRoot $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStage -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Compress-Archive -Path "$nexusStage\*" -DestinationPath $nexusZip
Remove-Item $nexusStage -Recurse -Force
Write-Host "Packaged nexus:     $nexusZip" -ForegroundColor Green
