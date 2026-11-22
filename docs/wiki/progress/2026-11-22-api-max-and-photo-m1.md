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

## Second batch — user-approved waves (2026-11-22, all landed, `SMOKE RESULT OK` 51 checks)

- **ORT fixed**: onnxruntime **1.32.0** vendored at
  `third_party/onnxruntime/win-x64/` (GitHub blocked → NuGet
  byte-equivalent); `xer_stage_vendored_ort()` in `cmake/YaDll.cmake`
  re-stages on rebuild. Probe `unloadable`→`available`;
  `ai_find_object` runs real inference on backend `onnxruntime`.
  Known residual: `libopencv_calib-500.dll` also stale (non-fatal);
  smoke banner shows 1.29.0 (configure-time metadata, cosmetic).
- **Photo M2** (`6cd3db65`): group isolation composite (Krita
  semantics), `locked` attr + enforcement, `layer_ops.{h,cc}`
  (MoveLayer/MergeDown/FlattenImage/DuplicateLayer — COW O(1) dup),
  TransformLayer via `ImageCompositeWarp`, `.xerphoto` format
  (XERPHOTO magic + JSON tree + zlib via CompressionCodec),
  `photo_m2` smoke. Deferred: workspace UI, masks, adjustment layers,
  selections, F16.
- **OIIO→ImageLoader + WASAPI** (`c3e486f8`): `io/oiio_image.*` →
  `io/still_image.*`; ImageLoader owns all its formats (incl. EXR
  Reinhard tonemap); OIIO narrowed to `dpx/hdr/pfm/ora/r3d/raw` +
  TIFF/EXR encode (ImageSaver = PNG/JPEG/raw only). Shared
  `src/content/video/wasapi_render_client.{h,cc}` now backs both
  `video_audio_player` and `wasapi_sink`.
- **AI models** (`5e8d7f9c`): `rfdetr_nano.onnx` (Apache-2.0,
  replaces AGPL YOLOv8n), `birefnet_lite.onnx` (MIT),
  `efficientsam_ti.onnx` (Apache-2.0) → `assets/models/`;
  `ggml-base.en.bin` whisper → `bin/models/`. `find_object.cc` gained
  **DecodeDetr** (multi-output DETR contract: pred_boxes+logits,
  sigmoid argmax, normalized-coord autodetect), `OutputCount`,
  `ProbeOnnxInputSquare`; `ai_tags` model-picker scores detr names
  ahead of generic .onnx (prevents BiRefNet being picked as
  detector); `rfdetr_nano.names` COCO-id labels.
- **Plugins→builtin** (`9e7a1cd2`): all **48 effects** migrated to
  `builtin_fx.{h,cc}` (~1290 lines); ids/params unchanged so
  `plugin:<id>` in old .xerproj resolves. ~22 routed to Skia bridge /
  effects_rack, rest ported verbatim (no equivalent). `plugin:` loader
  intact for third-party; 31 xer_* sources deleted; `sdk/` +
  `plugins/common/` kept. Smoke `builtin_fx` covers all 48.

## Decisions resolved 2026-11-22 / still open

- ORT route → **resolved**: vendored official binaries (user choice).
- Remaining open: Photo workspace UI (M3+), masks/adjustment/
  selections (M3/M4), `.xerphoto` open-flow, per-tile xerphoto
  encoding, model consumers for BiRefNet/EfficientSAM (matting /
  smart-select features — models staged, no callers yet),
  audio_mixer.cc/media_catalog.cc PCM copies (wave-1 leftovers),
  dead-path items 6–8 (ENABLE_CUDNN decorative, sd.exe stub,
  module_registry.h absent), stale `libopencv_calib-500.dll`.
