# ui_drive.ps1 — reusable live-GUI driver for agent verification.
#
# Two verbs, callable as functions (dot-source) or via -Command:
#
#   click  — PostMessage WM_LBUTTONDOWN/UP at client coords in a
#            process's main window. No foreground steal, so it works
#            on windows that are occluded or unfocused.
#   shot   — SetForegroundWindow + CopyFromScreen of the window's
#            screen rect to a PNG. Needs a real interactive session
#            (fails over SSH/headless agents — that is expected).
#   wait   — poll until the process shows a non-zero MainWindowHandle.
#
# Usage (Windows PowerShell or pwsh):
#   powershell -File tools/scripts/ui_drive.ps1 -Command wait `
#       -ProcessName video_editor -TimeoutSec 15
#   powershell -File tools/scripts/ui_drive.ps1 -Command click `
#       -ProcessName video_editor -X 640 -Y 400
#   powershell -File tools/scripts/ui_drive.ps1 -Command shot `
#       -ProcessName video_editor -OutFile C:/Temp/ve.png
#
# Dot-source for scripted sequences (functions stay in scope):
#   . tools/scripts/ui_drive.ps1
#   Wait-UiWindow -ProcessName video_editor -TimeoutSec 15
#   Send-UiClick  -ProcessName video_editor -X 100 -Y 40
#   Save-UiScreenshot -ProcessName video_editor -OutFile "$env:TEMP/ve.png"
#
# Conventions: -ProcessName is the exe name WITHOUT ".exe" (what
# Get-Process reports). Coords are CLIENT coords (origin at the
# window's top-left content area, not the screen). Exit code is 0 on
# success, 2 when the window/process was not found — scripts can gate
# on $LASTEXITCODE.
#
# Motivation: ad-hoc versions of these helpers were used to drive
# video_editor.exe for live verification (2026-11-21); distilled here
# so every agent reuses one implementation instead of re-deriving the
# P/Invoke signatures.

param(
    [ValidateSet('click', 'shot', 'wait')]
    [string]$Command,
    # Not Mandatory: dot-sourcing for the function forms must not prompt.
    [string]$ProcessName,
    [int]$X = 0,
    [int]$Y = 0,
    [string]$OutFile,
    [int]$TimeoutSec = 10,
    [int]$SettleMs = 400
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# P/Invoke surface: -MemberDefinition injects raw members into the
# generated XER.UiDrive.Native class — no nested wrapper class.
Add-Type -Namespace XER.UiDrive -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PostMessageW(
    System.IntPtr hWnd, uint msg, System.IntPtr wParam, System.IntPtr lParam);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetForegroundWindow(System.IntPtr hWnd);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(
    System.IntPtr hWnd, out RECT rect);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool IsWindow(System.IntPtr hWnd);
[System.Runtime.InteropServices.StructLayout(
    System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@ -ErrorAction SilentlyContinue   # already defined on re-dot-source

# lParam for mouse messages: LOWORD=x, HIWORD=y (signed 16-bit each).
function script:MakeLParam([int]$x, [int]$y) {
    [IntPtr](($x -band 0xFFFF) + (($y -band 0xFFFF) * 65536))
}

# Locate the process's main window; returns Process or throws.
function script:GetUiProcess([string]$name) {
    $p = Get-Process -Name $name -ErrorAction SilentlyContinue |
         Where-Object { $_.MainWindowHandle -ne 0 } |
         Select-Object -First 1
    if ($null -eq $p) {
        throw "no process '$name' with a main window"
    }
    return $p
}

function Wait-UiWindow {
    param([string]$ProcessName, [int]$TimeoutSec = 10)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $p = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
             Where-Object { $_.MainWindowHandle -ne 0 } |
             Select-Object -First 1
        if ($null -ne $p) { return $p }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    throw "timed out after ${TimeoutSec}s waiting for '$ProcessName' window"
}

function Send-UiClick {
    param([string]$ProcessName, [int]$X, [int]$Y)
    $h = (GetUiProcess $ProcessName).MainWindowHandle
    $lp = MakeLParam $X $Y
    # WM_LBUTTONDOWN=0x0201 wParam=MK_LBUTTON(1), WM_LBUTTONUP=0x0202.
    [void][XER.UiDrive.Native]::PostMessageW($h, 0x0201, [IntPtr]1, $lp)
    Start-Sleep -Milliseconds 60
    [void][XER.UiDrive.Native]::PostMessageW($h, 0x0202, [IntPtr]::Zero, $lp)
    return $true
}

function Save-UiScreenshot {
    param([string]$ProcessName, [Parameter(Mandatory = $true)][string]$OutFile,
          [int]$SettleMs = 400)
    $h = (GetUiProcess $ProcessName).MainWindowHandle
    [void][XER.UiDrive.Native]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds $SettleMs   # let the WM_PAINT settle
    $r = New-Object XER.UiDrive.Native+RECT
    [void][XER.UiDrive.Native]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    if ($w -le 0 -or $hh -le 0) { throw "window '$ProcessName' has empty rect" }
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    try {
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $g.CopyFromScreen($r.Left, $r.Top, 0, 0,
                              (New-Object System.Drawing.Size $w, $hh))
        } finally { $g.Dispose() }
        $dir = Split-Path -Parent $OutFile
        if ($dir -and -not (Test-Path $dir)) {
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
        }
        $bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bmp.Dispose() }
    return $OutFile
}

if ($Command) {
    if (-not $ProcessName) {
        Write-Error '-ProcessName is required with -Command'
        exit 2
    }
    try {
        switch ($Command) {
            'wait'  { [void](Wait-UiWindow -ProcessName $ProcessName `
                                          -TimeoutSec $TimeoutSec) }
            'click' { [void](Send-UiClick -ProcessName $ProcessName `
                                         -X $X -Y $Y) }
            'shot'  {
                if (-not $OutFile) {
                    throw '-OutFile is required for -Command shot'
                }
                [void](Save-UiScreenshot -ProcessName $ProcessName `
                                        -OutFile $OutFile -SettleMs $SettleMs)
            }
        }
    } catch {
        [Console]::Error.WriteLine("ui_drive: $_")
        exit 2
    }
}
