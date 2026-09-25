# Fails when a file frozen.tsv records has changed: the frozen legacy reader, a core source
# it compiles, the oracle, or a first-run input. The reader and its core sources are what an
# old HeadTracking.ini is read through, so a change moves what a player's old file
# converts to; the oracle and the inputs are what the differential test holds that to.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$record = Join-Path $PSScriptRoot 'frozen.tsv'
# .NET's SHA256 rather than Get-FileHash: Windows PowerShell started from pwsh, as CI starts
# it, inherits pwsh's module path and cannot load the module Get-FileHash lives in.
$sha256 = [System.Security.Cryptography.SHA256]::Create()
$changed = @()
foreach ($line in [System.IO.File]::ReadAllLines($record)) {
    if ($line -eq '' -or $line.StartsWith('#')) { continue }
    $path, $sha = $line.Split("`t")[0, 1]
    if ($path.Contains(':')) { continue }
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $repo $path))
    $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    if ($actual -ne $sha) { $changed += "$path is $actual, frozen.tsv records $sha" }
}
if ($changed.Count -gt 0) {
    throw "Frozen config differential files changed:`n  $($changed -join "`n  ")"
}
Write-Host 'frozen config differential files: unchanged'
