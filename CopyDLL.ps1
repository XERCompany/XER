<#
.SYNOPSIS
    Copy all DLLs a PE executable needs next to it (windeploy-style) for
    MSYS2/clang64 builds — dependencies are discovered with LLVM tools,
    not guessed.

.DESCRIPTION
    Parses the PE import table of -ExePath with llvm-objdump (fallback:
    llvm-readobj, then objdump), resolves each imported DLL against the
    MSYS2 toolchain dirs + PATH, copies non-system DLLs to -DestDir and
    recurses into them so transitive deps (e.g. everything FFmpeg's
    avformat-63.dll pulls: zlib, x264, openssl, iconv, ...) come along.

    A DLL is classified as "system" (skipped) when it is only resolvable
    in Windows\System32 / api-ms-win-* / ext-ms-* API-set names — those
    ship with Windows and must not be redistributed from MSYS2.

    DLLs loaded at runtime via LoadLibrary (MKL for the object lab,
    VST3 plugins) cannot appear in the import table — pass them via
    -Extra if present.

.EXAMPLE
    .\CopyDLL.ps1 -ExePath out\build\windows-clang-msys2\bin\video_editor.exe

.EXAMPLE
    .\CopyDLL.ps1 -ExePath out\build\windows-clang-msys2\bin\video_editor.exe `
        -DestDir dist\video_editor -CopyExe -Extra mkl_rt.dll -Verbose
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,

    # Where the dependencies land. Default: the exe's own directory.
    [string]$DestDir,

    # Also copy the executable itself (useful for a dist/ folder).
    [switch]$CopyExe,

    # MSYS2 installation root; DLLs are searched in <root>\<flavor>\bin.
    [string]$MsysRoot = 'C:\tools\msys64',

    # Search flavors in priority order.
    [string[]]$Flavors = @('clang64', 'ucrt64', 'mingw64'),

    # Extra directories to search before PATH.
    [string[]]$SearchPath = @(),

    # Extra DLL names to copy if resolvable (LoadLibrary'd deps).
    [string[]]$Extra = @(),

    # Aliases for import names that don't exist under that exact name,
    # in 'imported=disk' form — MSYS2's libdovi.dll imports "dovi.dll"
    # while shipping only libdovi.dll:
    #   -Alias 'dovi.dll=libdovi.dll','rav1e.dll=librav1e.dll'
    # copies the real file under the requested import name.
    [string[]]$Alias = @(),

    # Explicit llvm-objdump path; auto-detected when empty.
    [string]$Objdump,

    # Only report, do not copy anything.
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ---------------------------------------------------------------- tools
function Find-Objdump {
    if ($Objdump -and (Test-Path -LiteralPath $Objdump)) { return $Objdump }
    foreach ($flavor in $Flavors) {
        foreach ($tool in 'llvm-objdump.exe', 'llvm-readobj.exe', 'objdump.exe') {
            $candidate = Join-Path $MsysRoot "$flavor\bin\$tool"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    foreach ($tool in 'llvm-objdump.exe', 'llvm-readobj.exe', 'objdump.exe') {
        $cmd = Get-Command $tool -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    throw 'No LLVM objdump found — install llvm or point -Objdump at it'
}

# Return the imported DLL names of a PE file via llvm-objdump -p
# (llvm-readobj shares the "DLL Name:" output under --coff-imports).
function Get-ImportedDlls {
    param([string]$Binary, [string]$Tool)
    if ($Tool -like '*llvm-readobj*') {
        $out = & $Tool --coff-imports $Binary 2>$null
    } else {
        $out = & $Tool -p $Binary 2>$null
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "objdump failed ($LASTEXITCODE): $Binary"
        return @()
    }
    $out | Select-String -Pattern '^\s*DLL Name:\s*(\S+)' -AllMatches |
        ForEach-Object { $_.Matches[0].Groups[1].Value } |
        Where-Object { $_ } | Sort-Object -Unique
}

# ----------------------------------------------------------- resolution
$systemDirs = @(
    "$env:SystemRoot\System32",
    "$env:SystemRoot\SysWOW64",
    "$env:SystemRoot\WinSxS"
)
$searchDirs = [System.Collections.Generic.List[string]]::new()
$searchDirs.Add((Split-Path -Parent (Resolve-Path -LiteralPath $ExePath)))
foreach ($flavor in $Flavors) { $searchDirs.Add("$MsysRoot\$flavor\bin") }
foreach ($dir in $SearchPath) { $searchDirs.Add($dir) }
foreach ($dir in ($env:PATH -split ';')) {
    if ($dir.Trim() -and (Test-Path -LiteralPath $dir.Trim())) {
        $searchDirs.Add($dir.Trim())
    }
}
$searchDirs = [string[]]($searchDirs | Where-Object {
        Test-Path -LiteralPath $_ } | Select-Object -Unique)

function Find-Dll {
    param([string]$Name)
    foreach ($dir in $searchDirs) {
        $hit = Join-Path $dir $Name
        if (Test-Path -LiteralPath $hit) {
            return (Resolve-Path -LiteralPath $hit).Path
        }
    }
    return $null
}

function Test-SystemDll {
    param([string]$Name, [string]$ResolvedPath)
    if ($Name -match '^(api-ms-win|ext-ms)-') { return $true }
    if ($ResolvedPath) {
        foreach ($dir in $systemDirs) {
            if ($ResolvedPath -like "$dir\*") { return $true }
        }
        return $false
    }
    # Unresolvable but only expected to exist in Windows: check System32.
    foreach ($dir in $systemDirs[0..1]) {
        if (Test-Path -LiteralPath (Join-Path $dir $Name)) { return $true }
    }
    return $false
}

# ----------------------------------------------------------------- main
$exe = (Resolve-Path -LiteralPath $ExePath).Path
if (-not $DestDir) { $DestDir = Split-Path -Parent $exe }
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $DestDir | Out-Null }

$tool = Find-Objdump
Write-Verbose "Dependency scanner: $tool"

$aliasMap = @{}
# Each element may itself be a comma-joined list — this lets CMake and
# `powershell -File` invocations pass "-Alias 'a=b,c=d'" as one token.
foreach ($pair in ($Alias -split ',')) {
    $idx = $pair.IndexOf('=')
    if ($idx -gt 0) {
        $aliasMap[$pair.Substring(0, $idx).Trim().ToLowerInvariant()] =
            $pair.Substring($idx + 1).Trim()
    }
}

$pending = [System.Collections.Generic.Queue[string]]::new()
$seen = @{}          # lower-name -> 'copied' | 'system' | 'missing'
Get-ImportedDlls -Binary $exe -Tool $tool | ForEach-Object {
    $pending.Enqueue($_) }
foreach ($name in $Extra) { $pending.Enqueue($name) }

$copied = [System.Collections.Generic.List[string]]::new()
$missing = [System.Collections.Generic.List[string]]::new()
$system = [System.Collections.Generic.List[string]]::new()

while ($pending.Count -gt 0) {
    $name = $pending.Dequeue()
    $key = $name.ToLowerInvariant()
    if ($seen.ContainsKey($key)) { continue }

    $path = Find-Dll $name
    if (Test-SystemDll -Name $name -ResolvedPath $path) {
        $seen[$key] = 'system'
        $system.Add($name) | Out-Null
        Write-Verbose "system  $name"
        continue
    }
    if (-not $path -and $aliasMap.ContainsKey($key)) {
        $path = Find-Dll $aliasMap[$key]
    }
    if (-not $path) {
        $seen[$key] = 'missing'
        $missing.Add($name) | Out-Null
        Write-Warning "missing  $name (imported, not found on search paths)"
        continue
    }

    $dest = Join-Path $DestDir $name
    $same = (Test-Path -LiteralPath $dest) -and
            ((Get-Item -LiteralPath $dest).Length -eq
             (Get-Item -LiteralPath $path).Length)
    $seen[$key] = 'copied'
    if (-not $same) {
        $copied.Add($name) | Out-Null
        if (-not $DryRun) {
            Copy-Item -LiteralPath $path -Destination $dest -Force
        }
    }
    Write-Verbose "copied  $name  <- $path"

    # Recurse into the dependency's own import table.
    foreach ($dep in (Get-ImportedDlls -Binary $path -Tool $tool)) {
        if (-not $seen.ContainsKey($dep.ToLowerInvariant())) {
            $pending.Enqueue($dep)
        }
    }
}

if ($CopyExe -and -not $DryRun) {
    Copy-Item -LiteralPath $exe -Destination (Join-Path $DestDir (Split-Path -Leaf $exe)) -Force
}

Write-Host ''
Write-Host ("copied : {0,3}  -> {1}" -f $copied.Count, (Resolve-Path $DestDir -ErrorAction SilentlyContinue))
Write-Host ("system : {0,3}   (left to Windows)" -f $system.Count)
Write-Host ("missing: {0,3}" -f $missing.Count)
if ($missing.Count -gt 0) {
    Write-Warning ('Missing DLLs: ' + ($missing -join ', '))
}
if ($DryRun) { Write-Host '(dry run — nothing was written)' }
