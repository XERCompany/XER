# InstallCef.ps1 --- download and unpack the CEF binary distribution.
#
# CEF is not published to winget/choco/MSYS2 --- official builds live on
# cef-builds.spotifycdn.com. This script fetches the windows64 "minimal"
# distribution (runtime + libcef_dll_wrapper sources, no sample apps) and
# extracts it to third_party/cef/cef_windows64 where the CMake shim
# (cmake/deps/configs/CEF/CEFConfig.cmake) looks for it.
#
# Usage:
#   ./InstallCef.ps1                        # pinned default version
#   ./InstallCef.ps1 -Latest                # resolve newest stable via index.json
#   ./InstallCef.ps1 -Version <cef_version> # e.g. 156.0.30+g6028375+chromium-156.0.8078.96
#   ./InstallCef.ps1 -Force                 # reinstall / re-extract over existing
#   ./InstallCef.ps1 -Clean                 # also delete the cached archive
param(
    [string]$Version = "156.0.30+g6028375+chromium-156.0.8078.96",
    [switch]$Latest,
    [switch]$Force,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"   # IWR progress bar is very slow

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$cefDir  = Join-Path $root "third_party\cef"
$target  = Join-Path $cefDir "cef_windows64"
$archive = Join-Path $cefDir "cef_windows64_minimal.tar.bz2"

function Resolve-LatestVersion {
    Write-Host "Resolving latest stable CEF for windows64 ..."
    $index = Invoke-RestMethod "https://cef-builds.spotifycdn.com/index.json"
    $stable = $index.windows64.versions |
        Where-Object { $_.channel -eq "stable" } |
        Select-Object -First 1                  # index is newest-first
    if (-not $stable) { throw "index.json has no stable windows64 entry" }
    $minimal = $stable.files |
        Where-Object { $_.name -like "*_minimal.tar.bz2" } |
        Select-Object -First 1
    if (-not $minimal) { throw "stable $($stable.cef_version) has no minimal build" }
    return $stable.cef_version
}

function Test-CefLayout([string]$dir) {
    foreach ($p in "Release\libcef.dll", "Release\libcef.lib",
                  "Resources\icudtl.dat", "include\cef_app.h",
                  "libcef_dll\CMakeLists.txt") {
        if (-not (Test-Path (Join-Path $dir $p))) { return $false }
    }
    return $true
}

if ($Latest) { $Version = Resolve-LatestVersion }
$name    = "cef_binary_${Version}_windows64_minimal"
$encoded = [uri]::EscapeDataString($Version)
$url     = "https://cef-builds.spotifycdn.com/${name}.tar.bz2"

if ((Test-CefLayout $target) -and -not $Force) {
    Write-Host "CEF already installed at $target (-Force to reinstall)"
    exit 0
}

New-Item -ItemType Directory -Force -Path $cefDir | Out-Null

# ~175 MB. Reuse a cached archive; validate size > 100 MB to catch a
# truncated earlier download.
$needDownload = $Force -or -not (Test-Path $archive) -or
    ((Get-Item $archive).Length -lt 100MB)
if ($needDownload) {
    if (Test-Path $archive) { Remove-Item $archive -Force }
    Write-Host "Downloading $name (~175 MB) ..."
    Write-Host "  $url"
    Invoke-WebRequest -Uri $url -OutFile $archive
    if ((Get-Item $archive).Length -lt 100MB) {
        Remove-Item $archive -Force
        throw "Download truncated --- archive smaller than expected"
    }
}

Write-Host "Extracting (this takes a minute) ..."
# Extract into a staging dir so a failed unpack can't leave a
# half-written tree at $target.
$staging = Join-Path $cefDir ".extract_tmp"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force -Path $staging | Out-Null
# Windows ships bsdtar (libarchive) which handles drive-letter paths
# natively; an MSYS GNU tar in PATH would parse "D:..." as a remote
# host:file pair. Prefer System32 tar, fall back to PATH + --force-local.
$tarExe = Join-Path $env:SystemRoot "System32\tar.exe"
if (Test-Path $tarExe) {
    & $tarExe -xjf $archive -C $staging
} else {
    tar --force-local -xjf $archive -C $staging
}
if ($LASTEXITCODE -ne 0) {
    Remove-Item -Recurse -Force $staging
    throw "tar extraction failed (exit $LASTEXITCODE)"
}
$extracted = Join-Path $staging $name
if (-not (Test-CefLayout $extracted)) {
    Remove-Item -Recurse -Force $staging
    throw "Extracted layout incomplete under $extracted"
}
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Move-Item $extracted $target
Remove-Item -Recurse -Force $staging

if ($Clean -and (Test-Path $archive)) {
    Remove-Item $archive -Force
    Write-Host "Archive cache cleaned"
}

Write-Host "CEF $Version installed: $target"
Write-Host "Reconfigure CMake (cmake --preset windows-clang-msys2) to enable XER_Cef."

