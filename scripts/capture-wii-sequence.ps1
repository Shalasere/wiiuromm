[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 35,
    [int]$FrameCount = 45,
    [int]$FrameIntervalMs = 400,
    [int]$StartupWaitSeconds = 25,
    [string]$CaptureDir = "",
    [string]$EnvFile = "$env:USERPROFILE\Desktop\.env"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Capture-MainWindowFrame {
    param(
        [Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $Process.Refresh()
    $handle = $Process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        return $false
    }

    $rect = New-Object Win32CaptureSeq+RECT
    if (-not [Win32CaptureSeq]::GetWindowRect($handle, [ref]$rect)) {
        return $false
    }

    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }

    return $true
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32CaptureSeq {
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
"@

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $scriptDir
$runtimeScript = Join-Path $scriptDir "runtime-wii-smoke.ps1"
$logDir = Join-Path $repoRoot "run\logs"

if ([string]::IsNullOrWhiteSpace($CaptureDir)) {
    $CaptureDir = Join-Path $logDir "indexing-seq"
}

if (-not (Test-Path $runtimeScript)) {
    throw "Missing runtime script: $runtimeScript"
}

if (Test-Path $CaptureDir) {
    Remove-Item -Path $CaptureDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $CaptureDir | Out-Null

Get-Process -Name Dolphin,DolphinQt2,DolphinWx -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$runtimeJob = Start-Job -ScriptBlock {
    param($RuntimeScriptPath, $RuntimeTimeoutSeconds, $RuntimeEnvFile)
    $env:RUNTIME_HEADLESS = "0"
    & $RuntimeScriptPath -TimeoutSeconds $RuntimeTimeoutSeconds -CaptureDelaySeconds 2 -CapturePath "C:\git\wiiuromm\run\logs\runtime-wii-visible-seq-anchor.png" -EnvFile $RuntimeEnvFile
} -ArgumentList $runtimeScript, $TimeoutSeconds, $EnvFile

try {
    $target = $null
    $deadline = (Get-Date).AddSeconds($StartupWaitSeconds)
    while ((Get-Date) -lt $deadline -and -not $target) {
        $target = Get-Process -Name Dolphin,DolphinQt2,DolphinWx -ErrorAction SilentlyContinue |
            Sort-Object StartTime -Descending |
            Select-Object -First 1
        if (-not $target) {
            Start-Sleep -Milliseconds 300
        }
    }

    if (-not $target) {
        throw "Dolphin process not found during capture window"
    }

    $captured = 0
    for ($i = 0; $i -lt $FrameCount; $i++) {
        $framePath = Join-Path $CaptureDir ("frame-{0:D3}.png" -f $i)
        if (Capture-MainWindowFrame -Process $target -Path $framePath) {
            $captured++
        }
        Start-Sleep -Milliseconds $FrameIntervalMs
    }

    Wait-Job -Job $runtimeJob -Timeout ([Math]::Max(60, $TimeoutSeconds + 45)) | Out-Null
    $runtimeOutput = Receive-Job -Job $runtimeJob

    Write-Host ("captured_frames={0}" -f $captured)
    Write-Host ("capture_dir={0}" -f $CaptureDir)
    if ($runtimeOutput) {
        $runtimeOutput | ForEach-Object { Write-Host $_ }
    }
} finally {
    if ($runtimeJob) {
        Remove-Job -Job $runtimeJob -Force -ErrorAction SilentlyContinue
    }
}
