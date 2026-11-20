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

## Removed

- `CopyDLL.ps1` deleted; `video_editor_dist` /
  `XER_VIDEO_EDITOR_DEPLOY_DLLS` gone; `Build.ps1` rewritten
  (no `-Dist` / `-NoDeploy` / MSYS2-PATH hack). Verification:
  `XER_UI_Tests` 90/90 and `XER_Media_Smoke` OK without
  clang64/bin on PATH.
