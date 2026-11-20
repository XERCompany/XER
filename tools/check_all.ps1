# check_all.ps1 — repeatable verification pass for XER builds.
#
# Runs the project's smoke tests and reports one summary line per
# check. Agents and humans use this after any merge or refactor;
# exit code is non-zero when any check fails.
#
#   pwsh tools/check_all.ps1 [-Preset windows-clang-msys2] [-Quick]
#
# -Quick skips the slow binaries (video_editor --smoke, scanner).

param(
    [string]$Preset = "windows-clang-msys2",
    [switch]$Quick
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "out/build/$Preset/bin"

if (-not (Test-Path $bin)) {
    Write-Host "FAIL missing build dir: $bin"
    exit 1
}

$failed = 0
$results = @()

function Invoke-Check([string]$name, [string]$exe, [string[]]$args_, [int]$timeoutSec = 120) {
    $path = Join-Path $bin $exe
    if (-not (Test-Path $path)) {
        $script:results += "SKIP  $name  (no $exe)"
        return
    }
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $path
    $psi.Arguments = ($args_ -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $proc = [System.Diagnostics.Process]::Start($psi)
    # Drain pipes asynchronously BEFORE waiting — a child that fills
    # the 4KB pipe buffer blocks on write and deadlocks a parent that
    # is only waiting for exit.
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($timeoutSec * 1000)) {
        $proc.Kill()
        $script:results += "FAIL  $name  (timeout ${timeoutSec}s)"
        $script:failed = 1
        $proc.Dispose()
        return
    }
    $out = $outTask.Result
    $err = $errTask.Result
    $code = $proc.ExitCode
    $proc.Dispose()
    if ($code -eq 0) {
        $script:results += "PASS  $name"
    } else {
        $script:results += "FAIL  $name  exit=$code"
        $script:failed = 1
        if ($err) {
            $script:results += ($err.Trim() -split "`n" | Select-Object -First 3 | ForEach-Object { "      $_" }) -join "`n"
        }
    }
}

Write-Host "== check_all [$Preset] =="

Invoke-Check "XER_UI_Tests"      "XER_UI_Tests.exe"      @()
Invoke-Check "XER_Media_Smoke"   "XER_Media_Smoke.exe"   @()
Invoke-Check "EngineScanVST3"    "EngineScanVST3.exe"    @()
if (-not $Quick) {
    Invoke-Check "video_editor --smoke" "video_editor.exe" @("--smoke") 30
    Invoke-Check "sd --help"            "sd.exe"           @("--help") 30
    Invoke-Check "preview"              "preview.exe"      @("--help") 15
}

$results | ForEach-Object { Write-Host $_ }
$pass = ($results | Where-Object { $_ -like "PASS*" }).Count
$fail = ($results | Where-Object { $_ -like "FAIL*" }).Count
$skip = ($results | Where-Object { $_ -like "SKIP*" }).Count
Write-Host "== $pass passed, $fail failed, $skip skipped =="
exit $failed
