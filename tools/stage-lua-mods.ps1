[CmdletBinding()]
param(
    [string]$ModsDir,
    [string]$DistDir
)
$ErrorActionPreference = 'Stop'
if (-not $ModsDir) { $ModsDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'mods' }
if (-not $DistDir) { $DistDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\dist\mods' }
. (Join-Path $PSScriptRoot 'mod-payloads.ps1')
foreach ($folder in Get-ChildItem -LiteralPath $ModsDir -Directory) {
    $manifestPath = Join-Path $folder.FullName 'mod.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { continue }
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.type -ne 'lua') { continue }
    $files = @(Get-ModPayloadPaths -ModDir $folder.FullName -Manifest $manifest)
    $stage = Join-Path $DistDir $folder.Name
    foreach ($relative in $files) {
        $destination = Join-Path $stage $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $folder.FullName $relative) -Destination $destination -Force
    }
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $stage 'mod.json') -Force
    Write-Host "Staged Lua mod '$($manifest.name)' ($($files.Count) payload files)"
}
