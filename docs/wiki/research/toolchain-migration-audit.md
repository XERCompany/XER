# Toolchain migration audit — choco + msys2 + clang64

**Date:** 2026-11-15. **Source:** `ForceChangeManagerPackages.md` +
subagent feasibility audit. **Verdict: FEASIBLE with corrections** —
MSYS2 `clang64` covers ~90% of deps incl. folly/ggml/onnxruntime
(surprise), but the plan's package list is incomplete and contains an
ABI self-contradiction.

## Toolchain decision (audit recommendation)

**Choose clang-mingw = MSYS2 `clang64` repo** as the new preset's
toolchain. It is the ONLY self-consistent option: all msys2 packages
are Itanium-ABI/libc++/`.dll.a` — incompatible with MSVC ABI.

- Choco `llvm` (clang-cl) is MSVC-ABI — only useful for a SEPARATE
  clang-cl preset; do NOT mix with clang64 packages.
- CUDA/cuDNN/V8 have NO mingw path (nvcc requires MSVC cl.exe;
  `cmake/v8.cmake` hard-requires vcpkg triplet). Keep them OFF on the
  new preset; **keep vcpkg presets** (`default`, `v8`, `v8-xer`) for
  full-feature builds — do NOT delete vcpkg despite "полностью
  перестроить".
- `ENABLE_CUDA=OFF ENABLE_CUDNN=OFF ENABLE_V8=OFF` on new preset;
  bridges self-disable.

## Choco corrections

- `visualstudio2026buildtools` EXISTS (VS2026=MSVC 18.x) but default
  installs MSBuild workload only — MUST pass
  `--package-parameters "--add Microsoft.VisualStudio.Workload.VCTools
  --includeRecommended"` (only needed for clang-cl/CUDA paths anyway).
- **`cmake` missing from plan** — add `choco install cmake`.
- MKL: NO choco package → `winget install -e --id Intel.oneMKL` or
  Intel component installer `intel.oneapi.win.mkl.devel`.
- msys2 choco pkg installs to `C:\tools\msys64`; run `pacman -Syu`
  before package installs.

## MSYS2 `clang64` coverage

Plan's 21 packages: ALL exist in clang64. Caveats:
- llvm/llvm-libs/llvm-tools/lld REDUNDANT — use group
  `mingw-w64-clang-x86_64-toolchain` + `libc++` instead.
- `lua54` unnecessary (repo FetchContent-builds Lua).
- `skia` EXISTS but pkg-config-only + minimal GN args — **HIGH RISK**:
  repo needs Ganesh D3D12 headers (`gpu/ganesh/d3d/*`,
  `canvas.cc:36-47`); verify package contents before committing.
- `opencv` is **5.0.0** (vcpkg had 4.x) — API-break risk MEDIUM.
- `ggml` exists but NO CUDA backend (CPU+BLAS+OpenCL+Vulkan only).

Required deps MISSING from plan's list — all exist in clang64:
sqlite, zstd, brotli, curl, boost, eigen3, cereal, mimalloc, ggml,
folly (!), pugixml, libarchive, assimp, libmupdf (AGPL), libffi,
shaderc, spirv-tools, glslang, vulkan-headers/loader/memory-allocator/
utility-libraries, asmjit, nlohmann-json, rapidjson, tinyxml2,
chrono-date (provides `date` config — name differs from vcpkg `date`),
lz4, snappy, leptonica, libxml2, openblas, libavif, tesseract-ocr,
libyuv, lame, onnx, onnxruntime, opencl-icd, opencl-headers,
protobuf, yaml-cpp, onednn, pkgconf, cmake, ninja.

**Hard blockers (REQUIRED, NOT in msys2 — must vendor/shim):**
1. `unofficial-pthreadpool` — vendor Maratyszcza/pthreadpool (MIT, tiny)
2. `unofficial-fxdiv` — vendor headers (header-only)
3. `unofficial-fp16` — vendor headers (header-only)
4. `dlfcn-win32` — vendor (LGPL-2.1, same exposure as vcpkg port) or
   ~100-line LoadLibrary shim; needed by 11 compiled .cc files
   (pxer_host, vulkan_renderer, vr_*, nvml_loader, cudnn_bridge, …)
5. `intel-mkl` — oneAPI installer; C API only → should link via lld;
   prefer `MKL_LINK=dynamic` (single mkl_rt import lib) over static.
   Worst case relax `find_package(MKL REQUIRED)` → OpenBLAS
   (feature loss: no DFTI/VML).

**Soft losses (optional, self-gating — acceptable):** xnnpack, pffft,
tinyexr, kineto, Dawn, fbgemm, blaze, json-schema-validator,
level-zero, cudnn.

## Repo-side changes needed

1. New toolchain `cmake/platforms/windows-msys2-clang64.cmake`
   (~15 lines, model on `linux-x64.cmake`; `WIN32` stays true →
   `if(WIN32)` paths live).
2. Root `CMakeLists.txt:25-43`: re-key `if(WIN32)→vcpkg` vs
   `system_deps.cmake` — include system_deps when
   `XER_USE_SYSTEM_DEPS=ON` on ANY OS (file comment "Windows never
   includes this" breaks).
3. New preset `windows-clang-msys2`: Ninja + toolchain file +
   `XER_USE_SYSTEM_DEPS=ON` + `XER_FETCH_DEPENDENCIES=OFF` +
   V8/CUDA/CUDNN OFF + `ENABLE_MEDIA=ON` + `BUILD_TESTING=ON`.
4. ~20 shim configs under `cmake/deps/configs/` — same pattern as
   Linux; header-only/no-config packages are the workload
   (skia, libmupdf, sqlite3, brotli, libffi, shaderc, pthreadpool,
   fxdiv, fp16, libuv, FFTW3f, libyuv `yuv` target, folly, ggml,
   mimalloc, cereal, range-v3, FlatBuffers, LZ4, Snappy, date,
   Tesseract, Leptonica, ICU, LibArchive, assimp, protobuf,
   yaml-cpp, glslang, nlohmann_json, opencl, openblas, onednn,
   libxml2, pugixml, asmjit, libavif, lame, onnxruntime).
5. `#pragma comment(lib)` audit: ~14 files rely on MSVC pragma —
   under mingw ignored → dwmapi/gdiplus/iphlpapi/winhttp/bthprops/
   setupapi/wlanapi likely need explicit `target_link_libraries`.
6. `_s` CRT functions (~6 sites) — mingw-w64 ships MS-compatible
   impls; verify link.
7. Runtime: clang64 packages are mostly DLLs → PATH or copy step for
   `Editor.exe`+tools (similar burden to vcpkg DLLs).
8. `XER_BUILD_LLVM` is dead option (declared, never referenced).

## Portability notes (clang-mingw vs clang-cl)

- `if(MSVC)` false → `-Wall -Wextra -Wpedantic` fallbacks already exist.
- `_MSC_VER` undefined → `xer_sscanf` macros take sscanf branch ✅.
- No `__try/__except`/`__asm` found ✅.
- folly under libc++ = main compile risk (notoriously picky).

## Recommended spike before InstallTools.ps1

Install package set → `find_package` probes → check
`/clang64/include/skia/include/gpu/ganesh/d3d/` exists (D3D Ganesh)
→ MKL dynamic-link smoke test → pragma-comment link audit.
