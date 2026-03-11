[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget is required for this installer script."
}

Write-Host "[install] Dolphin"
winget install -e --id DolphinEmulator.Dolphin --accept-package-agreements --accept-source-agreements

Write-Host "[install] Cemu"
winget install -e --id Cemu.Cemu --accept-package-agreements --accept-source-agreements

Write-Host "[ok] Emulator install commands completed."
