[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 12,
    [int]$CaptureDelaySeconds = 4,
    [string]$CapturePath = "",
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

function Find-DolphinBinary {
    $cmd = Get-Command dolphin-emu,dolphin,Dolphin -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        return $cmd.Source
    }
    $candidates = @(
        "C:\Program Files\Dolphin\Dolphin.exe",
        "$env:LOCALAPPDATA\Programs\Dolphin\Dolphin.exe"
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
        $srcVal = [Environment]::GetEnvironmentVariable($pair.Src)
        if (-not [string]::IsNullOrWhiteSpace($srcVal)) {
            Set-Item -Path ("Env:{0}" -f $pair.Dst) -Value $srcVal
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

function Stop-ExistingDolphin {
    $running = Get-Process -Name Dolphin,DolphinQt2,DolphinWx -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "[run] Stopping existing Dolphin process(es)"
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 600
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

function Set-IniValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Section,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path $Path) {
        foreach ($line in Get-Content -Path $Path) {
            $lines.Add($line)
        }
    }

    $sectionHeader = "[$Section]"
    $sectionStart = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -eq $sectionHeader) {
            $sectionStart = $i
            break
        }
    }

    if ($sectionStart -lt 0) {
        if ($lines.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace($lines[$lines.Count - 1])) {
            $lines.Add("")
        }
        $lines.Add($sectionHeader)
        $lines.Add("$Key = $Value")
        $lines | Set-Content -Path $Path -Encoding Ascii
        return
    }

    $sectionEnd = $lines.Count
    for ($i = $sectionStart + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\s*\[.*\]\s*$") {
            $sectionEnd = $i
            break
        }
    }

    $escapedKey = [regex]::Escape($Key)
    for ($i = $sectionStart + 1; $i -lt $sectionEnd; $i++) {
        if ($lines[$i] -match "^\s*$escapedKey\s*=") {
            $lines[$i] = "$Key = $Value"
            $lines | Set-Content -Path $Path -Encoding Ascii
            return
        }
    }

    $lines.Insert($sectionEnd, "$Key = $Value")
    $lines | Set-Content -Path $Path -Encoding Ascii
}

function Initialize-DolphinControlProfile {
    param([Parameter(Mandatory = $true)][string]$UserDir)

    $cfgDir = Join-Path $UserDir "Config"
    New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null

    $gcPadIni = Join-Path $cfgDir "GCPadNew.ini"
    $wiimoteIni = Join-Path $cfgDir "WiimoteNew.ini"
    $globalCfgDir = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Dolphin Emulator\Config"
    $globalGcPad = Join-Path $globalCfgDir "GCPadNew.ini"
    $globalWiimote = Join-Path $globalCfgDir "WiimoteNew.ini"

    if (Test-Path $globalGcPad) {
        Copy-Item -Path $globalGcPad -Destination $gcPadIni -Force
    } else {
        @'
[GCPad1]
Device = DInput/0/Keyboard Mouse
Buttons/A = X
Buttons/B = Z
Buttons/X = C
Buttons/Y = S
Buttons/Z = D
Buttons/Start = RETURN
Main Stick/Up = UP
Main Stick/Down = DOWN
Main Stick/Left = LEFT
Main Stick/Right = RIGHT
Main Stick/Modifier = LSHIFT
[GCPad2]
Device = DInput/0/Keyboard Mouse
[GCPad3]
Device = DInput/0/Keyboard Mouse
[GCPad4]
Device = DInput/0/Keyboard Mouse
'@ | Set-Content -Path $gcPadIni -Encoding Ascii
    }

    if (Test-Path $globalWiimote) {
        Copy-Item -Path $globalWiimote -Destination $wiimoteIni -Force
    } else {
        @'
[Wiimote1]
Source = 1
[Wiimote2]
Source = 0
[Wiimote3]
Source = 0
[Wiimote4]
Source = 0
[BalanceBoard]
Source = 0
'@ | Set-Content -Path $wiimoteIni -Encoding Ascii
    }
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Source" -Value "1"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Device" -Value "DInput/0/Keyboard Mouse"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "IR/Up" -Value '`Cursor Y-`'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "IR/Down" -Value '`Cursor Y+`'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "IR/Left" -Value '`Cursor X-`'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "IR/Right" -Value '`Cursor X+`'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "IR/Auto-Hide" -Value "False"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/A" -Value '`Click 0` | X'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/B" -Value '`Click 1` | Z'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/1" -Value '`1`'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/2" -Value '`2`'
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/-" -Value "Q"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/+" -Value "E"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "Buttons/Home" -Value "RETURN"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "D-Pad/Up" -Value "UP"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "D-Pad/Down" -Value "DOWN"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "D-Pad/Left" -Value "LEFT"
    Set-IniValue -Path $wiimoteIni -Section "Wiimote1" -Key "D-Pad/Right" -Value "RIGHT"

    $dolphinIni = Join-Path $cfgDir "Dolphin.ini"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "SIDevice0" -Value "6"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "SIDevice1" -Value "0"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "SIDevice2" -Value "0"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "SIDevice3" -Value "0"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "WiimoteSource0" -Value "1"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "WiimoteSource1" -Value "0"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "WiimoteSource2" -Value "0"
    Set-IniValue -Path $dolphinIni -Section "Core" -Key "WiimoteSource3" -Value "0"
    Set-IniValue -Path $dolphinIni -Section "Input" -Key "BackgroundInput" -Value "True"
    Set-IniValue -Path $dolphinIni -Section "Interface" -Key "BackgroundInput" -Value "True"
    Set-IniValue -Path $dolphinIni -Section "Interface" -Key "PauseOnFocusLost" -Value "False"
}

function Capture-MainWindow {
    param(
        [Parameter(Mandatory = $true)][int]$ProcessId,
        [Parameter(Mandatory = $true)][string]$Path
    )

    Add-Type -AssemblyName System.Drawing
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Capture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
}
"@ -ErrorAction SilentlyContinue | Out-Null

    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if (-not $proc) { return $false }

    $handle = [IntPtr]::Zero
    for ($i = 0; $i -lt 40; $i++) {
        $proc.Refresh()
        $handle = $proc.MainWindowHandle
        if ($handle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($handle -eq [IntPtr]::Zero) { return $false }

    $rect = New-Object Win32Capture+RECT
    if (-not [Win32Capture]::GetWindowRect($handle, [ref]$rect)) { return $false }

    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $dir = Split-Path -Parent $Path
        if ($dir) {
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    return $true
}

if ($env:RUNTIME_TIMEOUT_SECONDS) {
    $TimeoutSeconds = [int]$env:RUNTIME_TIMEOUT_SECONDS
}
$headless = $true
if ($env:RUNTIME_HEADLESS -eq "0") {
    $headless = $false
}

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptDir
$repoMsys = Convert-ToMsysPath $repoRoot
$buildOut = Join-Path $repoRoot "wii\wiiuromm.dol"
$runDir = Join-Path $repoRoot "run"
$userDir = Join-Path $runDir "dolphin_user"
$logDir = Join-Path $runDir "logs"
$logFile = Join-Path $logDir "runtime-wii.log"
$errFile = Join-Path $logDir "runtime-wii.err.log"

$dolphin = Find-DolphinBinary
if (-not $dolphin) {
    Write-Host "[skip] dolphin not found; skipping Wii runtime smoke"
    exit 0
}

if (Test-Path $EnvFile) {
    Write-Host "[env] Loading $EnvFile"
    [void](Load-DotEnv -Path $EnvFile)
    Sync-RommEnvFallbacks
} else {
    Write-Host "[env] No env file at $EnvFile (continuing)"
}

$resolvedServerUrl = Resolve-RommServerUrl
Write-Host ("[env] ROMM_SERVER_URL={0}" -f ($(if ([string]::IsNullOrWhiteSpace($resolvedServerUrl)) { "<empty>" } else { $resolvedServerUrl })))
if ([string]::IsNullOrWhiteSpace($resolvedServerUrl)) {
    throw "ROMM server URL is empty. Set SERVER_URL/ROMM_SERVER_URL in $EnvFile."
}

if (-not $SkipApiPreflight) {
    Test-RommApiPreflight
}

New-Item -ItemType Directory -Force -Path $runDir, $userDir, $logDir | Out-Null
if (Test-Path $logFile) { Remove-Item -Force $logFile }
if (Test-Path $errFile) { Remove-Item -Force $errFile }
Set-Item -Path Env:ROMM_LOG_PATH -Value (Join-Path $logDir "wiiuromm-app.log")
Set-Item -Path Env:ROMM_EMULATOR -Value "dolphin"
Initialize-DolphinControlProfile -UserDir $userDir
Stop-ExistingDolphin

Write-Host "[build] Wii target"
Invoke-MsysCommand "export DEVKITPRO=/c/devkitPro; export DEVKITPPC=`$DEVKITPRO/devkitPPC; make -C '$repoMsys/wii'"

if (-not (Test-Path $buildOut)) {
    throw "[fail] Missing output: $buildOut"
}

$args = @("/u", $userDir, "/e", $buildOut)
if ($headless) {
    $args = @("/b") + $args
    Write-Host "[run] Wii smoke via Dolphin (batch) for ${TimeoutSeconds}s"
} else {
    Write-Host "[run] Wii smoke via Dolphin (visible) for ${TimeoutSeconds}s"
}

$bridgeProc = Start-RommLocalBridge -RepoRoot $repoRoot -ServerUrl $resolvedServerUrl
$proc = Start-Process -FilePath $dolphin -ArgumentList $args -PassThru `
    -RedirectStandardOutput $logFile -RedirectStandardError $errFile

try {
    if (-not $headless) {
        $captureTarget = $CapturePath
        if ([string]::IsNullOrWhiteSpace($captureTarget)) {
            $captureTarget = Join-Path $logDir "runtime-wii-visible.png"
        }
        Start-Sleep -Seconds ([Math]::Max(1, $CaptureDelaySeconds))
        if (Capture-MainWindow -ProcessId $proc.Id -Path $captureTarget) {
            Write-Host "[capture] Saved screenshot: $captureTarget"
        } else {
            Write-Host "[capture] warning: unable to capture Dolphin window"
        }
    }

    $exited = $proc.WaitForExit($TimeoutSeconds * 1000)
    if (-not $exited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Write-Host "[pass] Wii runtime smoke passed (process alive until timeout)"
        exit 0
    }

    Write-Host "[fail] Wii runtime smoke failed (rc=$($proc.ExitCode)). Logs: $logFile ; $errFile"
    if (Test-Path $logFile) { Get-Content $logFile -Tail 60 }
    if (Test-Path $errFile) { Get-Content $errFile -Tail 60 }
    exit $proc.ExitCode
} finally {
    Stop-RommLocalBridge -Process $bridgeProc
}
