# Static linking state (MSYS2/clang64 preset)

What was linked statically, what is delay-loaded, what stays
dynamic — and why. All claims as of 2026-11-20, verified in the
working tree on top of `65cd81b`.

## Static (MSYS2 `.a` archives)

- `libarchive` consumers (`ArchiveManager`, `HubCivitAIBrowser`):
  static + transitives declared in
  `src/content/archive/CMakeLists.txt` — `liblz4.a`, `libb2.a`,
  `libzstd.a`, `xmllite` (Windows system import lib).
- `libmp3lame.a` + `libmpg123.a` + `shlwapi` (audio).
- V8 → `libv8_monolith.a` via the shim
  `cmake/deps/configs/V8/V8Config.cmake` — no `libv8*.dll` imports.
- FFmpeg-related and codec archives: static wherever configured.
- Result: `video_editor.exe`, `XER_VST3_CEF.exe`, `sd.exe`,
  `preview.exe`, `ArchiveManager`, `EngineScanVST3` etc. import
  only `YA.*` + Win32 (+ exceptions below).

## Delay-loaded (no static archive exists)

Helper: `xer_collect_delayload_dlls` / `xer_delayload` in
`cmake/YaDll.cmake`. Resolves versioned names via `stem-*.dll` /
`stem_*.dll` globs in `<libdir>/../bin`; works on INTERFACE targets
via `INTERFACE` link options.

- OpenCV (`libopencv_*-500.dll`) — delay-loaded into every exe,
  list exported from `src/wrapper/opencv/CMakeLists.txt`.
- OpenColorIO `libOpenColorIO_2_6.dll`, OpenImageIO
  `libOpenImageIO-3.2.dll` / `libOpenImageIO_Util-3.2.dll` —
  delay-loaded in `YA.VideoEditor-Core.dll`.
- ggml/whisper/vulkan/shaderc set — delay-loaded inside
  `YA.MediaDecoderUniversal.dll` (hardcoded name list; will break
  silently on ggml version bumps — known trap).

## Must stay dynamic

- `libskia.dll` — **cannot be delay-loaded**: implib exports data
  symbols (`__declspec(dllimport)` vtables) and GNU delay-load only
  handles functions. libskia + 20 MSYS2 transitives are staged next
  to `YA.UICore` via POST_BUILD (see `src/ui/CMakeLists.txt` —
  list of 21 DLLs).
- CEF (`libcef.dll`, `chrome_elf.dll`, resources/locales) —
  third-party binary runtime, deployed beside CEF apps.

## Delay-load trap (learned 2026-11-20, ocio_transform exit 127)

GNU delay-load is **not recoverable**: when the helper fails to
resolve a missing DLL it hard-terminates the process (exit 127
on the suite runner). An "absent runtime → nullptr" contract can
never fire — the caller must probe `LoadLibraryW` over the resolved
runtime names *before* first use (see `EnsureOcioRuntime()` in
`tools/video_editor/color/ocio_transform.cc`,
`EnsureOiioRuntime()` in `io/oiio_image.cc`; names baked via
`XER_VE_OCIO_RUNTIME_DLLS`/`XER_VE_OIIO_RUNTIME_DLLS` defines).

`xer_stage_runtime_dlls(<out_dir> <toolchain_bin> <seeds>...)` in
`cmake/YaDll.cmake` generalizes the libskia staging: configure-time
recursive walk of PE import+delay-import tables via
`CMAKE_OBJDUMP -p`, copying everything resolvable into the bin dir.
OCIO/OIIO closure = 115 DLLs (~200 MB: openvdb, ffmpeg, dcmtk,
jxl, heif…). Runs once at configure.

## Removed

- `CopyDLL.ps1` deleted; `video_editor_dist` /
  `XER_VIDEO_EDITOR_DEPLOY_DLLS` gone; `Build.ps1` rewritten
  (no `-Dist` / `-NoDeploy` / MSYS2-PATH hack). Verification:
  `XER_UI_Tests` 90/90 and `XER_Media_Smoke` OK without
  clang64/bin on PATH.
