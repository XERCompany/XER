# 2026-11-22 — src/-API maximization + NN cleanup + Photo M1

Three implementation waves landed in the working tree (uncommitted
as of 2026-11-22). `video_editor --smoke` → `SMOKE RESULT OK`,
incl. new checks `blend_modes` and `photo_m1`.

## Wave 1 — compositor/PCM → src/ APIs (agent 8b97c7bd)

- `compositor.cc` scalar pixel loops replaced by:
  `MixSystem::BlendFrames` (new, frame-level named blend) →
  `Skia::BlendModeApply` extended to **all 29 canonical modes**
  (Porter-Duff premul + W3C separable/non-separable) —
  `skia_engine_bridge.cc:571-767`.
- `VideoEffects::ChromaKey/ColorBalance/Posterize` added (thin
  wrappers over bridge); compositor inline kernels deleted.
- `AlphaOver`/`CrossfadePixel` → `BlendSrcOver`; bilinear resample →
  `ResizeFrameWithSkia` (which had a latent bug — used
  `RasterDrawImage` and never resized; now calls `ImageScale`).
- `ImageCompositeWarp(dst, src, Matrix3x3, opacity)` added to bridge
  via `SkCanvas::concat`/`drawImage`.
- `AudioBitDepthConverter` extended (`FLOAT_64`, int↔float routes,
  `ConvertBuffer` byte-level entry); `audio_pcm.cc` now uses it +
  `AudioSampleRateConverter(FAST)` (identical math to old linear;
  sinc = future option).
- **`BlendMode` enum expanded 7→29 in `BlendModeNames()` order; old
  project files map to same ordinals** (`Normal=0..Lighten=6`
  preserved, `Add`→`"plus"` premul). `project_store.cc` + inspector
  list updated.
- Skipped per scope: transitions/WarpClipFrame/masks/glass kernels
  (no src/ equivalent), audio_mixer.cc + media_catalog.cc PCM copies.

## Wave 2 — NN dead-path cleanup (agent b9f6392f)

- predictor.cc cblas gate now on link defines `XER_AI_HAS_OPENBLAS` /
  `XER_AI_HAS_MKL` (MKL uses `<mkl_cblas.h>`) — **MKL confirmed active:
  oneAPI 2026.2 present on the dev machine**.
- cudnn_bridge wildcard → concrete names `cudnn64_9/8/7.dll` etc.
- `XER_AI_HAS_ONNX` split: proto → `XER_AI_HAS_ONNX_PROTO`; runtime
  keeps `XER_AI_HAS_ONNX`. `onnx_graph_parser.cc` now does real
  `onnx::ModelProto` decode when proto linked (was byte-scan stub).
- `.cu` link trap: `XER_AI_HAS_CUDA_KERNELS` only when
  `CMAKE_CUDA_COMPILER_LOADED`.
- YA name typos fixed → `YA.Extensions.dll`,
  `YA.VideoEditor-ToolsDesign.dll`.
- **ORT still broken**: MSYS2 pkg 1.29.0-2 is latest but stale —
  imports absl-2605, toolchain ships 2608. Needs package rebuild or
  vcpkg `onnxruntime` port. cv::dnn fallback unaffected.

## Wave 3 — Photo editor M1 core (agent 572e76ec)

`tools/video_editor/photo/` — UI-free core per
`research/2026-11-21-graphics-editor-stack.md`:

- `tile_store.{h,cc}` — `TiledDevice` 64×64 sparse COW tiles
  (`unordered_map<int64, shared_ptr<Tile>>`), default pixel,
  canonical uniform tile, `TileMemento` inverse-swap undo;
  `image::PixelFormat` tag = F16 promotion seam.
- `photo_history.{h,cc}` — `PhotoCommand/PhotoHistory` mirroring
  `node_graph_history.h` + `PushApplied`/`ExecutePaintOp` stroke path.
- `document.{h,cc}` — Document + `LayerNode` tree (group/raster),
  `Flatten()` via bridge blend+opacity, `OpenPng`/`SavePng` via
  `image::ImageLoader`/`ImageSaver`.
- `brush.{h,cc}` — soft-tip dab engine (hardness/spacing/flow/jitter/
  pressure, deterministic seed) on scratch device, `CompositeStroke`,
  eraser via `dstOut`.
- `XER_Image` now linked into `XER_VideoEditor_Core` (photo needs it —
  also unblocks the OIIO→ImageLoader swap later).
- Smoke `photo_m1`: clone isolation, memento pixel-exact,
  fill→stroke→undo→redo pixel-exact, PNG round-trip — all OK.
- Deferred to M2+: isolated group composite, masks, adjustment layers,
  selections, op_graph, `.xerphoto`, workspace UI.

## Decisions open for the user (2026-11-22)

1. **ORT fix route**: (a) vcpkg `onnxruntime`/`onnxruntime-gpu` on
   vcpkg presets; (b) wait for MSYS2 rebuild; (c) drop ORT and ship
   cv::dnn-only (all chosen models in the model-stack research are
   cv::dnn-compatible).
2. **AI model downloads**: RF-DETR should replace YOLOv8n as the
   FindObject reference (AGPL risk) — needs a model file in
   `assets/models/` + retest of `ScanClipForObjects`.
3. **Plugin kernel ABI strategy** (audit §3): (a) static-link
   XER_BridgeSkia into each `xer_fx_*` plugin, (b) host-services export
   in `xer_plugin.h` ABI, (c) host-side `builtin:` effects and retire
   DLL kernels to third-party-only. Blocks ~13 kernel files' swap.
4. **Photo M2** next? (isolated groups, masks, adjustments,
   selections, `.xerphoto`, Photo workspace UI) vs finishing
   OIIO→ImageLoader + WASAPI unification + plugin swaps first.
