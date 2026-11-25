# MSYS2 exit plan — XERSyncDev-managed toolchain + dependencies

**Date:** 2026-11-25. **Status:** decision made, migration starting.
**Decision:** fully exit MSYS2 on Windows — every dependency is fetched
or built by `xer_sync_dev` (`tools/xer_sync_dev`, manifest
`third_party/3RD.xersyunc`) as static libs + CUDA; no
`C:/tools/msys64`, no pacman, no `shell: msys2 {0}`.

**In-flight evidence:** HEAD commit `4259606` already deleted the old
subtree-vendored dirs (`third_party/{dlfcn-win32,fp16,fxdiv,pthreadpool,
onnxruntime,cef,...}`) — `third_party/` now contains only the manifest +
`VENDORED.md`; `.xersync-state.json` shows only `re2` synced so far.
The `windows-clang-msys2` build is currently **broken until `xer_sync_dev
sync` repopulates the tree** (the shim-configured vendored packages in
§1c are gone).

This doc is the migration context pointer for future agents. Sections:
what MSYS2 provides today → toolchain decision → per-dep route table →
pkg-config replacement → CUDA → code-impact list → phases.

---

## 1. What MSYS2 provides today (consumption inventory)

### 1a. Toolchain components taken from `C:/tools/msys64/clang64/bin`

| Component | Used for | Evidence |
|---|---|---|
| `clang` / `clang++` | C/C++23 compiler (libc++, Itanium ABI, UCRT) | `cmake/platforms/windows-msys2-clang64.cmake:44-45` |
| `ld.lld` / `lld-link` | linker; GNU-style `--delayload`, `--whole-archive`, `-Bstatic` | `cmake/FindFFMPEG.cmake:139-178`, `cmake/YaDll.cmake` |
| `libc++/libc++abi/libunwind/libwinpthread` (.a + .dll) | static-linked into every exe/YA dll via `-Wl,-Bstatic,-lc++,...` | `CMakeLists.txt:241-264` |
| `compiler-rt` builtins | clang runtime | pacman list `ci.yml:115` |
| `libomp` (`llvm-openmp`) | `__kmpc_*` refs inside ffmpeg static closure | `FindFFMPEG.cmake:128-134`, staged `libomp.dll` |
| `llvm-ar`/`llvm-ranlib`/`llvm-nm`/`llvm-objdump`/`llvm-windres` | archive ops, .def generation, PE import walk, .rc compile | `cmake/YaDll.cmake:406` (`CMAKE_OBJDUMP`), `GenYaDef.cmake`, `xer_app_win32` |
| `pkgconf` | every `pkg_check_modules` call (FFMPEG, V8, OCIO, OIIO, librsvg, libuv, mupdf, skia, MKL) | `cmake/FindFFMPEG.cmake:15`, shims |
| `cmake`, `ninja`, `ccache` | build tools; `Build.ps1:63` falls back to clang64/bin | `ci.yml:119-121` |
| LLVM/Clang **dev** package | `llvm/Object/COFF.h` + static `libLLVM*.a` for `xer_pe_deps`; `clang-c/Index.h` + `libclang.dll.a` for `xer_sync_dev` cmake-gen; monolithic `libLLVM-23.dll` for `VE.ExtensionsRuntime` JIT (`xsjo_jit.cc`) | `tools/pe_deps/CMakeLists.txt`, `CMakeLists.txt:987`, `tools/video_editor/CMakeLists.txt:766-790` |
| mingw-w64 CRT + Windows headers/import libs | `windows.h`, `-lkernel32`... the actual OS SDK substitute | implicit — this is what replaces the Windows SDK |

**Key fact:** there is no MSVC, no Windows SDK, no CUDA toolkit on the
dev machine or in CI (verified: `cl.exe`, `vswhere`, `nvcc`,
`C:/Program Files (x86)/Windows Kits` all absent). The mingw-w64
headers+CRT inside clang64 *are* the Windows dev kit today.

### 1b. pacman package list (authoritative dep set)

`ci.yml:110-188` installs ~70 `mingw-w64-clang-x86_64-*` packages;
`InstallTools.ps1` `$script:Msys2Packages` mirrors the same list
(audit-verified per `docs/wiki/research/toolchain-migration-audit.md`).

Toolchain pkgs: clang, lld, libc++, compiler-rt, llvm-openmp, llvm,
pkgconf, cmake, ninja, ccache.

Library pkgs (60): zlib, openssl, icu, ffmpeg, libuv, range-v3, re2,
abseil-cpp, flatbuffers, jsoncpp, fmt, skia, opencv, libraw, fftw, ggml,
sqlite, zstd, brotli, curl, bzip2, fast_float, boost, eigen3, cereal,
mimalloc, folly, pugixml, libarchive, assimp, libmupdf, libffi, shaderc,
spirv-tools, glslang, vulkan-headers, vulkan-loader,
vulkan-memory-allocator, vulkan-utility-libraries, asmjit,
nlohmann-json, rapidjson, tinyxml2, chrono-date, lz4, snappy,
leptonica, libxml2, openblas (disabled at configure:
`CMAKE_DISABLE_FIND_PACKAGE_OpenBLAS=ON`), libavif, tesseract-ocr,
libyuv, lame, onnx, onnxruntime, opencl-icd, opencl-headers, protobuf,
yaml-cpp, onednn, v8, opencolorio, openimageio.

### 1c. find_package → provider map

Root `CMakeLists.txt:213-458` REQUIRED set: ICU(uc,i18n), ZLIB, FFMPEG,
libuv, absl, fmt, jsoncpp, re2, unofficial-sqlite3, zstd,
unofficial-brotli, OpenSSL, CURL, unofficial-pthreadpool,
unofficial-fxdiv, Eigen3, range-v3, mimalloc, ggml, folly, MKL
(`REQUIRED` → `QUIET` under `MINGW`, `:446`), Boost, cereal, FFTW3f.
Everything else (~80 calls) is `QUIET` and feature-gates via
`if(TARGET ...)`. Shim layer: `cmake/deps/configs/*` (30 shims) +
`xer_defer_vcpkg_config.cmake`; pkg-config-based shims: V8, MKL,
OpenColorIO, OpenImageIO, librsvg, libuv, unofficial-skia,
unofficial-libmupdf, libyuv, opencl, FFTW3f, lz4, BZip2, sodium,
range-v3, cereal, re2, RapidJSON, unofficial-{brotli,libffi,shaderc,
sqlite3,fp16,fxdiv,pthreadpool,xnnpack,libmupdf}, CUDNN (env-probe
style), CEF (vendored-tree probe), V8.

### 1d. Runtime DLL staging inventory (real `bin/` snapshot)

`out/build/windows-clang-msys2/bin` holds **283 DLLs**, populated by
`xer_stage_runtime_dlls` seed walks + `xer_pe_deps` POST_BUILD
(`cmake/YaDll.cmake:325-427`) + hand lists (`src/ui/CMakeLists.txt:105-122`
skia 21-DLL set, `CMakeLists.txt:816-828` ggml glob, CEF deploy):

- **YA.\*.dll** (~50) — own modules (`cmake/YaDll.cmake` `xer_ya_module`).
- **MSYS2 clang64 third-party** (~175): FFmpeg set (avcodec-63, avformat-63,
  avutil-61, swresample-7, swscale-10 + codec deps x264/x265/vpx/aom/
  dav1d/rav1e/SvtAv1Enc/kvazaar/xvid/openh264/opus/vorbis/theora/speex/
  mp3lame/mpg123/soxr/gsm/gme/modplug/zvbi/lc3/opencore-amr); glib stack
  (glib-2.0/gio/gmodule/gobject/intl/pcre2/iconv), cairo/pango/harfbuzz/
  fribidi/datrie/thai/pixman/fontconfig/freetype + **librsvg-2**;
  gnutls stack (gnutls/nettle/hogweed/gmp/tasn1/p11-kit/unistring/idn2/
  ssh/srt/rtmp/bluray); image libs (png/jpeg/tiff/webp/jxl/jxl_cms/lcms2/
  lerc/deflate/jbig/openjp2/openjph/gif/raw/heif/uhdr/avif deps blosc);
  OCIO + OIIO + OpenEXR/Imath/Iex/IlmThread + openvdb/dcmtk/ptex;
  opencv-*-500 (7 modules, delay-loaded); ggml-1 + ggml-base + 14
  ggml-cpu-\*/blas/opencl/vulkan/rpc backend DLLs (dlopen'd, invisible to
  objdump — hence glob staging); whisper, shaderc_shared,
  spirv-cross, vulkan-1, dnnl, openblas, onnx, protobuf(-lite),
  utf8_validity, absl-\*-2608 set (~40 dlls — still dynamic despite
  static-retarget because other DLLs import them), re2, fmt, pugixml,
  yaml-cpp, minizip-ng, snappy, lz4, lzma, bz2, z, zstd, brotli\*, xml2,
  ffi, cryptopp, gmp, libva/va_win32, vpl, libc++/libunwind/libwinpthread
  (only because some 3rd-party DLL still imports them), omp.
- **CEF set** (~8): libcef, chrome_elf, libEGL, libGLESv2,
  d3dcompiler_47, dxcompiler, vk_swiftshader — already vendored.
- **Vendored/non-MSYS2** (3): onnxruntime.dll +
  onnxruntime_providers_shared.dll (`third_party/onnxruntime/win-x64`),
  mkl_rt.3.dll (Intel oneAPI), OpenCL.dll (Khronos ICD).

The ~175 MSYS2 DLLs are what migration must eliminate — either by
building the same libs ourselves (preferred: static → they vanish from
`bin/` entirely) or by sourcing foreign binaries (C-API only).

---

## 2. Toolchain decision

Constraint recap: no MSVC, no Windows SDK, no CUDA toolkit — and the
project source + the whole `if(MINGW)` static-runtime/delay-load
machinery assume **clang + libc++ + Itanium ABI + mingw-w64/UC RT**.
Mixing in MSVC-ABI binaries is only safe for C-API DLLs.

| Option | Verdict |
|---|---|
| **(a) VS Build Tools 2022/2026 + clang-cl** | Full ABI pivot to MSVC — every dep must be rebuilt under MSVC ABI (the entire system_deps/shim/YA layer was written for Itanium+`.dll.a`; `static-link-state.md`, `toolchain-migration-audit.md`). winget `Microsoft.VisualStudio.2022.BuildTools` + VCTools workload + Windows SDK is a heavy per-machine install. Keep as the **fallback track** (vcpkg presets still exist) and the only way to ever get nvcc; NOT the migration path. |
| **(b) Official LLVM Windows release (clang-cl)** | Cannot work alone: clang-cl needs MSVC CRT + Windows SDK headers/implibs — the LLVM installer ships neither. Standalone `clang --target=x86_64-pc-windows-msvc` produces unlinked objects with no CRT. Dead end without (a). |
| **(c) llvm-mingw toolchain tarball** | **Recommended.** `github.com/mstorsjo/llvm-mingw` `*-ucrt-x86_64.zip` — self-contained native Windows toolchain: clang, lld, libc++/libc++abi, libunwind, compiler-rt, libomp, winpthreads, mingw-w64 headers+CRT+import libs, llvm-ar/ranlib/nm/objdump/windres. **Identical ABI and driver behavior to MSYS2 clang64** — same Itanium ABI, same libc++, same UCRT, same `.dll.a` convention, same `-Wl,-Bstatic` / `--delayload` lld syntax. Every `if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")` block, the static-runtime flag set, the YA `.def` generation and `xer_retarget_imported_to_static` keep working unchanged. Unzip-and-go — fits `dependencies-opensource` in the manifest exactly like CEF. |
| **(d) Keep a stripped clang64 as compiler-only** | Smallest diff but violates "fully remove" least-painfully-wrong: still leaves a pacman-managed root that drifts out of sync with the vendored lib set. Use only as a P0 transition while llvm-mingw is validated. |

**Recommendation: (c) llvm-mingw**, fetched by `xer_sync_dev` as a
`dependencies-opensource` tarball into e.g. `third_party/toolchain/
llvm-mingw/`, with a new `cmake/platforms/windows-llvm-mingw.cmake`
(same shape as `windows-msys2-clang64.cmake`; `XER_TOOLCHAIN_ROOT`
replaces `XER_MSYS2_ROOT`, `CMAKE_PREFIX_PATH` adds the vendored
deps sysroot instead of clang64).

**llvm-mingw gaps (all solvable, see §3/§7):**
- No `pkgconf` → fetch `pkg-config-lite` win32 binary (native, zero
  deps) via addopensource, or eliminate pkg-config (§4).
- No cmake/ninja/ccache → already choco-installed machine-wide;
  optionally add to manifest for hermetic CI.
- **No LLVM dev package** — llvm-mingw ships `libLLVM-N.dll`/
  `libclang-cpp.dll` as runtime binaries but NOT `llvm/Object/COFF.h`,
  `LLVMConfig.cmake`, static `libLLVM*.a`, or `libclang.dll`.
  Consumers: `xer_pe_deps` (static LLVM object libs),
  `xer_sync_dev` (libclang), `VE.ExtensionsRuntime` JIT (`libLLVM-23`).
  Three routes: (i) `llvm-project` as a manifest git dep — real CMake
  build (`LLVM_ENABLE_PROJECTS=clang`), one ~40 min build producing all
  three; (ii) degrade: `xer_sync_dev` already has
  `XER_SYNC_DEV_HAVE_LIBCLANG=0` textual fallback, `XER_VE_HAS_LLVM=0`
  degrades the JIT; (iii) rewrite `xer_pe_deps`'s COFF import-walk in
  plain C++ (~150 lines — it only reads data dirs 1+13) and drop the
  LLVM dep there entirely. Recommend (iii) for pe_deps + (i) only if
  the JIT/libclang features matter.

---

## 3. Per-dep migration classification

Routes: **M** = already in `3RD.xersyunc` manifest · **B** = upstream
CMake, builds via xer_sync_dev cmake-gen/`add_subdirectory` ·
**P** = prebuilt binary via `dependencies-opensource` · **H** = hard
(autotools/GN/non-CMake — needs w64devkit escape hatch or patch port) ·
**X** = drop/disable candidate.
Effort: T trivial (<½d), S small (½-1d), M medium (1-3d), L large (3-10d), XL (>10d).

### 3.1 Manifest / already vendored (route M)

| Dep | find_package | Notes |
|---|---|---|
| abseil-cpp | absl REQUIRED | in manifest; CMake |
| libxml2 | LibXml2 / YA.LibXml2 | in manifest; CMake |
| ffmpeg | FFMPEG REQUIRED | in manifest but **autotools** — `REQUIRES_CONFIGURE` gap (`xer-sync-dev.md` §"autotools-only"); real route is P (§3.3) or w64devkit (§3.4) |
| zlib | ZLIB REQUIRED | in manifest; synced dirs currently absent (HEAD `4259606`) |
| brotli | unofficial-brotli REQUIRED | manifest; CMake |
| libuv | libuv REQUIRED | manifest; has CMake (shim currently pkg-config based) |
| fp16 / fxdiv | unofficial-\* | manifest; header-only |
| re2 | re2 REQUIRED | manifest; **first synced dep** per `.xersync-state.json` |
| zstd | zstd REQUIRED | manifest; CMake |
| pthreadpool | unofficial-pthreadpool REQUIRED | manifest; source-list build (was vendored+shimmed before) |
| dlfcn-win32 | dlfcn-win32 (header global) | manifest |
| assimp | assimp QUIET | manifest; CMake |
| sdl2 | SDL2 | manifest; CMake |
| CEF | CEF shim | manifest `dependencies-opensource` tarball — template for the whole P route |
| onnxruntime | onnxruntime QUIET | vendored MS binaries work today via `xer_stage_vendored_ort`; keep + generate `.dll.a` implib with `llvm-dlltool` (ships in llvm-mingw) |

### 3.2 CMake-buildable via xer_sync_dev (route B)

| Dep | Effort | Notes |
|---|---|---|
| jsoncpp, fmt, yaml-cpp, pugixml, tinyxml2, flatbuffers, lz4, snappy, bzip2, mimalloc, asmjit, sqlite3, libarchive, libraw, libavif, leptonica, tesseract, libyuv, onnx, protobuf, shaderc(+glslang,SPIRV-Tools), glslang, SPIRV-Tools, Vulkan{Headers,Loader,VMA,UtilityLibraries}, opencl-headers+ICD, fftw3, xnnpack, oneDNN, DNNL | T–M each | all upstream CMake; protobuf/oneDNN/tesseract are the slow builds |
| eigen3, cereal, range-v3, fast_float, rapidjson, nlohmann_json, chrono-date, Boost headers | T | header-only — manifest git dep + INTERFACE patch; `Boost::boost` needs headers only (no compiled boost libs — verify boost context isn't pulled by folly, see X row) |
| curl | M | CMake; choose schannel or vendored TLS backend — picking **schannel deletes the gnutls-stack runtime set** (gnutls/nettle/hogweed/gmp/tasn1/p11-kit/idn2/unistring/ssh2) |
| OpenSSL | M | upstream needs perl+make → prefer **LibreSSL** (CMake, API-compatible, targets `OpenSSL::SSL/Crypto` need alias) or BoringSSL; else P |
| ICU (uc+i18n) | L–XL | autotools-only; consumers ~7 files (`helper/string_utils.cc`, `javascript/common/text_normalizer.cc`, `xer/xer_string.cc`, `node_graph_ext.cc`, `content/ai/.../ai_runtime_features.cc`, `helper/buffer.cc`, `xer/xer_script.cc`). Options: w64devkit build; unofficial icu-cmake port; or shrink usage to a vendored subset. **Hardest "small" dep.** |
| OpenCV 5 | L | real CMake build (~30-45 min); delay-load seam already absorbs the DLL set; official releases are MSVC-only → MUST source-build (C++ API under libc++) |
| ggml, whisper.cpp | M | upstream CMake; backend DLL model (ggml-cpu-*, vulkan, opencl) reproducible; CUDA backend needs nvcc → stays off (driver-probed paths unaffected) |
| OpenColorIO | L | CMake; needs Imath/OpenEXR/yaml-cpp/pystring/minizip-ng first |
| OpenImageIO | L–XL | CMake; **build with minimal formats** (png/jpg/tiff/exr+ffmpeg) to avoid re-creating the openvdb/dcmtk/jxl/heif 115-DLL closure |
| OpenEXR/Imath (for OCIO/OIIO) | S | upstream CMake |
| LLVM/Clang dev (pe_deps, libclang, VE JIT) | L | llvm-project manifest dep; or pe_deps rewrite + feature-gate the rest (§2) |
| libffi | M | autotools upstream; vcpkg carries a CMake overlay to lift; **or replace** `pxer_host.cc` ffi usage (~40 call sites, ffi_prep_cif only) with dyncall (CMake, tiny) |
| libmupdf | M | REQUIRED by `src/content/pdf`; no upstream CMake — vcpkg port has a CMake overlay to lift |
| lame, mpg123, soxr, fftw, libexpat | S | small C trees — cmake-gen source-list patches |

### 3.3 Prebuilt-binary route (route P)

| Dep | Source | Caveat |
|---|---|---|
| FFmpeg | **BtbN/FFmpeg-Builds** `*-win64-gpl-shared` (or `-lgpl`) — mingw-GCC-built `.dll.a` implibs + matching runtime DLLs; gyan.dev builds are MSVC (C API links OK via `.lib` implibs — lld accepts them — but prefer BtbN) | C API → ABI-safe; static-closure parity is the open question (§4). Choosing a build **without librsvg** deletes the entire glib/cairo/pango subtree from the runtime set |
| MKL | Intel oneAPI installer (`winget Intel.oneMKL`) or `mkl-devel` pip wheel (extractable) | `mkl_rt.3.dll` staged today; C ABI, delay-load machinery already handles it (`MKLConfig.cmake`) |
| CUDA cudart/cuDNN | NVIDIA redist zips `developer.download.nvidia.com/compute/cuda/redist` (cuda_cudart, cudnn components — JSON-manifested, extractable) | headers+libs only, no nvcc needed (§5) |
| LLVM dev libs | only via source build (3.2) or keep textual fallbacks | no trustworthy prebuilt mingw libclang package exists outside MSYS2 |

### 3.4 Hard / decision-needed (route H)

| Dep | Why hard | Path forward |
|---|---|---|
| **skia** | `find_package(unofficial-skia REQUIRED)`; C++ API (must match libc++/Itanium ABI — foreign binaries unusable); upstream GN build needs gn+ninja+depot_tools; MSYS2's own package is a minimal custom build, GL-Ganesh-only (`chromium-render-port-plan.md`); `libskia.dll` can't even be delay-loaded (data symbols, `static-link-state.md`) | **Biggest single risk.** Options: port MSYS2's skia PKGBUILD recipe into an xer_sync_dev build step (gn is fetchable standalone, ninja exists); or FetchContent gn-build skia (known recipe, e.g. `skia-cmake`/`skia-build` projects); accept libskia.dll-shared (stage it) like today. Est. XL |
| V8 | `find_package(V8 QUIET)` — OFF in current preset; MSYS2's `libv8_monolith.a` pkg-config route is what the shim consumed | Options: source build via depot_tools+gn (XL, officially MSVC-only); no credible prebuilt mingw V8; **QuickJS swap** for the JS module layer (T–M, ABI-free); or leave OFF. Defer to its own decision doc |
| folly | `find_package(folly REQUIRED)` but **zero sources include `folly/`** — link-only dep (audit `src/content/CMakeLists.txt:105-113`) | Likely **X**: make QUIET + link `Folly::folly` only `if(TARGET)`; MSYS2's mingw folly is a patched port that would be XL to reproduce |
| librsvg + glib stack | meson-based tree (glib/pango/cairo/harfbuzz); only enters via ffmpeg's `.pc` closure + `librsvg` shim | **Eliminate by ffmpeg build config** — pick a build without rsvg → ~25 DLLs gone |
| gnutls stack | autotools; enters via ffmpeg srt/https + curl | curl→schannel; ffmpeg without gnutls (or accept BtbN's gnutls DLLs as runtime set) |
| ICU | see 3.2 | w64devkit escape hatch is the realistic route |
| autotools generally (ffmpeg-source, openssl, icu, libffi, libxml2-configure, gnutls, lame) | no POSIX shell/make without MSYS2 | **w64devkit** (`github.com/skeeto/w64devkit`) — portable GCC+busybox dev kit, ~100 MB zip, provides sh/make/sed/awk without being MSYS2; C-only autotools deps built by GCC are ABI-compatible inputs to our clang/libc++ links. Keep w64devkit strictly a *build-time* tool fetched by the manifest |

### 3.5 Drop / already-dead (route X)

openblas (preset disables), folly (link-only today), XER_BUILD_LLVM
(dead option), `date`/`pffft`/`tinyexr`/`kineto`/`fbgemm`/`blaze`/`Dawn`
(optional QUIET probes that self-disable), OpenBLAS paths, `psimd`
(inert under fp16).

---

## 4. pkg-config elimination strategy

`FindFFMPEG.cmake` is pkg-config-based and so are ~10 shims
(`pkg_check_modules` in V8/OCIO/OIIO/librsvg/libuv/skia/mupdf/MKL/libyuv).
Under the new layout there is no distro `.pc` directory. Two viable
strategies — **recommend A**:

- **A. Generate `.pc` files in the XERSyncDev install step.** The
  manifest already knows each dep's include/lib layout; emit
  `third_party/sysroot/lib/pkgconfig/*.pc` (one per dep incl. transitive
  `Requires`/`Libs.private` mirroring today's pacman files — lift the
  content verbatim from MSYS2 `.pc` templates) + fetch `pkg-config-lite`
  (single native win32 exe) or build `pkgconf` (small C tree). All
  existing shims + FindFFMPEG keep working **unchanged**; the only edit
  is `PKG_CONFIG_LIBDIR` → the new sysroot in the toolchain file.
- **B. Convert consumers to CMake configs.** Cleaner long-term (drop
  pkg-config entirely): rewrite the 10 shim bodies + `FindFFMPEG` to
  `find_path/find_library` against `XER_DEPS_ROOT`. ~1-2 days of shim
  edits but loses the `--static` closure logic the `.pc` files carry —
  that logic must move somewhere (a generated `xer-deps-closure.cmake`).

**FFmpeg static-closure fidelity is the sub-risk:** today's
`XER_FFMPEG_STATIC` path resolves ~150 `.a` archives + hand-listed
gap-fillers (`FindFFMPEG.cmake:118-137` — brotli/jxl_cms/hwy/tasn1/
omp/dovi/lcms2/...). If the P-route FFmpeg ships shared only, accept the
DLL set (staging machinery already handles it); if it ships `.a`
archives, regenerate the closure list per chosen build's pkgconfig dir.

---

## 5. CUDA path (no MSVC, no nvcc — what still works)

Verified state today: `YA.CUDADecode`/`YA.CUDAEncode` **already build
and ship without any toolkit** — they LoadLibrary-probe `nvcuda.dll`/
`nvcuvid.dll`/`nvEncodeAPI64.dll` at runtime (`src/gpu_video/
CMakeLists.txt:1-8`); RTX-3070 nvenc/cuda-hwaccel verified working
(`progress/2026-11-21-gpu-ai-modules.md`). `XER_BridgeCuda` reports
"CUDA toolkit not found" (`src/wrapper/cuda/CMakeLists.txt:23`) —
that's the *only* thing a toolkit adds: `CUDA::cudart` →
`ENGINE_HAS_CUDA_BRIDGE`, `XER_AI_HAS_CUDA_LIB`.

XERSyncDev layout: fetch NVIDIA **redistributable component zips**
(`developer.download.nvidia.com/compute/cuda/redist` — official
per-component JSON manifests: `cuda_cudart`, `cuda_nvrtc`, plus `cudnn`
redist) into `third_party/cuda/` + `third_party/cudnn/`. Then:

- `find_package(CUDAToolkit)` finds the toolkit dir without nvcc (nvcc
  is only required for `enable_language(CUDA)`); set
  `CUDAToolkit_ROOT=$env:XER_DEPS_ROOT/cuda` in the toolchain file.
- CUDNN shim already probes `CUDNN_ROOT` env (`cmake/deps/configs/
  CUDNN/CUDNNConfig.cmake:26`) — point it at the vendored dir or extend
  its root list.
- `.cu` kernels (`src/content/ai/ml/cuda_ops/*.cu`) stay dead: they
  need `enable_language(CUDA)` → nvcc → **MSVC host compiler**, which
  only arrives with the optional VS Build Tools side-quest. Document
  as accepted limitation; clang `-x cuda` on Windows likewise requires
  MSVC headers for host code.
- cuDNN runtime DLLs land in `bin/` via existing staging paths.

---

## 6. Code-impact inventory (what breaks when MSYS2 paths vanish)

| Site | Breakage | Fix |
|---|---|---|
| `cmake/platforms/windows-msys2-clang64.cmake` (whole file) | `XER_MSYS2_ROOT` probe, clang64 `CMAKE_PREFIX_PATH`, `PKG_CONFIG_LIBDIR` | new `windows-llvm-mingw.cmake`; rename cache var → `XER_TOOLCHAIN_ROOT` / `XER_DEPS_ROOT` |
| `cmake/YaDll.cmake:286-304` | hardcoded `C:/tools/msys64/clang64/lib` fallback in `xer_collect_delayload_dlls` | swap to `${XER_DEPS_ROOT}/lib` |
| `Build.ps1:63` | `$MsysClangBin` tool fallback | point at `third_party/toolchain/llvm-mingw/bin` |
| `CMakeLists.txt:241-264` static-runtime block | `-lc++,-lc++abi,-lunwind,-lwinpthread` | **unchanged** — llvm-mingw ships the same archives |
| `xer_retarget_imported_to_static` + `macro(find_package)` (`CMakeLists.txt:285-427`) | repoints `.dll.a`→`.a` under any MINGW toolchain | unchanged; works against vendored sysroot |
| `FindFFMPEG.cmake` + pkg-config shims | no clang64 `.pc` dir | §4 strategy A or B |
| `tools/pe_deps` | needs LLVM dev headers+static libs | rewrite PE walk plain-C++ or llvm-project dep (§2) |
| `xer_sync_dev` libclang | `XER_SYNC_DEV_HAVE_LIBCLANG` gate already exists | degrade or llvm-project dep |
| `VE.ExtensionsRuntime` JIT | `XER_VE_HAS_LLVM` gate already exists | degrade or llvm-project dep |
| `ci.yml` | setup-msys2 + pacman + `shell: msys2 {0}` everywhere | P4 rewrite: bootstrap xer_sync_dev → `sync` → cmake (pwsh/cmd shells) |
| libc++/MSVC ABI boundary | vendored C++/MSVC binaries can't link into libc++ TUs | C-API binaries only (ORT, CEF wrapper compiled locally — already true; any future prebuilt must be C API or source-built) |
| VST3 SDK | FetchContent+CMake from GitHub (`cmake/vst3sdk.cmake`) | unaffected |
| `#pragma comment(lib)` sites (~14, per audit) | already inert under mingw | unchanged |

---

## 7. Phased plan

**P0 — toolchain bootstrap (days 1-3).** Extend `.xersync` schema with a
`toolchain` dep kind (llvm-mingw `*-ucrt-x86_64.zip` + pkg-config-lite +
optional w64devkit); `xer_sync_dev sync` lays it into
`third_party/toolchain/`; new `windows-llvm-mingw.cmake` + preset
`windows-xersync`; prove compiler-only swap first — point
`CMAKE_PREFIX_PATH` at **both** the llvm-mingw root *and* still-present
clang64 lib dirs so the full lib set resolves while the compiler is
already MSYS2-free. Gate: `video_editor` + tests green.

**P1 — foundation libs (weeks 1-2).** All route-M + trivial route-B
deps (~30): zlib/brotli/zstd/lz4/snappy/jsoncpp/fmt/absl/re2/sqlite3/
libxml2/libuv/yaml-cpp/pugixml/tinyxml2/flatbuffers/eigen/cereal/
range-v3/fast_float/rapidjson/nlohmann/date/mimalloc/asmjit/
pthreadpool/fxdiv/fp16/dlfcn + curl(schannel) + LibreSSL + bzip2 +
libarchive + OpenEXR/Imath + pkg-config-lite + generated `.pc` set.
Emit `third_party/sysroot/{include,lib,lib/pkgconfig,lib/cmake}`.
Deliverable: every dep needed by `xer_sync_dev`, `xer_pe_deps` (post-
rewrite), `ztzmake`, `ArchiveManager` etc. resolves from the sysroot —
the migration tool builds itself MSYS2-free.

**P2 — media libs (weeks 2-5).** FFmpeg via BtbN prebuilt (P) or
w64devkit source build; codec/image set (png/jpeg/tiff/webp/lcms2/
libraw/libavif/openjph as needed by chosen ffmpeg+oiio builds);
freetype/harfbuzz only if ffmpeg build requires; audio codecs
(lame/mpg123/soxr/opus/vorbis — mostly inside the ffmpeg build);
sdl2; assimp; OpenCL ICD+headers; Vulkan headers/loader/VMA/VUL;
glslang/SPIRV-Tools/shaderc; libyuv; leptonica/tesseract; libmupdf
overlay. Gate: media smoke suite green without clang64 on PATH.

**P3 — heavy/hard libs (weeks 4-10, parallelizable).** ggml/whisper
(CMake); protobuf+onnx; OpenCV 5 source build; OCIO then OIIO
(minimal-format build); LLVM dev set or JIT/libclang degradation
decision; **skia gn-build port (critical path, XL)**; ICU (w64devkit);
oneDNN; folly drop-verification; V8 decision (defer/QuickJS/source);
MKL wheel. Gate: `video_editor` feature parity vs. the MSYS2 build —
same smoke checks, same delay-load probing contract.

**P4 — CI/CD + purge (week 10-12).** `ci.yml`: delete setup-msys2,
mirror-reorder, pacman steps; replace with a bootstrap stage (fetch a
pinned `xer_sync_dev.exe` or build it with choco cmake+ninja+llvm-mingw)
then `xer_sync_dev sync` + cmake; all `shell: msys2 {0}` → pwsh.
Repo purge: `XER_MSYS2_ROOT`, `C:/tools/msys64` literals
(`YaDll.cmake:286-304`, `Build.ps1:63`, toolchain file), pacman refs in
`InstallTools.ps1`, doc updates (`static-link-state.md`,
`windows-msys2-gotchas.md` → rename). Optional side-quest: VS Build
Tools install → enables clang-cl preset + nvcc later — orthogonal.

**Keep (zero-change survivors):** YA.* module/`xer_ya_module` system,
static-runtime `-Bstatic` flag set, delay-load machinery +
`xer_pe_deps` approach, shim-config indirection pattern, vendored CEF +
ORT + manifest format, `xer_retarget_imported_to_static`,
`xer_stage_runtime_dlls`.

**Risks (ranked):** 1) **skia** — GN-build port, C++-ABI-locked, no
prebuilt fallback (XL, critical path); 2) FFmpeg static-closure parity
vs. today's ~150-archive `.pc` resolution (mitigate: shared-DLL FFmpeg
acceptable interim — staging already works); 3) ICU autotools build;
4) hidden link-only deps surfacing at link time (folly may be the
first); 5) dep-version drift vs. what MSYS2's patched ports papered
over (e.g. MSYS2's opencv-5 API already diverged from vcpkg-4
expectations — new sources may re-open that class).

## Status log

- **2026-11-26** (as of `f11dee0`): llvm-mingw toolchain **proven standalone**.
  `xer_sync_dev sync --dep llvm-mingw` fetched 191 MB →
  `third_party/llvm-mingw/` (Clang 23.1.6, libc++/compiler-rt/lld,
  windres, gendef, busybox coreutils, python 3.14). Probe
  exe+static+shared+RC links with KERNEL32+UCRT-only imports.
  `cmake/platforms/mingw-llvm-x64.cmake` landed — find-root locked to
  toolchain + repo, MSYS2/vcpkg prefixes unreachable by design. Gaps:
  pkg-config (pkg-config-lite dep), perl/autotools/meson/nasm
  (w64devkit hatch); ninja survives via choco install. Same day:
  manifest expanded to 165 deps (`b7414b2`) covering the full §2
  inventory + FFmpeg codec tree; `ref`/`notes`/`build_mode` schema.
