[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lock = Get-Content (Join-Path $PSScriptRoot 'dxc-lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$destination = Join-Path $root ".tools\dxc\$($lock.version)"
if (Test-Path $destination) {
    throw 'DXC directory already exists. Do not overwrite it; verify the files or explicitly remove that tool directory first.'
}
$archive = Join-Path $root ".tools\dxc\dxc-$($lock.version).zip"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archive) | Out-Null
try {
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest -UseBasicParsing -Uri $lock.url -OutFile $archive
    }
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $lock.sha256) {
        throw 'DXC archive SHA256 mismatch. No files were installed; inspect the cached archive.'
    }
    Expand-Archive -Path $archive -DestinationPath $destination
    Write-Host "Installed DXC $($lock.version) to $destination. License files are inside that directory."
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
