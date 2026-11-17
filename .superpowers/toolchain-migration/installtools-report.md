# InstallTools.ps1 — write report

**Date:** 2026-11-16. **Status:** file written, syntax-verified (parser
check only — installer NOT executed, per instructions).

## File written

- `D:\Developers\XER3\InstallTools.ps1` (~600 lines, PS 5.1 compatible,
  no CmdletBinding/common-param conflicts; custom `-WhatIf` switch).

## Package counts

- **choco (5 max):** `nodejs-lts`, `python3`, `cmake`, `ninja`, `msys2`
  (msys2 only when no existing install is detected — avoids a duplicate
  at `C:\tools\msys64`).
- **MKL (1):** `winget install -e --id Intel.oneMKL --silent
  --accept-package-agreements --accept-source-agreements`; on missing
  winget or non-zero exit prints the Intel download URL
  (`.../onemkl/download.html`) + component-installer id
  `intel.oneapi.win.mkl.devel`.
- **pacman: 63 unique `mingw-w64-clang-x86_64-*` names**, installed via
  `pacman -S --needed --noconfirm` in 4 batches of 16, with
  per-package fallback on batch failure (isolates any name that does
  not exist in clang64). Composition:
  - toolchain group `mingw-w64-clang-x86_64-toolchain` (17 members incl.
    clang/lld/llvm/libc++/compiler-rt — replaces plan's
    llvm/llvm-libs/llvm-tools/lld) + explicit `libc++`, `pkgconf`,
    `cmake`, `ninja`;
  - corrected plan list (16): zlib, openssl, icu, ffmpeg, libuv,
    range-v3, re2, abseil-cpp, flatbuffers, jsoncpp, fmt, skia, opencv,
    libraw, fftw, ggml — `lua54` dropped (repo FetchContent-builds Lua);
  - audit "required deps missing from plan" (44): sqlite, zstd, brotli,
    curl, boost, eigen3, cereal, mimalloc, folly, pugixml, libarchive,
    assimp, libmupdf, libffi, shaderc, spirv-tools, glslang,
    vulkan-headers, vulkan-loader, vulkan-memory-allocator,
    vulkan-utility-libraries, asmjit, nlohmann-json, rapidjson, tinyxml2,
    chrono-date, lz4, snappy, leptonica, libxml2, openblas, libavif,
    tesseract-ocr, libyuv, lame, onnx, onnxruntime, opencl-icd,
    opencl-headers, protobuf, yaml-cpp, onednn.

  Note: audit has no literal "§8"; the corrected list was assembled as
  (plan's 21, corrected) + (audit's "required deps missing" list).
  User estimate "~55" vs actual 63 — the full audit set was kept.

## Decisions made (documented in script header comments)

- **Skipped `visualstudio2026buildtools`** — clang64/mingw path has no
  MSVC dependency; only needed for clang-cl/CUDA, both dropped (audit:
  nvcc requires cl.exe, no mingw path).
- **Skipped choco `llvm`** — clang-cl = MSVC ABI, incompatible with
  mingw-w64 clang64 packages; toolchain comes from the MSYS2 group.
- **Skipped choco `cuda`, `nasm`** — CUDA dead on this path; nasm was
  only for CUDA/asm.
- **Skipped pacman `lua54`** — repo FetchContent-builds Lua.
- **MSYS2 detection order:** `-Msys2Path` → `$env:MSYSTEM_PREFIX` →
  `C:\tools\msys64` (choco default) → `C:\msys64` (installer default) →
  uninstall-registry scan (Inno uninstaller path walk, 3 parents) →
  `<drive>:\msys64` on all fixed drives. Chocolatey uninstall entries
  point at choco.exe and simply fail validation — harmless.
- **pacman invocation:** `usr\bin\pacman.exe` with `MSYSTEM=CLANG64`
  env for the child; `-Syu --noconfirm` run twice (self-update pass),
  `-Sy` fallback if both fail, then batched `-S --needed`.
- **Continue-on-error everywhere:** choco per-package, pacman
  batch→per-package fallback; script exits 1 only if any package/MKL
  failed, 0 otherwise. Nothing aborts the whole run.
- **No PATH mutation** — script prints the `clang64\bin` User-scope
  `SetEnvironmentVariable` one-liner instead.
- **WhatIf:** prints every native command (choco bootstrap, choco
  installs, winget, pacman batches) without executing; still runs
  read-only verification.
- **Self-elevation:** UAC relaunch preserves all four params;
  execution-policy bypass set at Process scope in-script AND on the
  elevated command line.

## Verification performed

- PowerShell parser check: **PARSE OK** (no execution).
- Package-name spot-checks against packages.msys2.org (clang64 repo):
  `onednn`, `vulkan-memory-allocator`, `onnxruntime`, `chrono-date`,
  `opencl-icd`, `tesseract-ocr`, `folly`, `libmupdf`, `asmjit`, `ggml`
  — **all 10 confirmed present**; `mingw-w64-clang-x86_64-toolchain`
  group confirmed (17 members). Remaining ~52 names are standard
  mingw-w64 packages per the audit; per-package fallback covers any
  surprise.

## Uncertainties / for the parent to note

- `winget` package id `Intel.oneMKL` taken from audit; not verified
  live (winget may need `--source winget` or a different id on some
  systems — failure path prints the manual URL).
- `pacman -Syu` from a non-msys2 shell can occasionally fail on
  self-update/locks; script retries once and falls back to `-Sy`.
- skia Ganesh-D3D12 header gap and opencv 5.0-vs-4.x API risk remain
  repo-side concerns flagged by the audit (mentioned in summary output).
- Script was NOT run (requires admin + would download ~GBs); first live
  run is the deferred spike per the decision doc.
- Not committed — left for parent.
