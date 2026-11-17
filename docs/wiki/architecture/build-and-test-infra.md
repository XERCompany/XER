# Build & test infrastructure — verified facts

Verified by explore subagent, 2026-11-15. Repo `D:\Developers\XER3`.

## Presets

- **`default` is THE preset**: Ninja generator, `binaryDir=out/build/default`,
  `BUILD_TESTING=ON`, Release, vcpkg toolchain `C:/vcpkg`,
  triplet `x64-windows-release`, `ENABLE_MEDIA=ON`, `ENABLE_V8=OFF`.
- `debug` = RelWithDebInfo variant. `xbuild`, `v8`, `v8-xer` presets are
  **broken/stale** (cmake/v8.cmake never included; ENABLE_MEDIA/
  XER_BUILD_XBUILD are dead options) — do not model new work on them.
- Configure: `cmake --preset default`; build: `cmake --build --preset
  default` or target-scoped `cmake --build out/build/default --target
  <name>`; test: `ctest --preset default`.
- **MSVC env required**: plain shells lack INCLUDE/LIB for MSVC/SDK —
  `cl.exe` fails with `C1083 'string'/'memory': No such file or
  directory` on standard headers (observed 2026-11-15). Prefix every
  build/test invocation:
  `cmd //c "call \"C:\Program Files\Microsoft Visual Studio\18\
  Community\VC\Auxiliary\Build\vcvars64.bat\" >nul && <cmd>"`
  (Git Bash: `cmd //c`; path has VS **18** Community, MSVC 14.51).
- **Baseline build GREEN as of 2026-11-15**: `cmake --preset default`
  configure (~77s) + full build 194/194 targets + `ctest --preset
  default` 3/3 passed (XER_UI_Tests incl. ffmpeg decode tests, so
  FFmpeg DLLs resolve under ctest from `bin/`).
- Helper created: `out/vcenv.bat <cmd>` wraps any command in the
  vcvars64 env (lives under `out/` = gitignored build dir). Usage:
  `"D:/Developers/XER3/out/vcenv.bat" cmake --build --preset default`.

## Tests

- `tests/CMakeLists.txt` is the only CMake file; `tests/ui/` has none.
- Two idioms: (a) `XER_UI_Tests` harness exe (`tests/ui/test_main.cc` +
  `test_harness.h`, `XER_UI_TEST`/`XER_UI_EXPECT_*` macros, `--filter`);
  (b) standalone smoke exes with plain `main()`+CHECK helpers.
- New tests → **smoke pattern**: `add_executable(XER_<Area>_<Kind>
  tests/<file>.cc)` + `target_link_libraries(... PRIVATE ...)` +
  `cxx_std_23` + `add_test(NAME <exe> COMMAND <exe>)`.
- Naming: `XER_<Area>_<Kind>` PascalCase. CTest runs all via
  `ctest --preset default`.
- Tests synthesize media at runtime in `%TEMP%` — **no binary fixtures
  exist** (pattern: `tests/ui/ffmpeg_video_decode_tests.cc` encodes a
  150-frame mp4 via the bridge; `ffmpeg_avatar_tests.cc` hand-builds a
  PPM). Gate on `XER::Bridge::FFmpeg::IsAvailable()`.
- tests/ root has 6 **orphan .cc files** (no add_executable) — stale,
  ignore.
- Include dirs are NOT global — tests rely on transitive PUBLIC includes
  or add `${CMAKE_CURRENT_SOURCE_DIR}/src` explicitly.

## XER_Video / Skia linkage (critical for first consumer)

- `XER_Video` compiles+archives today (static lib, unconditional
  add_subdirectory) but **nothing links it**.
- `XER::Bridge::Skia::*` calls are **compile-time unguarded**, only
  runtime-gated by `IsAvailable()` — 8 .cc files include the bridge
  header unconditionally. Any exe linking XER_Video without
  `XER_BridgeSkia` gets LNK2019. `XER_Video` PUBLIC-links
  `XER_BridgeSkia` when the target exists → consumers get it
  transitively; on this machine Skia IS installed
  (`C:\vcpkg\installed\x64-windows-release\share\unofficial-skia\`).
- `src/ui/CMakeLists.txt:31` has `find_package(unofficial-skia CONFIG
  REQUIRED)` — **Skia is a hard configure requirement for ALL presets**;
  configure dies there before src/video is even processed.
- `XER_VIDEO_HAS_SKIA` macro defined but **never referenced in sources**.
- Latent bug: `clip_gpu_accel.cc`/`rotating_cube_scene.cc` guard GPU
  code with `ENGINE_HAS_OPENCL/VULKAN/ASSIMP` macros that are **never
  defined** (CMake defines `XER_VIDEO_HAS_*` instead) → those paths are
  silently compiled out. Harmless; don't "fix" unless needed.
- `src/image` has the same unguarded-Skia pattern and IS already linked
  by tools (preview/sd) — precedent that it links fine when Skia
  present.

## FFmpeg wrapper

- Target `XER_FFmpeg` STATIC (`src/wrapper/ffmpeg/CMakeLists.txt:9`),
  unconditional at root:106 — test/tool targets can link it directly.
- Include bridge headers as `"wrapper/ffmpeg/ffmpeg_engine_bridge.h"`.
- **FFmpeg DLLs live in `C:\vcpkg\installed\x64-windows-release\tools\
  ffmpeg\` (avcodec-63.dll etc.), NOT `bin/`** — new exes/tests must
  resolve them at runtime (existing tests have the same dependency;
  verify under ctest).
- On non-Windows the project module `cmake/FindFFMPEG.cmake`
  (pkg-config) is used; bridge code is MSVC-gated regardless.

## JSON

- **`jsoncpp_lib`** — guaranteed: `find_package(jsoncpp CONFIG REQUIRED)`
  root:67, propagates transitively via `XER_Helper` (PUBLIC link).
  Header `<jsoncpp/json/json.h>`; wrapper `src/helper/jsoncpp_helper.h`.
- `nlohmann_json` port installed but only found inside
  `src/content/ai` — for new code prefer `jsoncpp_lib` (zero configure).

## New tool registration

- Root `CMakeLists.txt`, `add_executable` block (pattern: `preview`
  ~428-451, `ServerAudio` ~249-264): sources, `target_include_directories
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src`, `cxx_std_23`,
  `target_link_libraries PRIVATE`, `RUNTIME_OUTPUT_DIRECTORY
  "${CMAKE_BINARY_DIR}/bin"`. Unconditional → builds in every preset.
