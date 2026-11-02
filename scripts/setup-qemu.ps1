# Portable extraction only: no installer execution or machine-wide changes.
[CmdletBinding()]
param([string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$downloadDir = Join-Path $projectRoot 'tools\downloads'
$destination = Join-Path $projectRoot 'tools\qemu'
$archive = Join-Path $downloadDir 'qemu.exe'
if (-not (Test-Path -LiteralPath $SevenZip)) { throw '7-Zip required; pass -SevenZip with its executable path.' }
if (Test-Path -LiteralPath $destination) { throw "Destination already exists: $destination. Use the existing copy or a fresh project checkout." }
New-Item -ItemType Directory -Force $downloadDir | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $lock.qemu.url -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash.ToLowerInvariant() -ne $lock.qemu.sha512) {
    throw 'QEMU archive checksum mismatch. Extraction cancelled.'
}
& $SevenZip x $archive "-o$destination" -y
if ($LASTEXITCODE -ne 0) { throw 'QEMU extraction failed.' }
Write-Output "Prepared portable QEMU: $destination"
