<#
.SYNOPSIS
    Container-side CEF build driver — runs INSIDE xer/cef-builder.

.DESCRIPTION
    Invoked by BuildChromiumCEFWithDocker.ps1 (host side); not meant to
    be run on the host itself.

    Layout inside the container:
      C:\cef          persistent cache volume (depot_tools + chromium_git
                      checkout + build outputs survive image rebuilds)
      C:\src          mounted repo checkout — the finished
                      cef_binary_*_windows64* tree is copied to
                      C:\src\third_party\cef\cef_windows64

    Steps:
      1. depot_tools onto the cache volume (image copy seeds the first run)
      2. gclient/python bootstrap (depot_tools python3.bat)
      3. automate-git.py: fetch chromium+cef at -Branch/-Checkout,
         gn+autoninja the libcef_dll_wrapper + libcef, produce the
         binary_distrib tree
      4. copy cef_binary_*_windows64* into the mounted repo

.PARAMETER Branch
    Chromium/CEF branch number to check out (e.g. 8078 tracks
    Chromium 156.0.8078.* — the branch used by InstallCef.ps1's pinned
    binary dist).

.PARAMETER Checkout
    Optional exact commit/tag/hash for automate-git --checkout; overrides
    the branch tip when set.
#>
[CmdletBinding()]
param(
    [string]$Branch = '8078',
    [string]$Checkout = '',
    [string]$CacheRoot = 'C:\cef',
    [string]$RepoDir = 'C:\src',
    [string]$OutSubDir = 'third_party\cef\cef_windows64',
    # automate-git flags: minimal distribution or client distribution.
    [switch]$ClientDistrib,
    [switch]$NoClean,
    [string[]]$ExtraAutomateArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DepotTools = Join-Path $CacheRoot 'depot_tools'
$DownloadDir = Join-Path $CacheRoot 'code'
$AutomatePy = Join-Path $CacheRoot 'automate-git.py'
$AutomateUrl = 'https://bitbucket.org/chromiumembedded/cef/raw/master/tools/automate/automate-git.py'

function Step([string]$Message) {
    Write-Host "`n=== $Message" -ForegroundColor Cyan
}

# ---------------------------------------------------------- depot_tools
if (-not (Test-Path (Join-Path $DepotTools 'gclient.bat'))) {
    Step 'Seed depot_tools onto cache volume'
    New-Item -ItemType Directory -Force $CacheRoot | Out-Null
    if (Test-Path 'C:\depot_tools\gclient.bat') {
        Copy-Item -Recurse 'C:\depot_tools' $CacheRoot
    } else {
        git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git $DepotTools
    }
}
$env:PATH = "$DepotTools;$env:PATH"
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:vs2022_install = 'C:\BuildTools'
$env:GYP_MSVS_VERSION = '2022'
$env:GYP_MSVS_OVERRIDE_PATH = 'C:\BuildTools'
$env:GYP_GENERATORS = 'ninja'

# Bootstrap depot_tools' bundled python (first run downloads cipd/python).
Step 'Bootstrap depot_tools python'
& (Join-Path $DepotTools 'python3.bat') --version
if ($LASTEXITCODE -ne 0) { throw 'depot_tools python3 bootstrap failed' }

# ----------------------------------------------------------- automate-git
if (-not (Test-Path $AutomatePy)) {
    Step 'Fetch automate-git.py'
    Invoke-WebRequest -UseBasicParsing -Uri $AutomateUrl -OutFile $AutomatePy
}

$auto = @(
    $AutomatePy,
    "--download-dir=$DownloadDir",
    "--depot-tools-dir=$DepotTools",
    "--branch=$Branch",
    '--x64-build',
    '--no-debug-build',
    '--minimal-distrib',
    '--with-pgo-profiles'
)
if ($Checkout)       { $auto += "--checkout=$Checkout" }
if ($ClientDistrib)  { $auto += '--client-distrib' }
if (-not $NoClean)   { $auto += '--force-clean' }
$auto += $ExtraAutomateArgs

Step "automate-git.py branch=$Branch"
Write-Host "python3 $($auto -join ' ')"
& (Join-Path $DepotTools 'python3.bat') @auto
if ($LASTEXITCODE -ne 0) { throw "automate-git failed (exit $LASTEXITCODE)" }

# ------------------------------------------------------------ copy dist
Step 'Locate binary_distrib output'
$dist = Get-ChildItem (Join-Path $DownloadDir 'chromium_git\cef\binary_distrib') -Directory -Filter 'cef_binary_*_windows64*' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $dist) { throw "no cef_binary_*_windows64* under $DownloadDir\chromium_git\cef\binary_distrib" }
Write-Host "dist: $($dist.FullName)"

$out = Join-Path $RepoDir $OutSubDir
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -Recurse "$($dist.FullName)\*" $out
if ($LASTEXITCODE -ne 0) { throw 'copy to repo failed' }

# Same layout contract as InstallCef.ps1's Test-CefLayout.
$required = @('Release\libcef.dll', 'Release\libcef.lib',
              'Resources\icudtl.dat', 'include\cef_app.h',
              'libcef_dll\CMakeLists.txt')
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $out $rel))) {
        throw "CEF layout incomplete: missing $rel under $out"
    }
}
Step "Done — $out"
