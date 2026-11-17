# CMake groundwork report — vcpkg → MSYS2 clang64 migration

**Date:** 2026-11-15. **Scope:** repo-side groundwork only (audit §6 items
1–4). Additive — the `default` vcpkg preset is unchanged and still
configures. Nothing committed; branch `feature/xer-studio-1c-export`.

## Files created

| File | Purpose |
|---|---|
| `cmake/platforms/windows-msys2-clang64.cmake` | Toolchain file: `CMAKE_SYSTEM_NAME Windows` (native, all `if(WIN32)` paths live), `clang`/`clang++` compilers, `XER_MSYS2_ROOT` (default `C:/tools/msys64`, cache-overridable), clang64 `bin/` prepended to `PATH`, `CMAKE_PREFIX_PATH += <clang64>` (covers `include/`, `lib/`, `lib/cmake/<pkg>`), `PKG_CONFIG_LIBDIR`/`PKG_CONFIG_PATH` pinned to `clang64/lib/pkgconfig`, `PKG_CONFIG_SYSROOT_DIR` set for `.pc` `prefix=/clang64` → Windows path rewrite (**VERIFY-on-spike**). No `CMAKE_SYSTEM_PROCESSOR`, no `CMAKE_MSVC_RUNTIME_LIBRARY`. |
| `CMakePresets.json` | New `windows-clang-msys2` configure preset (Ninja, toolchain file, `XER_USE_SYSTEM_DEPS=ON`, `XER_FETCH_DEPENDENCIES=OFF`, `ENABLE_MEDIA=ON`, `ENABLE_V8/CUDA/CUDNN=OFF`, `BUILD_TESTING=ON`, Release) + matching build preset. Standalone (no inherits), same JSON style as `linux-release`. |
| `cmake/deps/configs/xer_defer_vcpkg_config.cmake` | Shared helper included by shims: if a partial-vcpkg install is active (`XER_VCPKG_ROOT`/`XER_VCPKG_TRIPLET`), looks for the package's **real** config under `installed/<triplet>/share/<pkg>/` and `include()`s it — so a shim never shadows an authoritative vcpkg config on Linux/macOS. No-op on MSYS2. |
| `cmake/deps/configs/unofficial-fxdiv/unofficial-fxdivConfig.cmake` | Header-only `fxdiv.h`. Vendor-first (`third_party/fxdiv`, `include/`, `include/fxdiv/`), then `find_path`. Target `unofficial::fxdiv::fxdiv` + `FXDIV_USE_INLINE_ASSEMBLY=0` (mirrors vcpkg). |
| `cmake/deps/configs/unofficial-fp16/unofficial-fp16Config.cmake` | Header-only `fp16.h`, same vendor/probe pattern → `unofficial::fp16::fp16`. Also resolves optional `unofficial::psimd::psimd` (real config → `find_path(psimd.h)`) since the vcpkg port links it; links it when present. |
| `cmake/deps/configs/unofficial-pthreadpool/unofficial-pthreadpoolConfig.cmake` | **Compiled** dep. Priority: vcpkg-defer → vendored `third_party/pthreadpool/pthreadpool.c` (or `src/`) built as STATIC `unofficial::pthreadpool` (PIC, `c_std_11`, `Threads::Threads`) → `find_library`/`find_path` fallback (UNKNOWN IMPORTED). Links `unofficial::fxdiv::fxdiv` per vcpkg parity — uses `INTERFACE_LINK_LIBRARIES` for imported targets, `target_link_libraries` for the vendored static lib. |
| `cmake/deps/configs/unofficial-skia/unofficial-skiaConfig.cmake` | vcpkg-defer → `pkg_check_modules(skia)` (MSYS2 `.pc`) → `find_path` probes for all 3 include layouts the code uses (`core/SkCanvas.h`, `include/core/SkCanvas.h`, `SkCanvas.h`) + `find_library(skia)`. Target `unofficial::skia::skia` requires **both** headers and a link line (else configure-time not-found). **HIGH RISK — VERIFY.** |
| `cmake/deps/configs/range-v3/range-v3Config.cmake` | Header-only → `range-v3::range-v3` (+`cxx_std_14`). |
| `cmake/deps/configs/cereal/cerealConfig.cmake` | Header-only → `cereal::cereal` (+`cxx_std_11`). |
| `cmake/deps/configs/RapidJSON/RapidJSONConfig.cmake` | Header-only → `RapidJSON`. Deliberately does **not** set `RAPIDJSON_INCLUDE_DIRS` (would double-`add_library` vs `system_deps.cmake`'s fallback block). |
| `cmake/deps/configs/VulkanUtilityLibraries/VulkanUtilityLibrariesConfig.cmake` | `find_path(vulkan/utility/vk_safe_struct.hpp)` + `vulkan/vulkan_core.h`; creates `Vulkan::Headers` when no vulkan-headers config exists, plus `Vulkan::CompilerConfiguration` (vcpkg compile defs, MSVC warning flags dropped), `Vulkan::UtilityHeaders`, and `Vulkan::SafeStruct`/`Vulkan::LayerSettings` as UNKNOWN IMPORTED when `libVulkanSafeStruct`/`libVulkanLayerSettings` are found, else INTERFACE fallback. |
| `cmake/deps/configs/VulkanMemoryAllocator/VulkanMemoryAllocatorConfig.cmake` | `find_path` for both `vk_mem_alloc.h` and `vma/vk_mem_alloc.h` layouts → `GPUOpen::VulkanMemoryAllocator` INTERFACE (vcpkg parity: include dir pointed so `<vk_mem_alloc.h>` resolves). |

## Files modified

- **`CMakeLists.txt` (~L22-56):** dep selection re-keyed. `XER_USE_SYSTEM_DEPS`
  is now an `option()` on **all** platforms — `OFF` on Windows, `ON` on
  Linux/macOS (unchanged defaults). `if(XER_USE_SYSTEM_DEPS) → include
  system_deps.cmake; elseif(WIN32) → <original vcpkg block verbatim>`.
  Windows presets that don't set the var take the identical vcpkg path;
  `CMAKE_MSVC_RUNTIME_LIBRARY`/`VCPKG_*`/toolchain defaulting only run in
  the vcpkg branch, so the msys2 preset never sees them.
- **`cmake/deps/system_deps.cmake`:** header comments updated (file now
  servable on Windows); added `elseif(WIN32) → XER_VCPKG_TRIPLET
  "x64-mingw-dynamic"` default (new code path only — Windows never reached
  it before).
- **`CMakePresets.json`:** +21 lines (preset) as above.

## Shim confidence levels

| Shim | Confidence | Notes |
|---|---|---|
| unofficial-fxdiv | **High** | header-only; verified via fake-package probe + vcpkg-defer path. Needs vendored `third_party/fxdiv` on MSYS2 (audit hard-blocker). |
| unofficial-fp16 | **Med-High** | same pattern; psimd linkage optional. VERIFY whether fp16.h compiles without psimd.h on MSYS2. |
| unofficial-pthreadpool | **Med** | vendored-source branch untested end-to-end (no `third_party/` per scope); fallback path verified. Needs `third_party/pthreadpool` vendored. |
| unofficial-skia | **Low-Med** | `.pc` name (`skia`?) and include layout unverified; Ganesh D3D12 headers are the known HIGH-RISK item from the audit. Shim requires headers **and** lib — fails configure cleanly if either missing. |
| range-v3, cereal, RapidJSON | **High** | header-only `find_path`; verified. |
| VulkanUtilityLibraries | **Med** | header probes + optional static libs; VERIFY MSYS2 package actually ships `libVulkanSafeStruct`/`libVulkanLayerSettings` and the `vulkan/utility|layer` layout. |
| VulkanMemoryAllocator | **High** | single header, both layouts probed; verified. |

## Existing shims — no changes needed (VERIFY `.pc` names on spike)

`unofficial-sqlite3` (`sqlite3.pc`), `unofficial-brotli`
(`libbrotlidec/libbrotlienc/libbrotlicommon.pc`), `unofficial-libffi`
(`libffi.pc`), `unofficial-libmupdf` (`mupdf.pc`), `unofficial-shaderc`
(`shaderc_combined.pc`), `libyuv`→`yuv` target (verified working under
mingw find_library rules), plus `FFTW3f`, `MKL`, `libuv`, `re2`, `lz4`,
`opencl`, `unofficial-xnnpack`. All are pkg-config/find_library based and
OS-agnostic; MSYS2 `.pc` filenames are the only uncertainty.

**`unofficial-libyuv`:** no such package/target exists — code uses
`find_package(libyuv)` + `yuv` target; existing `libyuv/` shim covers it.

## Verification performed

- `out/vcenv.bat cmake --preset default` → **Configures + generates
  cleanly** (110s; only pre-existing vcpkg policy warnings). `default`
  behavior unchanged.
- `cmake --list-presets` → `windows-clang-msys2` registered.
- `cmake -P` on the toolchain file → parses (caught+fixed an
  `$ENV{PATH}` syntax bug).
- Throwaway shim harness (`out/shim-check`, gitignored, since removed):
  - **No deps:** all shims return FOUND FALSE gracefully, no errors.
  - **Fake package tree** (headers + `.dll.a`): all 9 new shims → FOUND
    TRUE with correct targets (caught+fixed two real bugs: `find_dependency`
    `return()` killing skia's fallback probes; `target_link_libraries`
    PUBLIC on an IMPORTED pthreadpool target; plus a `find_library` cache-var
    collision in the VUL loop).
  - **vcpkg-defer path** (`XER_VCPKG_ROOT/TRIPLET` + share on prefix path):
    unofficial-fxdiv/fp16/pthreadpool/skia, range-v3, cereal, RapidJSON all
    resolve through the real vcpkg configs — Linux behavior preserved.
    (VUL defer hits a pre-existing `VulkanHeaders` dir-name quirk in the
    test env — unrelated to the shim.)

## Needs spike verification (when MSYS2 packages are installed)

1. **skia**: does `clang64/lib/pkgconfig/skia.pc` exist; does the package
   ship `gpu/ganesh/d3d/*` headers; are all 3 include spellings covered.
2. **`.pc` prefix handling**: `PKG_CONFIG_SYSROOT_DIR` rewrite of
   `prefix=/clang64` → `C:/tools/msys64/clang64/...` — confirm pkgconf
   output is sane; drop the var if clang resolves `/clang64` natively.
3. **`.pc` names** for the pkg-config shims listed above.
4. **MKL**: `find_package(MKL CONFIG REQUIRED)` at root will FAIL on
   MSYS2 — no `.pc`, no cmake config (oneAPI installer needed, or relax to
   OpenBLAS per audit). Known configure blocker, not shim-fixable.
5. **pthreadpool/fxdiv/fp16 vendoring**: `third_party/` dirs still needed
   (audit hard-blockers); dlfcn-win32 also pending (separate task).
6. **Toolchain details**: `ar`/`ranlib`/`lld` resolution from clang64/bin;
   whether bare `clang`/`clang++` resolve correctly via the PATH prepend.
7. Folly/libc++ compile risk, OpenCV 5 API breaks, `_s` CRT functions,
   `#pragma comment(lib)` audit — all per audit §spike, untouched here.

## Not done (per scope)

No vcpkg references removed, no `src/`/`tests/`/`tools/` touched, no build
run against the new preset (deps absent — expected to fail until
InstallTools.ps1 + spike), no `vendor/`/`third_party/` dirs created, no
commits.
