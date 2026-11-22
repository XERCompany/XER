# src/-API reuse audit — tools/ vs src/

Subagent audit (4085d515), verified against source 2026-11-22.
Question: where does `tools/` reimplement what `src/` already has?

## Key link facts (2026-11-22)

- `XER_VideoEditor_Core` links `XER_Video` → `XER_BridgeSkia`
  (`tools/video_editor/CMakeLists.txt:38-52`, `src/video/CMakeLists.txt:77-83`)
  → **compositor.cc can call every Skia-bridge function with zero build
  changes**; it already probes it at `compositor.cc:873`.
- `XER_Image` (decoders/`ImageLoader`) is NOT linked — that's the only
  reason OIIO delay-load exists (`tools/video_editor/io/oiio_image.*`).
- `XER_AudioCore` is already linked into the editor core.

## Ranked swaps

| # | Duplicate | src/ API | Effort | Value |
|---|---|---|---|---|
| 1 | `compositor.cc` ~600 lines scalar pixel loops: `ApplyBlendModePixels` (7 modes, `:2132-2175`), `AlphaOverFrame`, `CrossfadePixel`, `ResampleBilinearBGRA`, `ApplyPosterizePixels`, `ApplyColorBalancePixels`, `ApplyChromaKeyPixels` | `Bridge::Skia::BlendModeApply/BlendSrcOver` (29 modes), `AdjustPosterize/ColorBalance`, `MaskFromColorRange`+`ApplyMask`, `ImageScale`/`video::utils::ResizeFrameWithSkia` | Low–Med | **highest** — 7→29 blend modes + SIMD paths |
| 2 | `io/oiio_image.*` still-image path (~115 lines delay-load CMake + call sites) | `XER_Image::ImageLoader` magic-detect over `src/image/formats/` (exr/tiff/psd/raw/heif/webp/gif/svg) | Med | drops heavyweight 3p dep; **gap: DPX + Radiance HDR** — keep narrow OIIO fallback or write 2 small decoders |
| 3 | 3× PCM format-conversion copies (`ai/audio_pcm.cc`, `audio_mixer.cc`, `media_catalog.cc`) + linear `ResampleLinear` | `AudioCore::AudioBitDepthConverter`, `AudioSampleRateConverter` (windowed-sinc) | Low–Med | dedup + sinc-quality upsample |
| 4 | `plugins/xer_fx_*` ~13 kernel files (blur/color/style/distort/audio) | Skia bridge filter suite + `src/audio/effects_rack` | Med–High | needs plugin link strategy first (below) |
| 5 | `playback/wasapi_sink.cc` — self-confessed copy of `src/content/video/video_audio_player.cc` WASAPI plumbing | extract shared sink into src/ | Med | one impl to fix bugs in |
| 6 | Trivials: `HexToBgra` (`lab/color_pick.h`) → `ParseColorHex`; `media_prep`/`thumb_cache` raw pools → `IOThreadPool` | `skia_engine_bridge.h:38`, `src/async_io/io_thread_pool.h` | Trivial | opportunistic |
| — | `lab/object_ops` inpaint + `mask_refine` | `src/image/tools/retouch_tool.h` | — | **promote-not-replace**: lab's exemplar/PatchMatch + float-mask ops are better than RetouchTool diffusion — move lab ops down to `src/image/tools/` |

## src/ extensions that unlock the swaps

- `video::VideoEffects` (`src/video/video_effects.h`): add `ChromaKey`,
  `ColorBalance`, `Posterize` one-line wrappers → deletes compositor
  inline fallbacks.
- `video::MixSystem` (`src/video/mix_system.h`): add frame-level
  `BlendFrames(dst, src, blend_mode_name)` → retires
  `ApplyBlendModePixels` + editor `BlendMode` enum seam.
- Skia bridge: add `ImageCompositeWarp(dst, src, Matrix3x3, opacity)`
  → replaces `WarpClipFrame` inner loop (`compositor.cc:2247-2273`).
- `ImageLoader`: needs DPX + Radiance .hdr decoders to fully retire
  OIIO; verify `image_saver.h` PNG/TIFF/EXR encode coverage first.
- Plugin ABI (`sdk/xer_plugin.h`) is CRT-only; options: (a) link
  `XER_BridgeSkia` static into each plugin, (b) host-service export in
  ABI, (c) implement `builtin:` effects host-side and retire DLL
  kernels to third-party-only.

## Keep-as-is (no src/ equivalent)

transition pixels wipe/slide/dip, `WarpClipFrame`, fractional
rect/ellipse mask+feather, sphere/ribbed-glass/bokeh kernels,
audio_sync correlation, lut_manager, ocio_transform, scopes_widget,
bitmap_font, timecode, timeline model/history.

## Unverified (spot-check before acting)

- `image_saver.h` encode coverage vs `Io::WriteImage`.
- `XER_BridgeSkia` export into plugin MODULE DLL on this toolchain.
- `audio_mixer.cc` downmix table vs src/audio channel matrices.
