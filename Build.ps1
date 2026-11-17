<#
.SYNOPSIS
    One-command build for XER / XER Studio (video_editor) on Windows.

.DESCRIPTION
    Wraps the CMake preset flow used by this repo:

      1. Configure   cmake --preset <preset>          (auto if no cache)
      2. Build       cmake --build --preset <preset> --target <targets>
      3. Deploy DLLs - happens automatically in the video_editor POST_BUILD
                       step (CopyDLL.ps1 via LLVM import scan)
      4. Tests       -Test runs the editor test runner
      5. Portable    -Dist builds the video_editor_dist target
                       -> dist\video_editor\ standalone package

    cmake/ninja are resolved from PATH first, then the MSYS2 clang64
    toolchain that the windows-clang-msys2 preset targets.

.EXAMPLE
    .\Build.ps1
    Configure (if needed) + build video_editor with the default preset.

.EXAMPLE
    .\Build.ps1 -Test -Dist
    Build, run the test suite, and produce dist\video_editor\.

.EXAMPLE
    .\Build.ps1 -Preset default -Target XER_VideoEditor_Tests -Clean
    Reconfigure the vcpkg preset from scratch and build only the tests.
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

    # Run the video-editor test runner after building.
    [switch]$Test,

    # Build the video_editor_dist portable package.
    [switch]$Dist,

    # Skip the POST_BUILD DLL deployment (sets cache var off).
    [switch]$NoDeploy
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

# ---------------------------------------------------------------- tools
$cmake = Find-Tool 'cmake'
# Ninja is used by every preset in this repo; resolve it so we can also
# pass -j and query targets without cmake --build quirks.
$ninja = Find-Tool 'ninja'
Write-Host "cmake : $cmake"
Write-Host "ninja : $ninja"

# ---------------------------------------------------------------- clean
if ($Clean -and (Test-Path $BinaryDir)) {
    Step "Clean $BinaryDir"
    Remove-Item -Recurse -Force $BinaryDir
}

# ------------------------------------------------------------- configure
$cacheFile = Join-Path $BinaryDir 'CMakeCache.txt'
if ($Configure -or -not (Test-Path $cacheFile)) {
    Step "Configure preset '$Preset'"
    $cfgArgs = @('--preset', $Preset)
    if ($NoDeploy) {
        $cfgArgs += '-DXER_VIDEO_EDITOR_DEPLOY_DLLS=OFF'
    }
    & $cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}

# ----------------------------------------------------------------- build
$targets = @($Target)
if ($Dist) { $targets += 'video_editor_dist' }

Step "Build preset '$Preset'  targets: $($targets -join ', ')"
$buildArgs = @('--build', '--preset', $Preset)
foreach ($t in $targets) { $buildArgs += @('--target', $t) }
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
    # The runner needs the MSYS2 runtime DLLs on PATH.
    $env:PATH = "$MsysClangBin;$env:PATH"
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "tests failed ($LASTEXITCODE)" }
}

# ---------------------------------------------------------------- summary
Step "Done"
$exe = Join-Path $BinaryDir 'bin\video_editor.exe'
if (Test-Path $exe) {
    $dllCount = (Get-ChildItem (Split-Path $exe) -Filter '*.dll' -ErrorAction SilentlyContinue).Count
    Write-Host "exe  : $exe"
    Write-Host "dlls : $dllCount next to exe"
}
if ($Dist) {
    $distDir = Join-Path $BinaryDir 'dist\video_editor'
    if (Test-Path $distDir) {
        $size = [math]::Round(((Get-ChildItem $distDir -Recurse -File |
            Measure-Object Length -Sum).Sum / 1MB), 1)
        Write-Host "dist : $distDir  ($size MB)"
    }
}
