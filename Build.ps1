<#
.SYNOPSIS
    One-command build for XER / XER Studio (video_editor) on Windows.

.DESCRIPTION
    Wraps the CMake preset flow used by this repo:

      1. Configure   cmake --preset <preset>          (auto if no cache)
      2. Build       cmake --build --preset <preset> --target <targets>
      3. Tests       -Test runs the editor test runner

    All MSYS2 dependencies link statically — no DLL deployment step.
    Optional heavy components (OpenCV, ggml/whisper, vulkan) are
    delay-loaded and only required when the matching feature runs.

    cmake/ninja are resolved from PATH first, then the MSYS2 clang64
    toolchain that the windows-clang-msys2 preset targets.

.EXAMPLE
    .\Build.ps1
    Configure (if needed) + build video_editor with the default preset.

.EXAMPLE
    .\Build.ps1 -Test
    Build and run the test suite.

.EXAMPLE
    .\Build.ps1 -Preset default -Target XER_VideoEditor_Tests -Clean
    Reconfigure the vcpkg preset from scratch and build only the tests.

.EXAMPLE
    .\Build.ps1 -KeepRunning
    Build without killing running exes from the build bin dir
    (default: the pre-build lock guard stops them for you).
#>
[CmdletBinding()]
param(
    # CMake configure/build preset name.
    [string]$Preset = 'windows-clang-msys2',

    # Ninja/CMake targets to build (empty = all).
    [string[]]$Target = @('video_editor'),

    # Force a fresh configure even if CMakeCache.txt exists.
    [switch]$Configure,

    # Delete the preset binary dir before configuring.
    [switch]$Clean,

    # Skip the pre-build lock guard — leave running exes from the
    # build bin dir alone (their file locks may then fail the build).
    [switch]$KeepRunning,

    # Run the video-editor test runner after building.
    [switch]$Test
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
$BinaryDir = Join-Path $RepoRoot "out\build\$Preset"
$MsysClangBin = 'C:\tools\msys64\clang64\bin'

function Find-Tool {
    param([string]$Name)
    $onPath = Get-Command "$Name.exe" -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidate = Join-Path $MsysClangBin "$Name.exe"
    if (Test-Path $candidate) { return $candidate }
    throw "'$Name' not found on PATH or in $MsysClangBin"
}

function Step([string]$Message) {
    Write-Host "`n=== $Message" -ForegroundColor Cyan
}

function Stop-LockingProcesses {
    # Running exes from the build bin dir keep CEF DLLs + link outputs
    # open (copy_directory "failed after retries" / ld.lld "failed to
    # write output"). Kill the known lockers ONLY when their image
    # path lives under $BinaryDir\bin — a same-named process anywhere
    # else is not ours and is left alone.
    $binDir = Join-Path $BinaryDir 'bin'
    $lockers = @('video_editor', 'xer_engine_ui_render', 'xer_browser_shell',
                 'XBrowser', 'XERVacUI', 'Editor', 'XER_VST3_CEF')
    $killed = 0
    foreach ($p in Get-Process -Name $lockers -ErrorAction SilentlyContinue) {
        $path = $null
        try { $path = $p.Path } catch { continue }  # access denied etc.
        if (-not $path) { continue }
        # Trailing '\' so '...\bin_evil\foo.exe' does not match '...\bin'.
        if ($path.StartsWith("$binDir\", [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "kill  $($p.ProcessName) (PID $($p.Id))  $path"
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            ++$killed
        }
    }
    if ($killed -eq 0) {
        Write-Host "none  (no matching processes under $binDir)"
    }
}

# ---------------------------------------------------------------- tools
$cmake = Find-Tool 'cmake'
# Ninja is used by every preset in this repo; resolve it so we can also
# pass -j and query targets without cmake --build quirks.
$ninja = Find-Tool 'ninja'
Write-Host "cmake : $cmake"
Write-Host "ninja : $ninja"

# ------------------------------------------------------------ lock guard
# Must run before Clean too — a running exe also blocks deleting the
# binary dir. -KeepRunning opts out entirely.
if (-not $KeepRunning) {
    Step "Stop processes locking $BinaryDir\bin"
    Stop-LockingProcesses
}

# ---------------------------------------------------------------- clean
if ($Clean -and (Test-Path $BinaryDir)) {
    Step "Clean $BinaryDir"
    Remove-Item -Recurse -Force $BinaryDir
}

# ------------------------------------------------------------- configure
$cacheFile = Join-Path $BinaryDir 'CMakeCache.txt'
if ($Configure -or -not (Test-Path $cacheFile)) {
    Step "Configure preset '$Preset'"
    & $cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}

# ----------------------------------------------------------------- build
Step "Build preset '$Preset'  targets: $($Target -join ', ')"
$buildArgs = @('--build', '--preset', $Preset)
foreach ($t in $Target) { $buildArgs += @('--target', $t) }
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

# ------------------------------------------------------------------ test
if ($Test) {
    $testExe = Join-Path $BinaryDir 'bin\XER_VideoEditor_Tests.exe'
    if (-not (Test-Path $testExe)) {
        Step "Test runner missing - building it"
        & $cmake --build --preset $Preset --target XER_VideoEditor_Tests
        if ($LASTEXITCODE -ne 0) { throw "test build failed ($LASTEXITCODE)" }
    }
    Step "Run tests"
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
}

# ---------------------------------------------------------------- summary
Step "Done"
$exe = Join-Path $BinaryDir 'bin\video_editor.exe'
if (Test-Path $exe) {
    Write-Host "exe  : $exe"
}
