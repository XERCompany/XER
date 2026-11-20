<#
.SYNOPSIS
    Docker-based toolchain installer for XER / XER Studio on Windows.

.DESCRIPTION
    Container equivalent of InstallTools.ps1: instead of installing
    Chocolatey packages and MSYS2 on the host, this builds a Windows
    container image (docker/Dockerfile.toolchain) that carries MSYS2
    clang64 at C:\tools\msys64 plus the full mingw-w64-clang-x86_64-*
    package set from docker/msys2-packages.txt — the same list
    InstallTools.ps1 installs.

    Once the image exists, BuildWithDocker.ps1 runs the repo's normal
    CMake preset flow inside it with the checkout mounted at C:\src.

    Requirements:
      - Docker Desktop or Docker Engine on Windows
      - daemon switched to WINDOWS containers
        (docker info --format '{{.OSType}}' must print 'windows';
         MSYS2 is a Windows userland — Linux containers cannot run it)

.EXAMPLE
    .\InstallToolsWithDocker.ps1
    Build the xer/toolchain:latest image.

.EXAMPLE
    .\InstallToolsWithDocker.ps1 -Rebuild -Push myregistry/xer-toolchain
    Force a clean rebuild and push to a registry.
#>
[CmdletBinding()]
param(
    # Image repository name (no tag).
    [string]$ImageName = 'xer/toolchain',

    # Image tag.
    [string]$Tag = 'latest',

    # Rebuild from scratch (docker build --no-cache).
    [switch]$Rebuild,

    # docker push the image after a successful build.
    [string]$Push,

    # Skip the post-build smoke check (clang++ --version inside the image).
    [switch]$SkipVerify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
$Dockerfile = Join-Path $RepoRoot 'docker\Dockerfile.toolchain'
$PackageList = Join-Path $RepoRoot 'docker\msys2-packages.txt'
$Image = "${ImageName}:${Tag}"

function Step([string]$Message) {
    Write-Host "`n=== $Message" -ForegroundColor Cyan
}

# ---------------------------------------------------------------- docker
$docker = Get-Command docker.exe -ErrorAction SilentlyContinue
if (-not $docker) {
    throw @'
docker.exe not found on PATH.
Install Docker Desktop (https://docs.docker.com/desktop/setup/install/windows-install/)
or Docker Engine, then re-run this script.
'@
}
$osType = (docker info --format '{{.OSType}}' 2>$null)
if ($LASTEXITCODE -ne 0) {
    throw 'docker info failed — is the Docker daemon running?'
}
if ($osType -ne 'windows') {
    throw @"
Docker is in '$osType' container mode; MSYS2 needs Windows containers.
Switch mode: right-click the Docker whale tray icon ->
'Switch to Windows containers...', then re-run this script.
"@
}
foreach ($f in @($Dockerfile, $PackageList)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing $f" }
}

# ----------------------------------------------------------------- build
Step "Build $Image (MSYS2 clang64 + package list)"
$buildArgs = @('build', '-f', $Dockerfile, '-t', $Image)
if ($Rebuild) { $buildArgs += '--no-cache' }
$buildArgs += $RepoRoot
Write-Host "docker $($buildArgs -join ' ')"
& docker @buildArgs
if ($LASTEXITCODE -ne 0) { throw "docker build failed ($LASTEXITCODE)" }

# ---------------------------------------------------------------- verify
if (-not $SkipVerify) {
    Step 'Verify image (clang++ --version)'
    & docker run --rm $Image clang64/bin/clang++.exe --version
    if ($LASTEXITCODE -ne 0) { throw 'image verification failed' }
}

# ------------------------------------------------------------------ push
if ($Push) {
    Step "Push $Image -> $Push"
    & docker tag $Image $Push
    if ($LASTEXITCODE -ne 0) { throw "docker tag failed ($LASTEXITCODE)" }
    & docker push $Push
    if ($LASTEXITCODE -ne 0) { throw "docker push failed ($LASTEXITCODE)" }
}

Step 'Done'
Write-Host "image : $Image"
Write-Host 'next  : .\BuildWithDocker.ps1 -Test -Dist'
