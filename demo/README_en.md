# Demo: VST3 + CEF (`XER_VST3_CEF.exe`)

[English](README_en.md) | [Русский](README_ru.md)

Desktop demo that embeds a native VST3 plugin editor and a CEF (Chromium
Embedded Framework) browser view inside the XER UI framework, with audio
file loading, WASAPI output, and a VST3 processing chain.

## Features

- **VST3 view** — plugin path selection, native editor HWND embedding,
  per-plugin right-click bypass
- **CEF view** — embedded Chromium browser with navigation controls
- **Audio chain** — audio file loading → `AudioEngine` → VST3 stages →
  WASAPI output; plugins are activated (`setActive`) and processed in
  stereo
- **Plugin scanning** — via `EngineScanVST3.exe`: one `--probe` child
  process per `.vst3` module, `std::async` bounded by CPU cores

## Files

| File | Purpose |
|---|---|
| `vst3_cef.h/.cc` | Demo window: VST3 host view + CEF browser view |
| `xbrowser.cc` | CEF browser integration |
| `CMakeLists.txt` | `XER_VST3_CEF` target |

## Notes

- Some VST3 plugins are interactive generators (e.g. capture/riser
  types) and intentionally output silence without their own workflow —
  use the right-click bypass to hear dry audio.
- `.vst3` bundles are resolved through `Contents\x86_64-win\*.vst3`;
  `Contents\Resources\moduleinfo.json` metadata selects audio classes.
