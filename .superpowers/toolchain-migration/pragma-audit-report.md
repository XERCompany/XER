# Pragma/MSVC-ism audit for mingw (agent dcf308d9, 2026-11-15)

Full findings in conversation; key results:

## WILL-BREAK-LINK (FIXED by parent, commit pending)
- dwmapi, gdiplus → XER_UI_Core (src/ui/CMakeLists.txt WIN32 block)
- winmm, iphlpapi → XER_ContentPlatform (platform/CMakeLists.txt:54)
- bthprops, setupapi, wlanapi, iphlpapi, ole32 → XER_ContentDevice
  (device/CMakeLists.txt:50)

## WILL-BREAK-COMPILE (covered by vendor agent 49b89dfc)
- pxer_host.cc (XER_Native, unguarded dlfcn.h+ffi.h)
- aura_lc3_decoder.cc (XER_ContentDevice, unguarded dlfcn.h)

## Key discovery
CMake Windows-Clang.cmake auto-links kernel32 user32 gdi32 winspool
shell32 ole32 oleaut32 uuid comdlg32 advapi32 on EVERY exe/shared —
VERIFY at first configure via CMAKE_CXX_STANDARD_LIBRARIES/link.txt.
If confirmed, shell32(preview)/ole32/advapi32 needs are covered.

## SAFE (verified)
All _s CRT (_controlfp_s, _dupenv_s, sscanf_s, strncat_s, fopen_s —
mingw-w64 secapi provides), __cpuid/intrin.h, __uuidof/IID_PPV_ARGS
(-fms-extensions default), #pragma warning, typeid/dynamic_cast
(all static-lib intra-binary), __declspec(dllexport), xer_sscanf
macros, errno_t, _aligned_malloc, SIMD headers.

## Notes
- src/crash + src/javascript not built (V8 gate); src/CMakeLists.txt +
  src/content/CMakeLists.txt are ORPHANED (no add_subdirectory).
- XER_Video_Smoke gated if(MSVC) → skipped under mingw;
  XER_VideoEditor_Tests builds → pulls WASAPI (ole32 transitive via
  XER_AsyncIO PUBLIC — fragile, optional explicit add).
- Do NOT remove #pragma comment(lib) lines — still needed for
  vcpkg/MSVC presets until removal phase.
- -march=native under clang-mingw: verify at spike.
