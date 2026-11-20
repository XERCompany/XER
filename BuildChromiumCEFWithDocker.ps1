<#
.SYNOPSIS
    Build Chromium/CEF from source inside a Docker container.

.DESCRIPTION
    Source-build counterpart to InstallCef.ps1 (which downloads the
    prebuilt cef_binary_* archive). This script:

      1. builds the xer/cef-builder image (docker/Dockerfile.cef —
         VS Build Tools 2022 + depot_tools + MinGit) unless it exists
      2. runs the build via docker/BuildCefInContainer.ps1 inside a
         Windows container, with a persistent volume (default
         'xer-cef-cache') holding depot_tools + the ~100 GB chromium_git
         checkout across runs
      3. copies the produced cef_binary_*_windows64* tree into
         third_party\cef\cef_windows64 — the same layout
         InstallCef.ps1's Test-CefLayout checks, so XER_Cef picks it up
         on the next configure

    Hardware expectations: Windows container mode, >= 16 GB RAM,
    >= 150 GB free disk, and several hours for the first build.
    Subsequent builds reuse the cache volume and only recompile deltas.

.EXAMPLE
    .\BuildChromiumCEFWithDocker.ps1
    Build CEF branch 8078 (Chromium 156.0.8078.*) minimal distrib and
    place it in third_party\cef\cef_windows64.

.EXAMPLE
    .\BuildChromiumCEFWithDocker.ps1 -Branch 8078 -ClientDistrib -Rebuild
    Rebuild the builder image and produce the full client distribution.
#>
[CmdletBinding()]
param(
    # CEF/Chromium branch number (8078 = Chromium 156.0.8078.*, matching
    # the binary dist pinned by InstallCef.ps1).
    [string]$Branch = '8078',

    # Optional exact --checkout (commit/tag) for automate-git.py.
    [string]$Checkout = '',

    # Builder image name.
    [string]$Image = 'xer/cef-builder:latest',

    # Docker volume holding depot_tools + chromium checkout + outputs.
    [string]$CacheVolume = 'xer-cef-cache',

    # Repo-relative output directory for the CEF binary dist.
    [string]$OutSubDir = 'third_party\cef\cef_windows64',

    # Force --no-cache rebuild of the builder image.
    [switch]$Rebuild,

    # Skip image build (assume $Image already exists).
    [switch]$SkipImageBuild,

    # Build the full client distribution, not just the minimal one.
    [switch]$ClientDistrib,

    # Pass --force-clean off: keep the previous out/ dirs for an
    # incremental rebuild.
    [switch]$NoClean,

    # Extra arguments forwarded verbatim to automate-git.py.
    [string[]]$ExtraAutomateArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
$Dockerfile = Join-Path $RepoRoot 'docker\Dockerfile.cef'
$Driver = Join-Path $RepoRoot 'docker\BuildCefInContainer.ps1'

function Step([string]$Message) {
    Write-Host "`n=== $Message" -ForegroundColor Cyan
}

# ---------------------------------------------------------------- docker
$docker = Get-Command docker.exe -ErrorAction SilentlyContinue
if (-not $docker) { throw 'docker.exe not found on PATH' }
$osType = (docker info --format '{{.OSType}}' 2>$null)
if ($osType -ne 'windows') {
    throw "Docker is in '$osType' container mode; the CEF build needs Windows containers."
}
foreach ($f in @($Dockerfile, $Driver)) {
    if (-not (Test-Path -LiteralPath $f)) { throw "missing $f" }
}

# --------------------------------------------------------------- image
& docker image inspect $Image *> $null
$imageExists = ($LASTEXITCODE -eq 0)
if (-not $imageExists -or $Rebuild) {
    if ($SkipImageBuild -and -not $imageExists) {
        throw "image '$Image' missing and -SkipImageBuild was passed"
    }
    if (-not $SkipImageBuild) {
        Step "Build $Image (VS Build Tools + depot_tools — large, one-time)"
        $buildArgs = @('build', '-f', $Dockerfile, '-t', $Image)
        if ($Rebuild) { $buildArgs += '--no-cache' }
        $buildArgs += $RepoRoot
        & docker @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "docker build failed ($LASTEXITCODE)" }
    }
}

# -------------------------------------------------------------- cache vol
& docker volume inspect $CacheVolume *> $null
if ($LASTEXITCODE -ne 0) {
    Step "Create cache volume $CacheVolume"
    & docker volume create $CacheVolume | Out-Null
}

# ------------------------------------------------------------- run build
$inner = @('-File', 'C:\src\docker\BuildCefInContainer.ps1',
          '-Branch', $Branch,
          '-OutSubDir', ($OutSubDir -replace '\\','/'))
if ($Checkout)      { $inner += @('-Checkout', $Checkout) }
if ($ClientDistrib) { $inner += '-ClientDistrib' }
if ($NoClean)       { $inner += '-NoClean' }
if ($ExtraAutomateArgs) { $inner += @('-ExtraAutomateArgs', ($ExtraAutomateArgs -join ',')) }

Step "Run CEF build in $Image (branch $Branch)"
Write-Host 'NOTE: first run fetches ~100 GB of chromium sources and builds for hours.' -ForegroundColor Yellow
& docker run --rm `
    -v "${CacheVolume}:C:\cef" `
    -v "${RepoRoot}:C:\src" `
    -w C:\src `
    $Image @inner
if ($LASTEXITCODE -ne 0) { throw "CEF container build failed (exit $LASTEXITCODE)" }

Step 'Done'
Write-Host "CEF dist : $(Join-Path $RepoRoot $OutSubDir)"
Write-Host 'next    : .\Build.ps1   (or .\BuildWithDocker.ps1) — XER_Cef now finds the real CEF'
