# Vendored third-party dependencies

Small dependencies that have **no MSYS2 clang64 package** and are required by
the `windows-clang-msys2` preset (`XER_USE_SYSTEM_DEPS=ON`; see
`docs/wiki/research/toolchain-migration-audit.md` §2b/§3 — "hard blockers").
Each directory contains only the files needed to build; upstream tests,
benchmarks, CI configs and language bindings were not vendored.

Provenance was taken from the matching vcpkg ports (`C:/vcpkg/ports/<name>`)
so the vendored code is bit-identical to what the vcpkg presets already build.

| Directory | Package | Upstream | Pinned ref | License |
|---|---|---|---|---|
| `pthreadpool/` | unofficial-pthreadpool | https://github.com/Maratyszcza/pthreadpool | `a56dcd79c699366e7ac6466792c3025883ff7704` (= vcpkg port ref) | BSD-2-Clause (`LICENSE`) |
| `fxdiv/` | unofficial-fxdiv | https://github.com/Maratyszcza/fxdiv | `63058eff77e11aa15bf531df5dd34395ec3017c8` (= vcpkg port ref) | MIT (`LICENSE`) |
| `fp16/` | unofficial-fp16 | https://github.com/Maratyszcza/fp16 | `4dfe081cf6bcd15db339cf2680b9281b8451eeb3` (= vcpkg port ref) | MIT (`LICENSE`) |
| `dlfcn-win32/` | dlfcn-win32 | https://github.com/dlfcn-win32/dlfcn-win32 | tag `v1.4.2` (= vcpkg port version) | MIT (`COPYING`) |

Download URLs used (GitHub archive tarballs):

- `https://github.com/Maratyszcza/pthreadpool/archive/a56dcd79c699366e7ac6466792c3025883ff7704.tar.gz`
- `https://github.com/Maratyszcza/fxdiv/archive/63058eff77e11aa15bf531df5dd34395ec3017c8.tar.gz`
- `https://github.com/Maratyszcza/fp16/archive/4dfe081cf6bcd15db339cf2680b9281b8451eeb3.tar.gz`
- `https://github.com/dlfcn-win32/dlfcn-win32/archive/refs/tags/v1.4.2.tar.gz`

## How each package is consumed (windows-clang-msys2)

### pthreadpool — compiled, `libxer_pthreadpool.a`

- **Entry point:** `find_package(unofficial-pthreadpool CONFIG REQUIRED)`
  in the root `CMakeLists.txt` resolves to the shim
  `cmake/deps/configs/unofficial-pthreadpool/unofficial-pthreadpoolConfig.cmake`.
- **Shim builds:** static lib `xer_pthreadpool` (alias
  `unofficial::pthreadpool`, matching the vcpkg target name) from
  `src/{portable-api,memory,pthreads,fastpath,legacy-api}.c` —
  the upstream ≥2024 split layout. `src/shim.c` is Emscripten-only and is
  intentionally not compiled; `legacy-api.c` carries the `pthreadpool_*`
  v1 API consumers call.
- **Compile setup:** `PTHREADPOOL_USE_PTHREADS=1` is forced on MinGW
  (upstream defaults to C11 `<threads.h>` on `_WIN32`, but MSYS2 clang64
  only reliably provides `<pthread.h>` via winpthreads); `Threads::Threads`
  is linked PUBLIC; C11 atomics come from `<stdatomic.h>` (clang needs no
  flag — the upstream `/experimental:c11atomics` is MSVC-only and the shim
  keeps it behind a compiler-ID genex). The vendored `pthreads.c` is
  already patched for MinGW's `alloca` macro.
- **fxdiv link:** `portable-api.c`, `pthreads.c`, `fastpath.c` and
  `legacy-api.c` all `#include <fxdiv.h>` unconditionally, so the shim
  `PUBLIC`-links `unofficial::fxdiv::fxdiv` (found via the same shim chain).
- **cpuinfo:** `<cpuinfo.h>` is only needed when
  `PTHREADPOOL_USE_CPUINFO=1`; `threadpool-common.h` defaults it to 0 on
  every platform, so cpuinfo is not required anywhere.
- **Consumers:** `XER_AudioDSP` (`src/audio/dsp_algorithms`),
  `XER_AudioCore`, `Editor`, and `XER_ContentAI`
  (`src/content/ai/ml/utils/threadpool.cc` → `pthreadpool_create`/
  `pthreadpool_parallelize_*`). Verified: `lib/libxer_pthreadpool.a` builds
  and exports the full v1+v2 API.

### fxdiv — header-only, `unofficial::fxdiv::fxdiv` (INTERFACE)

- **Entry point:** `find_package(unofficial-fxdiv CONFIG REQUIRED)` in the
  root `CMakeLists.txt` → shim
  `cmake/deps/configs/unofficial-fxdiv/unofficial-fxdivConfig.cmake`.
- **Shim provides:** an INTERFACE IMPORTED target with include dir
  `third_party/fxdiv/include` and `FXDIV_USE_INLINE_ASSEMBLY=0`
  (mirrors the vcpkg port — portable C path, no inline asm).
- **Layout:** single self-contained header `include/fxdiv.h` (no
  `fxdiv_impl.h` in this revision). Consumers `#include <fxdiv.h>`.
- **Consumers:** `XER_AudioDSP`, `XER_AudioCore`, `Editor`, and
  transitively every `unofficial::pthreadpool` consumer.

### fp16 — header-only, `unofficial::fp16::fp16` (INTERFACE)

- **Entry point:** `find_package(unofficial-fp16 CONFIG QUIET)` in
  `src/content/ai/CMakeLists.txt` → shim
  `cmake/deps/configs/unofficial-fp16/unofficial-fp16Config.cmake`.
- **Shim provides:** an INTERFACE IMPORTED target with include dir
  `third_party/fp16/include`, linked PUBLIC into `XER_ContentAI`
  (defines `XER_AI_HAS_FP16=1`). Consumers also self-gate with
  `__has_include(<fp16.h>)`.
- **psimd:** the vcpkg port links `unofficial::psimd::psimd` as fp16's
  optional SIMD backend. psimd is unpackaged on MSYS2 and not vendored —
  verified unnecessary: `fp16.h` only pulls `fp16/psimd.h` when `PSIMD_H`
  is already defined, and nothing defines it, so the scalar paths are used.
- **Layout:** umbrella `include/fp16.h` → `include/fp16/fp16.h` +
  `include/fp16/bitcasts.h`; `include/fp16/psimd.h` ships but is inert.
- **Consumers:** `src/content/ai` — `ml/utils/ml_type_converter.cc`,
  `reader/onnx_stack/onnx_tensor_converter.cc`,
  `reader/transformation/trans_weight_unpack.cc`,
  `models_builder/weight_management/weight_quantize.cc`
  (`fp16_ieee_from_fp32_value` / `fp16_ieee_to_fp32_value`). All compile
  cleanly under clang64.

### dlfcn-win32 — header global; compiled lib on demand

- **Why vendored:** MSYS2 clang64 ships **no** `<dlfcn.h>` and no `libdl`
  (`mingw-w64` provides only `libdloadhelper`, a different API). A handful
  of sources include `<dlfcn.h>` unconditionally.
- **Header side (active):** `cmake/deps/system_deps.cmake` (~line 191)
  adds `third_party/dlfcn-win32/src` via `include_directories()` on
  `WIN32`, so every target resolves `<dlfcn.h>` — mirroring how the header
  is globally visible under the vcpkg toolchain. WIN32-gated so the native
  POSIX `dlfcn.h` is never shadowed on Linux/macOS.
- **Compiled side (on demand):** `find_package(dlfcn-win32)` → shim
  `cmake/deps/configs/dlfcn-win32/dlfcn-win32Config.cmake` builds static
  `xer_dlfcn_win32` (alias `dlfcn-win32::dl`, matching the vcpkg port).
  Verified: `dlfcn.c` compiles cleanly under clang64 and exports
  `dlopen`/`dlsym`/`dlclose`/`dlerror`/`dladdr`.
- **Callers today:** all `dl*` call sites are inside `#else`
  (non-`_WIN32`) branches that use `LoadLibrary`/`GetProcAddress` on
  Windows — except two unguarded consumers:
  `src/native/plugin/pxer/pxer_host.cc` (`XER_Native`) and
  `src/content/device/cast/aura/aura_lc3_decoder.cc`
  (`XER_ContentDevice`). Both compile fine (the global include dir covers
  the header), but their objects carry undefined `dlopen`/`dlsym`/
  `dlclose`/`dlerror` references. No executable links `XER_Native` or
  `XER_ContentDevice` today, so nothing pulls those objects into a link —
  identical exposure to the vcpkg presets, where nothing links `dl` either.
  When a binary eventually links those libraries it must also add
  `dlfcn-win32::dl` (which triggers the vendored compile) — the shim
  exists so that `target_link_libraries(... dlfcn-win32::dl)` works
  unchanged. `src/crash/crash_stack_trace.cc` is a third unguarded
  consumer but is V8-gated (`ENABLE_V8=OFF`).

## License notes

- **dlfcn-win32** is **MIT** (`COPYING`), not LGPL-2.1 as an earlier audit
  stated — verified against upstream `COPYING` at v1.4.2 and the vcpkg
  port metadata.
- **pthreadpool** is BSD-2-Clause ("BSD-style license" in `LICENSE`);
  **fxdiv** and **fp16** are MIT.
