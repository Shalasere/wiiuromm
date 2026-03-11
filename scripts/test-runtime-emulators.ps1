[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 12
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $PSCommandPath

Write-Host "[runtime] Wii U (Cemu)"
& (Join-Path $scriptDir "runtime-wiiu-smoke.ps1") -TimeoutSeconds $TimeoutSeconds
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "[runtime] Wii (Dolphin)"
& (Join-Path $scriptDir "runtime-wii-smoke.ps1") -TimeoutSeconds $TimeoutSeconds
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "[ok] emulator runtime smoke checks complete"
