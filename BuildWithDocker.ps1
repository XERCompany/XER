<#
.SYNOPSIS
    Docker-based build for XER / XER Studio (video_editor) on Windows.

.DESCRIPTION
    Runs the repo's normal Build.ps1 inside the toolchain image built by
    InstallToolsWithDocker.ps1 (docker/Dockerfile.toolchain). The repo
    checkout is mounted at C:\src, so out\build\<preset>\ artifacts land
    back on the host — object files, ninja logs and dist\video_editor\
    are all reusable afterwards.

    Parameters mirror Build.ps1 and are passed through verbatim.

.EXAMPLE
    .\BuildWithDocker.ps1
    Configure (if needed) + build video_editor with the default preset.

.EXAMPLE
    .\BuildWithDocker.ps1 -Test -Dist
    Build, run the test suite, and produce dist\video_editor\.

.EXAMPLE
    .\BuildWithDocker.ps1 -Target XER_VideoEditor_Tests -Clean
    Reconfigure from scratch and build only the tests.
#>
[CmdletBinding()]
param(
    # CMake configure/build preset name.
    [string]$Preset = 'windows-clang-msys2',

    # Ninja/CMake targets to build.
    [string[]]$Target = @('video_editor'),

    # Force a fresh configure even if CMakeCache.txt exists.
    [switch]$Configure,

    # Delete the preset binary dir before configuring.
    [switch]$Clean,

    # Run the video-editor test runner after building.
    [switch]$Test,

    # Build the video_editor_dist portable package.
    [switch]$Dist,

    # Skip the POST_BUILD DLL deployment.
    [switch]$NoDeploy,

    # Toolchain image to run (name:tag).
    [string]$Image = 'xer/toolchain:latest',

    # Extra args appended to `docker run` before the image name
    # (e.g. @('--memory','16g','--cpus','8')).
    [string[]]$RunArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot

function Step([string]$Message) {
    Write-Host "`n=== $Message" -ForegroundColor Cyan
}

# ---------------------------------------------------------------- docker
$docker = Get-Command docker.exe -ErrorAction SilentlyContinue
if (-not $docker) { throw 'docker.exe not found on PATH — see InstallToolsWithDocker.ps1' }
$osType = (docker info --format '{{.OSType}}' 2>$null)
if ($osType -ne 'windows') {
    throw "Docker is in '$osType' container mode; MSYS2 needs Windows containers."
}
& docker image inspect $Image *> $null
if ($LASTEXITCODE -ne 0) {
    throw "image '$Image' not found — build it first: .\InstallToolsWithDocker.ps1"
}

# -------------------------------------------------- Build.ps1 passthrough
$innerArgs = @('-File', 'C:\src\Build.ps1', '-Preset', $Preset)
foreach ($t in @($Target)) { $innerArgs += @('-Target', $t) }
if ($Configure) { $innerArgs += '-Configure' }
if ($Clean)     { $innerArgs += '-Clean' }
if ($Test)      { $innerArgs += '-Test' }
if ($Dist)      { $innerArgs += '-Dist' }
if ($NoDeploy)  { $innerArgs += '-NoDeploy' }

$runArgs = @(
    'run', '--rm',
    # Repo -> C:\src. Windows containers mount host paths directly;
    # out\ is gitignored/dockerignored so only sources cross the mount.
    '-v', "${RepoRoot}:C:\src",
    '-w', 'C:\src'
) + $RunArgs + @($Image) + $innerArgs

Step "docker run $Image  (Build.ps1 $($innerArgs[2..($innerArgs.Count-1)] -join ' '))"
Write-Host "docker $($runArgs -join ' ')"
& docker @runArgs
$rc = $LASTEXITCODE

if ($rc -ne 0) { throw "container build failed (exit $rc)" }

Step 'Done'
$distDir = Join-Path $RepoRoot "out\build\$Preset\dist\video_editor"
if (Test-Path $distDir) {
    $size = [math]::Round(((Get-ChildItem $distDir -Recurse -File |
        Measure-Object Length -Sum).Sum / 1MB), 1)
    Write-Host "dist : $distDir  ($size MB)"
}
