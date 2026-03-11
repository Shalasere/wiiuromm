[CmdletBinding()]
param(
    [string]$EnvFile = "$env:USERPROFILE\Desktop\.env",
    [switch]$SkipApiPreflight
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
    throw "Unable to find devkitPro MSYS2 bash.exe."
}

function Convert-ToMsysPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full -match "^[A-Za-z]:\\") {
        $drive = $full.Substring(0, 1).ToLowerInvariant()
        $rest = $full.Substring(2).Replace("\", "/")
        return "/$drive$rest"
    }
    return $full.Replace("\", "/")
}

function Invoke-MsysCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    $bash = Get-MsysBashPath
    & $bash -lc $Command
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 command failed with exit code ${LASTEXITCODE}: $Command"
    }
}

function Find-CemuBinary {
    $cmd = Get-Command cemu,Cemu -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        return $cmd.Source
    }
    $candidates = @(
        "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Cemu.Cemu_Microsoft.Winget.Source_8wekyb3d8bbwe\Cemu_2.5\Cemu.exe",
        "$env:LOCALAPPDATA\Programs\Cemu\Cemu.exe",
        "$env:LOCALAPPDATA\Programs\Cemu\cemu.exe",
        "$env:LOCALAPPDATA\Cemu\cemu.exe",
        "C:\Program Files\Cemu\cemu.exe"
    )
    foreach ($path in $candidates) {
        if ($path -and (Test-Path $path)) {
            return $path
        }
    }
    return $null
}

function Load-DotEnv {
    param([Parameter(Mandatory = $true)][string]$Path)

    $loaded = @{}
    if (-not (Test-Path $Path)) {
        return $loaded
    }

    foreach ($line in Get-Content -Path $Path) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) { continue }
        if ($trimmed.StartsWith("#")) { continue }
        $eq = $trimmed.IndexOf("=")
        if ($eq -lt 1) { continue }
        $key = $trimmed.Substring(0, $eq).Trim()
        $val = $trimmed.Substring($eq + 1).Trim()
        if (($val.StartsWith('"') -and $val.EndsWith('"')) -or ($val.StartsWith("'") -and $val.EndsWith("'"))) {
            $val = $val.Substring(1, $val.Length - 2)
        }
        $loaded[$key] = $val
        Set-Item -Path ("Env:{0}" -f $key) -Value $val
    }
    return $loaded
}

function Sync-RommEnvFallbacks {
    $pairs = @(
        @{Src = "SERVER_URL"; Dst = "ROMM_SERVER_URL"},
        @{Src = "USERNAME"; Dst = "ROMM_USERNAME"},
        @{Src = "PASSWORD"; Dst = "ROMM_PASSWORD"},
        @{Src = "API_TOKEN"; Dst = "ROMM_AUTH_TOKEN"},
        @{Src = "PLATFORM"; Dst = "ROMM_PLATFORM"},
        @{Src = "DOWNLOAD_DIR"; Dst = "ROMM_DOWNLOAD_DIR"},
        @{Src = "FAT32_SAFE"; Dst = "ROMM_FAT32_SAFE"},
        @{Src = "LOG_LEVEL"; Dst = "ROMM_LOG_LEVEL"}
    )
    foreach ($pair in $pairs) {
        if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($pair.Dst))) {
            $srcVal = [Environment]::GetEnvironmentVariable($pair.Src)
            if (-not [string]::IsNullOrWhiteSpace($srcVal)) {
                Set-Item -Path ("Env:{0}" -f $pair.Dst) -Value $srcVal
            }
        }
    }
}

function Test-RommApiPreflight {
    $serverUrl = [Environment]::GetEnvironmentVariable("ROMM_SERVER_URL")
    if ([string]::IsNullOrWhiteSpace($serverUrl)) {
        $serverUrl = [Environment]::GetEnvironmentVariable("SERVER_URL")
    }
    if ([string]::IsNullOrWhiteSpace($serverUrl)) {
        Write-Host "[preflight] skipped (SERVER_URL/ROMM_SERVER_URL not set)"
        return
    }

    $token = [Environment]::GetEnvironmentVariable("ROMM_AUTH_TOKEN")
    if ([string]::IsNullOrWhiteSpace($token)) {
        $token = [Environment]::GetEnvironmentVariable("API_TOKEN")
    }
    $user = [Environment]::GetEnvironmentVariable("ROMM_USERNAME")
    if ([string]::IsNullOrWhiteSpace($user)) {
        $user = [Environment]::GetEnvironmentVariable("USERNAME")
    }
    $pass = [Environment]::GetEnvironmentVariable("ROMM_PASSWORD")
    if ([string]::IsNullOrWhiteSpace($pass)) {
        $pass = [Environment]::GetEnvironmentVariable("PASSWORD")
    }

    $headers = @{}
    if (-not [string]::IsNullOrWhiteSpace($token)) {
        $headers["Authorization"] = "Bearer $token"
    } elseif (-not [string]::IsNullOrWhiteSpace($user) -or -not [string]::IsNullOrWhiteSpace($pass)) {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("${user}:${pass}")
        $basic = [System.Convert]::ToBase64String($bytes)
        $headers["Authorization"] = "Basic $basic"
    }

    $url = $serverUrl.TrimEnd("/") + "/api/platforms?page=1&limit=1"
    try {
        $resp = Invoke-WebRequest -Method Get -Uri $url -Headers $headers -TimeoutSec 15
        Write-Host ("[preflight] API reachable ({0})" -f $resp.StatusCode)
    } catch {
        Write-Host ("[preflight] warning: API check failed: {0}" -f $_.Exception.Message)
    }
}

function Resolve-RommServerUrl {
    $url = [Environment]::GetEnvironmentVariable("ROMM_SERVER_URL")
    if ([string]::IsNullOrWhiteSpace($url)) {
        $url = [Environment]::GetEnvironmentVariable("SERVER_URL")
    }
    return $url
}

function Needs-RommLocalBridge {
    param([string]$ServerUrl)

    if ([string]::IsNullOrWhiteSpace($ServerUrl)) {
        return $false
    }
    try {
        $uri = [System.Uri]$ServerUrl
        $serverHost = $uri.Host.ToLowerInvariant()
        $port = if ($uri.IsDefaultPort) { if ($uri.Scheme -eq "https") { 443 } else { 80 } } else { $uri.Port }
        return -not (($serverHost -eq "localhost" -or $serverHost -eq "127.0.0.1") -and $port -eq 8080)
    } catch {
        return $false
    }
}

function Start-RommLocalBridge {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$ServerUrl
    )

    if (-not (Needs-RommLocalBridge -ServerUrl $ServerUrl)) {
        return $null
    }

    $pythonCmd = Get-Command python,py -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $pythonCmd) {
        Write-Host "[bridge] warning: python not found; skipping localhost bridge"
        return $null
    }

    $bridgeScript = Join-Path $RepoRoot "harness\romm_local_bridge.py"
    if (-not (Test-Path $bridgeScript)) {
        Write-Host "[bridge] warning: missing bridge script $bridgeScript"
        return $null
    }

    $logDir = Join-Path $RepoRoot "run\logs"
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $logFile = Join-Path $logDir "romm-bridge.log"
    $errFile = Join-Path $logDir "romm-bridge.err.log"
    if (Test-Path $logFile) { Remove-Item -Force $logFile }
    if (Test-Path $errFile) { Remove-Item -Force $errFile }

    $args = @(
        $bridgeScript,
        "--listen-host", "127.0.0.1",
        "--listen-port", "8080",
        "--target", $ServerUrl
    )

    $token = [Environment]::GetEnvironmentVariable("ROMM_AUTH_TOKEN")
    if ([string]::IsNullOrWhiteSpace($token)) {
        $token = [Environment]::GetEnvironmentVariable("API_TOKEN")
    }
    $user = [Environment]::GetEnvironmentVariable("ROMM_USERNAME")
    if ([string]::IsNullOrWhiteSpace($user)) {
        $user = [Environment]::GetEnvironmentVariable("USERNAME")
    }
    $pass = [Environment]::GetEnvironmentVariable("ROMM_PASSWORD")
    if ([string]::IsNullOrWhiteSpace($pass)) {
        $pass = [Environment]::GetEnvironmentVariable("PASSWORD")
    }

    if (-not [string]::IsNullOrWhiteSpace($token)) {
        $args += @("--bearer-token", $token)
    } elseif (-not [string]::IsNullOrWhiteSpace($user) -or -not [string]::IsNullOrWhiteSpace($pass)) {
        $args += @("--basic-user", $user, "--basic-pass", $pass)
    }

    if ([Environment]::GetEnvironmentVariable("ROMM_INSECURE_TLS") -eq "1") {
        $args += "--insecure"
    }

    $proc = Start-Process -FilePath $pythonCmd.Source -ArgumentList $args -PassThru `
        -RedirectStandardOutput $logFile -RedirectStandardError $errFile -WindowStyle Hidden
    Start-Sleep -Milliseconds 700
    if ($proc.HasExited) {
        Write-Host "[bridge] warning: bridge exited early; see $logFile / $errFile"
        return $null
    }
    Write-Host "[bridge] localhost bridge active: 127.0.0.1:8080 -> $ServerUrl"
    return $proc
}

function Stop-RommLocalBridge {
    param([System.Diagnostics.Process]$Process)

    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptDir
$repoMsys = Convert-ToMsysPath $repoRoot
$buildOut = Join-Path $repoRoot "wiiuromm.rpx"
$runDir = Join-Path $repoRoot "run"
$mlcDir = Join-Path $runDir "cemu_mlc"

$cemu = Find-CemuBinary
if (-not $cemu) {
    throw "Cemu not found. Run scripts/install-emulators-windows.ps1 first."
}

if (Test-Path $EnvFile) {
    Write-Host "[env] Loading $EnvFile"
    [void](Load-DotEnv -Path $EnvFile)
    Sync-RommEnvFallbacks
} else {
    Write-Host "[env] No env file at $EnvFile (continuing)"
}

if (-not $SkipApiPreflight) {
    Test-RommApiPreflight
}

New-Item -ItemType Directory -Force -Path $runDir | Out-Null
New-Item -ItemType Directory -Force -Path $mlcDir | Out-Null

Write-Host "[build] Wii U target"
Invoke-MsysCommand "export DEVKITPRO=/c/devkitPro; export DEVKITPPC=`$DEVKITPRO/devkitPPC; make -C '$repoMsys'"

if (-not (Test-Path $buildOut)) {
    throw "Missing build output: $buildOut"
}

Write-Host "[run] Launching Cemu with $buildOut (mlc: $mlcDir)"
$bridgeProc = Start-RommLocalBridge -RepoRoot $repoRoot -ServerUrl (Resolve-RommServerUrl)
try {
    & $cemu --mlc $mlcDir --game $buildOut
} finally {
    Stop-RommLocalBridge -Process $bridgeProc
}
