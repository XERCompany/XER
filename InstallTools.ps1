<#
.SYNOPSIS
    InstallTools.ps1 - XER toolchain installer (vcpkg -> choco + MSYS2 clang64 + Ninja)

.DESCRIPTION
    Single-file, idempotent installer for the XER toolchain migration away
    from vcpkg. Safe to re-run. Requires Administrator rights (Chocolatey
    and winget need elevation); the script self-elevates when needed.

    Sources of truth (do not change package lists without updating these):
      - docs/wiki/research/toolchain-migration-audit.md  (verified package list)
      - docs/wiki/decisions/toolchain-migration.md
      - ForceChangeManagerPackages.md                  (original plan)

    Steps performed:
      1. Elevation check + self-elevate
      2. Process-scope execution policy bypass
      3. Chocolatey bootstrap (if missing)
      4. choco packages: nodejs-lts, python3, cmake, ninja, msys2
      5. Intel oneMKL via winget (manual fallback URL on failure)
      6. MSYS2 pacman -Syu + ~60 mingw-w64-clang-x86_64-* packages
      7. Verification (clang++/cmake/ninja versions), PATH notes, summary

    Deliberately NOT installed (audit decisions):
      - visualstudio2026buildtools : the clang64/mingw path has no MSVC
        dependency. VS Build Tools are only needed for clang-cl or CUDA
        builds, and both are dropped on this toolchain (nvcc requires
        MSVC cl.exe; there is no mingw path for CUDA).
      - choco llvm                  : clang-cl is MSVC-ABI and incompatible
        with mingw-w64 clang64 packages. The compiler comes from the
        MSYS2 group 'mingw-w64-clang-x86_64-toolchain' + libc++ instead.
      - choco cuda, nasm            : CUDA has no mingw path (dead on this
        preset); nasm was only needed by CUDA/asm paths.
      - pacman lua54                : the repo FetchContent-builds Lua.
      - pacman llvm/llvm-libs/llvm-tools/lld : redundant, folded into the
        'mingw-w64-clang-x86_64-toolchain' group.

    NOT handled here (repo-side tasks, see audit "Hard blockers"):
      pthreadpool, fxdiv, fp16, dlfcn-win32 are NOT pacman packages - they
      must be vendored or shimmed inside the repository.

    This script installs TOOLS ONLY. It does not modify the repo, does not
    permanently change PATH (only prints what to add), and does not run
    any cmake configure.

.PARAMETER Msys2Path
    Path to an existing MSYS2 install (must contain usr\bin\pacman.exe).
    Auto-detected when omitted, in this order:
      -Msys2Path > $env:MSYSTEM_PREFIX > C:\tools\msys64 (choco default)
      > C:\msys64 (MSYS2 installer default) > uninstall-registry scan
      > <drive>:\msys64 on every fixed drive.

.PARAMETER SkipChoco
    Skip Chocolatey bootstrap and all choco package installs.

.PARAMETER SkipMsys2
    Skip all pacman / MSYS2 package steps (choco + MKL still run).

.PARAMETER WhatIf
    Print the commands that would run; do not execute installers.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\InstallTools.ps1
.EXAMPLE
    .\InstallTools.ps1 -Msys2Path D:\tools\msys64 -WhatIf
#>

param(
    [string]$Msys2Path,
    [switch]$SkipChoco,
    [switch]$SkipMsys2,
    [switch]$WhatIf
)

$ErrorActionPreference = 'Continue'

# ===========================================================================
# Helpers
# ===========================================================================

function Write-Step  { param([string]$Message) Write-Host "`n=== $Message ===" -ForegroundColor Cyan }
function Write-Note   { param([string]$Message) Write-Host "  $Message" -ForegroundColor DarkGray }
function Write-Ok     { param([string]$Message) Write-Host "  [ok]   $Message" -ForegroundColor Green }
function Write-WarnX  { param([string]$Message) Write-Host "  [warn] $Message" -ForegroundColor Yellow }
function Write-FailX  { param([string]$Message) Write-Host "  [FAIL] $Message" -ForegroundColor Red }

function Invoke-External {
    <#
    .SYNOPSIS
        Run a native command, echo it first, honour -WhatIf, return $true
        on exit code 0. Output is written to the host so the function's
        return value stays a clean boolean.
    #>
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @()
    )
    $displayArgs = ($Arguments | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
    }) -join ' '
    $cmdLine = ($FilePath + ' ' + $displayArgs).Trim()
    if ($WhatIf) {
        Write-Host "  [whatif] $cmdLine" -ForegroundColor DarkYellow
        return $true
    }
    Write-Host "  >> $cmdLine" -ForegroundColor DarkGray
    & $FilePath @Arguments | Out-Host
    return ($LASTEXITCODE -eq 0)
}

function Test-Msys2Root {
    param([string]$Path)
    return [bool]($Path -and (Test-Path -LiteralPath (Join-Path $Path 'usr\bin\pacman.exe')))
}

function Find-Msys2Install {
    <#
    .SYNOPSIS
        Locate an existing MSYS2 install. Returns the root dir containing
        usr\bin\pacman.exe, or $null.
    #>
    $roots = New-Object 'System.Collections.Generic.List[string]'
    if ($Msys2Path)          { $roots.Add($Msys2Path) }
    if ($env:MSYSTEM_PREFIX) { $roots.Add($env:MSYSTEM_PREFIX) }
    $roots.Add('C:\tools\msys64')   # Chocolatey msys2 default
    $roots.Add('C:\msys64')         # MSYS2 installer default

    # Uninstall registry entries. The Inno-Setup MSYS2 installer registers
    # "MSYS2" with an uninstaller inside the install root; Chocolatey entries
    # point at choco.exe and simply fail validation below (harmless).
    $uninstallKeys = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
                     'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
                     'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    foreach ($entry in (Get-ItemProperty $uninstallKeys -ErrorAction SilentlyContinue)) {
        $name = $entry.DisplayName
        if (-not $name -or $name -notmatch '(?i)msys') { continue }
        foreach ($prop in 'InstallLocation','UninstallString','DisplayIcon') {
            $raw = $entry.$prop
            if (-not $raw) { continue }
            $dir = ($raw -replace '^"|"$','').Trim()
            if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
                $dir = Split-Path $dir -Parent
            }
            # Walk up to 3 parents: the uninstaller may sit in a subdir.
            for ($lvl = 0; $lvl -lt 3 -and $dir; $lvl++) {
                if ($dir) { $roots.Add($dir) }
                $dir = Split-Path $dir -Parent
            }
        }
    }

    # Any fixed drive root: <drive>:\msys64
    foreach ($drive in (Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
        $roots.Add((Join-Path $drive.Root 'msys64'))
    }

    foreach ($candidate in ($roots | Select-Object -Unique)) {
        if (-not $candidate) { continue }
        try { $full = [System.IO.Path]::GetFullPath($candidate.TrimEnd('\')) }
        catch { continue }
        if (Test-Msys2Root $full) { return $full }
    }
    return $null
}

function Refresh-MachinePath {
    # Pick up PATH changes made by choco/winget in this session.
    $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path','User')
}

function Install-Choco {
    $chocoBin = 'C:\ProgramData\chocolatey\bin'
    if (Test-Path (Join-Path $chocoBin 'choco.exe')) {
        if ($env:Path -notlike "*$chocoBin*") { $env:Path = "$env:Path;$chocoBin" }
    }
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        Write-Ok 'Chocolatey already installed'
        return $true
    }
    if ($WhatIf) {
        Write-Host "  [whatif] iex ((New-Object Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))" -ForegroundColor DarkYellow
        return $true
    }
    Write-Note 'Chocolatey not found - bootstrapping from community.chocolatey.org ...'
    try {
        Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process -Force -ErrorAction SilentlyContinue
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072
        iex ((New-Object Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
    } catch {
        Write-FailX "Chocolatey bootstrap failed: $($_.Exception.Message)"
        return $false
    }
    if ($env:Path -notlike "*$chocoBin*") { $env:Path = "$env:Path;$chocoBin" }
    if (Get-Command choco -ErrorAction SilentlyContinue) {
        Write-Ok 'Chocolatey installed'
        return $true
    }
    Write-FailX 'Chocolatey bootstrap finished but choco.exe is not on PATH'
    return $false
}

function Get-Winget {
    if (Get-Command winget -ErrorAction SilentlyContinue) { return 'winget' }
    $alias = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\winget.exe'
    if (Test-Path $alias) { return $alias }
    return $null
}

function Install-Mkl {
    <#
    .SYNOPSIS
        Install Intel oneMKL via winget. Returns a status string:
        installed | failed | manual | skipped-whatif.
    #>
    $manualUrl = 'https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl/download.html'
    if ($WhatIf) {
        Write-Host '  [whatif] winget install -e --id Intel.oneMKL --silent --accept-package-agreements --accept-source-agreements' -ForegroundColor DarkYellow
        return 'skipped-whatif'
    }
    $winget = Get-Winget
    if (-not $winget) {
        Write-FailX 'winget not found - MKL must be installed manually:'
        Write-Note "  $manualUrl"
        Write-Note '  (Intel component installer id: intel.oneapi.win.mkl.devel,'
        Write-Note '   requires the oneAPI base toolkit)'
        return 'manual'
    }
    Write-Host "  >> winget install -e --id Intel.oneMKL --silent --accept-package-agreements --accept-source-agreements" -ForegroundColor DarkGray
    & $winget install -e --id Intel.oneMKL --silent --accept-package-agreements --accept-source-agreements | Out-Host
    if ($LASTEXITCODE -eq 0) {
        Write-Ok 'MKL installed via winget'
        return 'installed'
    }
    Write-FailX "winget MKL install failed (exit $LASTEXITCODE) - install manually:"
    Write-Note "  $manualUrl"
    Write-Note '  (Intel component installer id: intel.oneapi.win.mkl.devel,'
    Write-Note '   requires the oneAPI base toolkit)'
    return 'failed'
}

# ===========================================================================
# MSYS2 clang64 package list
# ===========================================================================
# Audit-verified (docs/wiki/research/toolchain-migration-audit.md):
#   - toolchain group replaces the plan's llvm/llvm-libs/llvm-tools/lld
#   - lua54 dropped (repo FetchContent-builds Lua)
#   - pkgconf/cmake/ninja added per audit
#   - second block = "required deps missing from the plan" audit section
$script:Msys2Packages = @(
    # --- toolchain ---
    'mingw-w64-clang-x86_64-toolchain',   # group: clang, lld, compiler-rt, libc++...
    'mingw-w64-clang-x86_64-libc++',
    'mingw-w64-clang-x86_64-pkgconf',
    'mingw-w64-clang-x86_64-cmake',
    'mingw-w64-clang-x86_64-ninja',

    # --- original plan list (audit-verified, lua54 removed) ---
    'mingw-w64-clang-x86_64-zlib',
    'mingw-w64-clang-x86_64-openssl',
    'mingw-w64-clang-x86_64-icu',
    'mingw-w64-clang-x86_64-ffmpeg',
    'mingw-w64-clang-x86_64-libuv',
    'mingw-w64-clang-x86_64-range-v3',
    'mingw-w64-clang-x86_64-re2',
    'mingw-w64-clang-x86_64-abseil-cpp',
    'mingw-w64-clang-x86_64-flatbuffers',
    'mingw-w64-clang-x86_64-jsoncpp',
    'mingw-w64-clang-x86_64-fmt',
    'mingw-w64-clang-x86_64-skia',
    'mingw-w64-clang-x86_64-opencv',
    'mingw-w64-clang-x86_64-libraw',
    'mingw-w64-clang-x86_64-fftw',
    'mingw-w64-clang-x86_64-ggml',

    # --- audit: required deps missing from the original plan ---
    'mingw-w64-clang-x86_64-sqlite',
    'mingw-w64-clang-x86_64-zstd',
    'mingw-w64-clang-x86_64-brotli',
    'mingw-w64-clang-x86_64-curl',
    'mingw-w64-clang-x86_64-boost',
    'mingw-w64-clang-x86_64-eigen3',
    'mingw-w64-clang-x86_64-cereal',
    'mingw-w64-clang-x86_64-mimalloc',
    'mingw-w64-clang-x86_64-folly',
    'mingw-w64-clang-x86_64-pugixml',
    'mingw-w64-clang-x86_64-libarchive',
    'mingw-w64-clang-x86_64-assimp',
    'mingw-w64-clang-x86_64-libmupdf',
    'mingw-w64-clang-x86_64-libffi',
    'mingw-w64-clang-x86_64-shaderc',
    'mingw-w64-clang-x86_64-spirv-tools',
    'mingw-w64-clang-x86_64-glslang',
    'mingw-w64-clang-x86_64-vulkan-headers',
    'mingw-w64-clang-x86_64-vulkan-loader',
    'mingw-w64-clang-x86_64-vulkan-memory-allocator',
    'mingw-w64-clang-x86_64-vulkan-utility-libraries',
    'mingw-w64-clang-x86_64-asmjit',
    'mingw-w64-clang-x86_64-nlohmann-json',
    'mingw-w64-clang-x86_64-rapidjson',
    'mingw-w64-clang-x86_64-tinyxml2',
    'mingw-w64-clang-x86_64-chrono-date',
    'mingw-w64-clang-x86_64-lz4',
    'mingw-w64-clang-x86_64-snappy',
    'mingw-w64-clang-x86_64-leptonica',
    'mingw-w64-clang-x86_64-libxml2',
    'mingw-w64-clang-x86_64-openblas',
    'mingw-w64-clang-x86_64-libavif',
    'mingw-w64-clang-x86_64-tesseract-ocr',
    'mingw-w64-clang-x86_64-libyuv',
    'mingw-w64-clang-x86_64-lame',
    'mingw-w64-clang-x86_64-onnx',
    'mingw-w64-clang-x86_64-onnxruntime',
    'mingw-w64-clang-x86_64-opencl-icd',
    'mingw-w64-clang-x86_64-opencl-headers',
    'mingw-w64-clang-x86_64-protobuf',
    'mingw-w64-clang-x86_64-yaml-cpp',
    'mingw-w64-clang-x86_64-onednn'
)

$script:ChocoFailed  = New-Object 'System.Collections.Generic.List[string]'
$script:PacmanFailed = New-Object 'System.Collections.Generic.List[string]'
$script:Msys2Root     = $null
$mklStatus            = 'skipped'

# ===========================================================================
# 0. Execution policy (process scope only, does not persist)
# ===========================================================================
try {
    Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process -Force -ErrorAction Stop
} catch {
    Write-WarnX "Set-ExecutionPolicy (Process) failed: $($_.Exception.Message)"
}

# ===========================================================================
# 1. Elevation - self-elevate via UAC if not admin
# ===========================================================================
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host ''
    Write-Host 'Administrator rights are required (Chocolatey / winget).' -ForegroundColor Yellow
    Write-Host 'Re-launching this script elevated...' -ForegroundColor Yellow
    $scriptPath = $PSCommandPath
    if (-not $scriptPath) { $scriptPath = $MyInvocation.MyCommand.Path }
    $argList = @('-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$scriptPath`"")
    if ($Msys2Path) { $argList += @('-Msys2Path',"`"$Msys2Path`"") }
    if ($SkipChoco) { $argList += '-SkipChoco' }
    if ($SkipMsys2) { $argList += '-SkipMsys2' }
    if ($WhatIf)    { $argList += '-WhatIf' }
    try {
        Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
            -Verb RunAs -ArgumentList ($argList -join ' ')
    } catch {
        Write-FailX "Elevation was declined or failed: $($_.Exception.Message)"
        Write-Note 'Re-run this script from an already-elevated PowerShell prompt.'
        exit 1
    }
    exit 0
}

# ===========================================================================
# Banner
# ===========================================================================
Write-Host ''
Write-Host 'XER toolchain installer - vcpkg -> choco + MSYS2 clang64 + Ninja' -ForegroundColor White
Write-Host 'Source of truth: docs/wiki/research/toolchain-migration-audit.md' -ForegroundColor DarkGray
if ($WhatIf) { Write-Host '*** WHATIF MODE - no changes will be made ***' -ForegroundColor Yellow }

# ===========================================================================
# 2. Detect existing MSYS2 (before choco, so we can skip a duplicate install)
# ===========================================================================
$script:Msys2Root = Find-Msys2Install
if ($Msys2Path -and -not $script:Msys2Root) {
    Write-WarnX "-Msys2Path '$Msys2Path' has no usr\bin\pacman.exe - falling back to auto-detect"
}
if ($script:Msys2Root) {
    Write-Ok "MSYS2 found: $($script:Msys2Root)"
} else {
    Write-Note 'MSYS2 not found yet'
}

# ===========================================================================
# 3. Chocolatey + packages
# ===========================================================================
if ($SkipChoco) {
    Write-Step 'Chocolatey - skipped (-SkipChoco)'
} else {
    Write-Step 'Chocolatey'
    if (Install-Choco) {
        Refresh-MachinePath
        $chocoPkgs = @('nodejs-lts','python3','cmake','ninja')
        # Only install msys2 via choco when no existing install was detected;
        # otherwise we would end up with a duplicate at C:\tools\msys64.
        if (-not $script:Msys2Root) { $chocoPkgs += 'msys2' }
        foreach ($pkg in $chocoPkgs) {
            if (Invoke-External 'choco' @('install',$pkg,'-y','--no-progress')) {
                Write-Ok "choco install $pkg"
            } else {
                Write-FailX "choco install $pkg failed"
                $script:ChocoFailed.Add($pkg)
            }
        }
        if ($chocoPkgs -contains 'msys2') {
            $script:Msys2Root = Find-Msys2Install
            if (-not $script:Msys2Root -and
                (Test-Path 'C:\tools\msys64\usr\bin\pacman.exe')) {
                $script:Msys2Root = 'C:\tools\msys64'
            }
        }
        if ($script:Msys2Root) { Write-Ok "MSYS2 root: $($script:Msys2Root)" }
    } else {
        Write-WarnX 'Chocolatey unavailable - skipping choco packages'
    }
}

# ===========================================================================
# 4. Intel oneMKL (no choco/msys2 package - winget or manual installer)
# ===========================================================================
Write-Step 'Intel oneMKL'
$mklStatus = Install-Mkl
if ($mklStatus -eq 'installed') { Write-Ok 'MKL done' }
elseif ($mklStatus -eq 'skipped-whatif') { Write-Note 'MKL install skipped (whatif)' }
else { Write-WarnX "MKL status: $mklStatus - see messages above" }

# ===========================================================================
# 5. MSYS2 pacman -Syu + clang64 packages
# ===========================================================================
if ($SkipMsys2) {
    Write-Step 'MSYS2 packages - skipped (-SkipMsys2)'
} else {
    Write-Step "MSYS2 clang64 packages ($($script:Msys2Packages.Count) packages)"
    $pacmanExe = $null
    if ($script:Msys2Root) {
        $pacmanExe = Join-Path $script:Msys2Root 'usr\bin\pacman.exe'
    } elseif ($WhatIf) {
        # No install detected: show the commands against the choco default.
        $pacmanExe = 'C:\tools\msys64\usr\bin\pacman.exe'
    }

    if (-not $pacmanExe) {
        Write-FailX 'MSYS2 installation not found - cannot run pacman.'
        Write-Note 'Install Chocolatey + msys2 (re-run without -SkipChoco)'
        Write-Note 'or pass -Msys2Path <root>.'
    } elseif ($WhatIf -or (Test-Path $pacmanExe)) {
        # MSYSTEM=CLANG64 makes pacman behave as the clang64 environment when
        # invoked from PowerShell instead of an msys2 shell.
        $prevMsystem = $env:MSYSTEM
        $env:MSYSTEM = 'CLANG64'
        try {
            # -Syu twice: the first pass may self-update pacman/msys2-runtime,
            # which requires a second pass to finish the transaction.
            $syuFails = 0
            foreach ($try in 1..2) {
                if (-not (Invoke-External $pacmanExe @('-Syu','--noconfirm'))) { $syuFails++ }
            }
            if ($syuFails -eq 2) {
                Write-WarnX 'pacman -Syu failed twice - trying database refresh (-Sy) and continuing'
                Invoke-External $pacmanExe @('-Sy','--noconfirm')
            } elseif ($syuFails -eq 1) {
                Write-WarnX 'one pacman -Syu pass reported an error - continuing anyway'
            }

            # Batched installs; --needed keeps re-runs cheap (idempotent).
            # On batch failure, retry package-by-package to isolate packages
            # that do not exist in the clang64 repo.
            $batchSize = 16
            for ($i = 0; $i -lt $script:Msys2Packages.Count; $i += $batchSize) {
                $end = [Math]::Min($i + $batchSize - 1, $script:Msys2Packages.Count - 1)
                $batch = $script:Msys2Packages[$i..$end]
                Write-Note ("batch {0}: {1} packages" -f [int]($i / $batchSize) + 1, $batch.Count)
                if (-not (Invoke-External $pacmanExe (@('-S','--needed','--noconfirm') + $batch))) {
                    Write-WarnX 'batch install failed - retrying packages individually'
                    foreach ($pkg in $batch) {
                        if (-not (Invoke-External $pacmanExe @('-S','--needed','--noconfirm',$pkg))) {
                            Write-FailX "pacman install failed: $pkg"
                            $script:PacmanFailed.Add($pkg)
                        }
                    }
                }
            }
        } finally {
            $env:MSYSTEM = $prevMsystem
        }
    } else {
        Write-FailX "pacman.exe not found at $pacmanExe"
    }
}

# ===========================================================================
# 6. Vendored deps notice (repo-side, NOT pacman packages)
# ===========================================================================
Write-Step 'Vendored dependencies (repo-side)'
Write-Note 'These vcpkg deps have NO MSYS2 package and must be vendored or'
Write-Note 'shimmed inside the repository (audit "Hard blockers"):'
Write-Note '  - unofficial-pthreadpool  (MIT, tiny)'
Write-Note '  - unofficial-fxdiv        (header-only)'
Write-Note '  - unofficial-fp16         (header-only)'
Write-Note '  - dlfcn-win32             (LGPL-2.1; ~100-line LoadLibrary shim alt.)'
Write-Note 'This installer is tool-only; vendoring is handled repo-side.'

# ===========================================================================
# 7. Verification
# ===========================================================================
Write-Step 'Verification'
Refresh-MachinePath

# --- clang++ (prefer PATH, fall back to clang64\bin directly) ---
$clangOnPath = Get-Command clang++ -ErrorAction SilentlyContinue
$clang64Bin = $null
if ($script:Msys2Root) { $clang64Bin = Join-Path $script:Msys2Root 'clang64\bin' }
$clangExe = $null
if ($clang64Bin) {
    $candidate = Join-Path $clang64Bin 'clang++.exe'
    if (Test-Path $candidate) { $clangExe = $candidate }
}
if ($clangOnPath) {
    $ver = (& clang++ --version 2>$null | Select-Object -First 1)
    Write-Ok "clang++ on PATH: $($clangOnPath.Source) - $ver"
} elseif ($clangExe) {
    $ver = (& $clangExe --version 2>$null | Select-Object -First 1)
    Write-Ok "clang++ found (not on PATH): $clangExe - $ver"
} else {
    Write-FailX 'clang++ not found'
}

# --- cmake / ninja / node / python ---
foreach ($tool in 'cmake','ninja','node') {
    $cmd = Get-Command $tool -ErrorAction SilentlyContinue
    if ($cmd) {
        $ver = (& $tool --version 2>$null | Select-Object -First 1)
        Write-Ok "$tool : $ver ($($cmd.Source))"
    } else {
        Write-WarnX "$tool : not found on PATH"
    }
}
$pyCmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $pyCmd) { $pyCmd = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $pyCmd) { $pyCmd = Get-Command py -ErrorAction SilentlyContinue }
if ($pyCmd) {
    $ver = (& $pyCmd.Source --version 2>$null | Select-Object -First 1)
    Write-Ok "python : $ver ($($pyCmd.Source))"
} else {
    Write-WarnX 'python : not found on PATH'
}

# --- pacman state ---
if ($pacmanExe -and (Test-Path $pacmanExe)) {
    $installed = @(& $pacmanExe -Qq 2>$null | Where-Object { $_ -like 'mingw-w64-clang-x86_64-*' })
    Write-Ok "pacman: $($installed.Count) mingw-w64-clang-x86_64-* packages installed"
    # Note: 'toolchain' is a group, not an installed package - query real
    # package names instead.
    & $pacmanExe -Q mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-libc++ 2>$null |
        ForEach-Object { Write-Note "  $_" }
} elseif (-not $SkipMsys2) {
    Write-Note 'pacman query skipped (no pacman.exe)'
}

# --- PATH note ---
Write-Step 'PATH'
if (-not $clang64Bin) { $clang64Bin = 'C:\tools\msys64\clang64\bin' }
$allPath = ([Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User'))
if ($allPath -notlike "*$clang64Bin*") {
    Write-WarnX "clang64\bin is not on PATH. Add it (User scope) so clang++/lld resolve:"
    Write-Note "  $clang64Bin"
    Write-Note '  PowerShell one-liner:'
    Write-Note "    [Environment]::SetEnvironmentVariable('Path', ([Environment]::GetEnvironmentVariable('Path','User') + ';$clang64Bin'), 'User')"
} else {
    Write-Ok "clang64 bin already on PATH: $clang64Bin"
}

# ===========================================================================
# 8. Summary
# ===========================================================================
Write-Step 'Summary'
$chocoMsg = if ($script:ChocoFailed.Count -gt 0) {
    "$($script:ChocoFailed.Count) failed -> $($script:ChocoFailed -join ', ')"
} else { 'all ok (or skipped)' }
Write-Note "choco failures : $chocoMsg"
$pacmanMsg = if ($script:PacmanFailed.Count -gt 0) {
    "$($script:PacmanFailed.Count) failed -> $($script:PacmanFailed -join ', ')"
} else { 'all ok (or skipped)' }
Write-Note "pacman failures: $pacmanMsg"
Write-Note "MKL             : $mklStatus"
Write-Note ''
Write-Note 'Next steps are repo-side (see docs/wiki/decisions/toolchain-migration.md'
Write-Note 'and docs/wiki/research/toolchain-migration-audit.md):'
Write-Note '  - new preset windows-clang-msys2 + toolchain file'
Write-Note '  - ~20 shim configs under cmake/deps/configs/'
Write-Note '  - vendored deps: pthreadpool, fxdiv, fp16, dlfcn-win32'
Write-Note '  - pragma-comment(lib) link audit for mingw'
Write-Note '  - skia Ganesh-D3D12 header check (HIGH RISK per audit)'
Write-Note '  - opencv is 5.0.0 in msys2 (vcpkg had 4.x) - API-break risk'

$hadFailures = ($script:ChocoFailed.Count -gt 0) -or
               ($script:PacmanFailed.Count -gt 0) -or
               ($mklStatus -in @('failed','manual'))
if ($hadFailures) { exit 1 } else { exit 0 }
