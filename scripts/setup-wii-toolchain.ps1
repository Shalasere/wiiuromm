[CmdletBinding()]
param(
    [switch]$UpdateFirst
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-MsysBashPath {
    $candidates = @()
    if ($env:DEVKITPRO) {
        $candidates += (Join-Path $env:DEVKITPRO "msys2\usr\bin\bash.exe")
    }
    $candidates += "C:\devkitPro\msys2\usr\bin\bash.exe"
    foreach ($path in $candidates) {
        if (Test-Path $path) {
            return $path
        }
    }
    throw "Unable to find devkitPro MSYS2 bash.exe. Expected at C:\devkitPro\msys2\usr\bin\bash.exe."
}

function Invoke-MsysCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    $bash = Get-MsysBashPath
    & $bash -lc $Command
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 command failed with exit code ${LASTEXITCODE}: $Command"
    }
}

if ($UpdateFirst) {
    Write-Host "[setup] Updating package database/system"
    Invoke-MsysCommand "pacman -Syu --noconfirm"
}

Write-Host "[setup] Installing Wii/Wii U toolchain packages"
Invoke-MsysCommand "pacman -S --needed --noconfirm devkitPPC libogc wut devkitppc-rules wut-tools"

Write-Host "[verify] Checking installed build rules"
Invoke-MsysCommand "test -f /c/devkitPro/devkitPPC/wii_rules"
Invoke-MsysCommand "test -f /c/devkitPro/wut/share/wut_rules"

Write-Host "[ok] Wii/Wii U toolchain is ready."
