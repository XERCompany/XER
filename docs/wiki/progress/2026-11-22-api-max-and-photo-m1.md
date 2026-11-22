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

## Third batch — workspace UI + AI consumers + cleanup (2026-11-22, `SMOKE RESULT OK` 54 checks)

- **Photo M3 workspace UI** (`a896d49c`): "Photo" workspace via
  title-bar pill / View menu (`photo_workspace.{h,cc}` ~2000 lines):
  canvas viewport (checkerboard, zoom-at-cursor, pan, brush ring),
  left-strip tools B/E/F/G/H (brush w/ size-hardness-opacity, eraser,
  fill, gradient-drag), layers panel (tree, eye, opacity, blend,
  add/group/dup/delete/up/down/merge/flatten), undo Ctrl+Z/Y,
  New/Open `.xerphoto`+common formats/Save/Export PNG via Windows
  pickers, responsive ≥1100×700. New `RegisterCheck` hook so exe-side
  UI can register smoke checks; `photo_workspace` check verifies
  headless widget + scripted stroke + pixel-exact undo + xerphoto
  round-trip.
- **AI consumers** (`c6781d49`): shared `Onnx::Session` extracted to
  `src/content/ai/onnx_session/` (ORT preferred → cv::dnn fallback,
  lazy per-model cache; find_object.cc shrank 1240→465). Object Lab:
  "No BG" (BiRefNet → RGBA cutout via AsyncOp), "Smart" select
  (EfficientSAM — up to 6 click points on monitor, best-of-3 by IoU
  → stored AI mask preferred by Cut out/Remove/To layer). Whisper
  verified already wired (media-bin ctx "Transcribe → Markers",
  `bin/models/ggml-base.en.bin`). Smoke `ai_lab_ops` runs real models
  on synthetic images.
- **Cleanup** (`c98114e2`): `libopencv_calib` "broken dll" was a
  probe-list splicing bug (`;`-list → only first entry survived) —
  fixed in CMakeLists, package was never stale. `audio_mixer.cc` +
  `media_catalog.cc` PCM copies → `AudioBitDepthConverter`
  (downmix/LFE matrix kept — editor-specific, `channel_mixer.cc`
  lacks the rules). `ENABLE_CUDNN` now actually gates async_io
  surface (and was outright broken before — missing find_package).
  `module_registry.h` TODO seam left.

## Fourth batch — timeline redesign + render cache + extensions (2026-11-22, `SMOKE RESULT OK` 54)

- **Timeline/panel redesign** (`4c0cd898`): CapCut-style rounded
  clip cards (accent header strip + per-type colors via
  `ClipTypeColor`/`VeTheme::ClipBaseColor`), transition junction
  overlay (hatched band + edge bars + duration chip), icon-pack
  glyphs in track headers/toolbar, right-dock tabs → `Style::Pill`
  with full-name tooltips (`Tabs` gained per-tab tooltip API in
  YA.UICore). `ui_screenshot_diff` unaffected (scopes compositor
  only). Deferred: draggable transition handles, rounded blits.
- **Effect-frame cache** (`a4e55700`): `effect_frame_cache.{h,cc}`
  — Olive-pattern LRU (512MB, `XER_VE_FX_CACHE_MB`) keyed
  `{clip_id, source_time_us, state_hash}` with full-key equality +
  honest-serve store gate; injected into PreviewEngine + Monitor
  (export/Lab bypass); per-session memo → paused repaints ~free;
  thread_local scratch in VideoEffects → zero steady-state allocs.
  `builtin:` chains cached; `script:`/`ocio:`/`plugin:`-DLL excluded.
  Next: AutoCacher pre-render ±playhead for playback hits.
- **Six extensions** (`a0ece86e`): beatcutter, captionist,
  silencecutter, autoduck, scenecutter, audiomix — pure JS .xexrj
  under `assets/extensions_src/`; required new v3 services
  (`timeline.set_clip`, `timeline.set_keyframes`, `ai.status/last/
  cancel`, `ai.detect_{silences,scenes,beats}`, `ai.loudness`,
  `ai.transcribe`, `ai.find_objects`) + CEF bridge `editor.ve_call`
  for `xer-ext://` pages; new `ai/silence_detect` + `ai/scene_detect`
  helpers; all documented in `ve.core.h`, advertised via
  `VeServiceNames()`, smoke-asserted.
- **Feature research** (`c96ee5e0`): `research/2026-11-22-modern-nle-
  features.md` — top-10 ranked; captions/text-edit/silence/ducking/
  reframe all hang off already-vendored whisper+ONNX.

## Fifth batch — text-editing + AutoCacher + polish (2026-11-22, `SMOKE RESULT OK` 56)

- **Text-based editing** (`bb580c95`): "Trn" right-dock tab —
  `transcript_edit.{h,cc}` (word splitting/proportional timing,
  source→timeline mapping via in_point/rate, path+size FNV-1a cache
  with JSON mirror under user-data/transcripts) +
  `ui/transcript_panel.{h,cc}` (word flow, playhead highlight,
  click→seek, drag-select → Ripple delete/+Marker/Captions action
  bar). Context-menu "Edit by Transcript" on clips + media bin.
  Whisper spans only (word times distributed); no diarization.
- **AutoCacher** (`5384fcee`): `playback/auto_cacher.{h,cc}` —
  below-normal-priority thread + PRIVATE second Compositor (the only
  safe seam) sharing the engine's EffectFrameCache. Key enabler:
  engine render time now snaps to project frame boundaries
  (`floor(t/frame_dt)`) — µs-keyed cache + wall-clock t could never
  collide otherwise. Window [playhead−0.5s,+3s], forward-first
  budget min(360, ½cap), settle 400ms, cooperative cancel.
  `XER_VE_AUTOCACHE=0` toggle. Smoke: playback hits 1→4.
- **Transition drag handles** (`78e73283`): edge grab zones on the
  junction overlay, centered-resize preview, commit via
  `TimelineHistory::SetTransitionDuration` (MergeWith coalescing);
  clamp = 2·min(from,to) + media handles via get_media_bounds.
- **AI mask overlay** (`78e73283`): `MonitorWidget::SetMaskOverlay`
  — 40% accent tint + boundary polyline in letterboxed frame rect;
  `Lab::MaskToPolygon` (crack-following trace + Douglas–Peucker)
  feeds edge + lasso reuse. Dims-mismatch masks skipped.
- Owner committed batches 3+4+5 himself (874feaa, a9d68b1, 2b77d00
  + later); stale stash@{0} on dev-1.0.0-1 flagged.

## Sixth batch — M4 + smart reframe + voice isolate + proxies (2026-11-22, `SMOKE RESULT OK` 59)

- **Photo M4** (`4cd1b107`): `.xerphoto` → v2 (mask+adjustment fields,
  v1 read-back compat). New `photo/adjust_ops` (15 ops: levels/curves/
  HSL/vibrance/selective_color/...), `selection` (rect/ellipse/lasso/
  poly/magic-wand + invert/feather, clips paint), `mask_ops` (A8
  TiledDevice masks per layer, attach/detach undoable), `retouch`
  (Clone/Heal/Spot/Inpaint via RetouchTool). 14 workspace tools with
  shortcuts; MergeDown bakes adjustments. `photo_m4` smoke pixel-exact.
  Deferred: refine-edge dialog UI, F16 raster.
- **Smart reframe** (`c4059896`): `ai/smart_reframe` — ScanClipSubject
  (RF-DETR, person-pref, scene boundaries) → ClipReframe samples on
  Clip (.xerproj-serialized) → ReframeWindowAt (aspect-locked clamp +
  causal EMA + velocity cap + scene snap). Applied geometrically at
  warp in compositor; `xer.fx.smart_reframe` builtin (inert ABI,
  params target_ratio/smoothing). Ctx-menu + inspector + effects
  catalog. Reframe samples in ClipStateHash → cache invalidates.
- **Voice isolation** (`325edb7b`): GTCRN ONNX (0.5MB, MIT,
  sherpa-onnx contract) in `voice_isolate.{h,cc}` — 16kHz STFT/iSTFT
  streaming, own radix-2 FFT, Onnx::Session lazy cache. Effect
  `xer.af.voice_isolate` (amount) via media-bin bounce →
  `.xer.af.voice_isolate.wav` (non-destructive). NOT in realtime mixer
  (10-15ms/frame CPU). Smoke: noise −40dB, harmonics preserved.
- **Proxy workflow** (`04071364`): `proxy_manager.{h,cc}` — worker
  transcode (x264 MP4, long-edge cap, atomic temp-commit, dedup,
  cooperative cancel); proxies/ dir + SQL table w/ size+mtime
  fingerprint; ResolveDecodePath → compositor/autocacher/session-hash;
  export_engine has no resolver = original-only (asserted). Settings:
  use_proxies/ingest/max_dim=960/crf=28 (undoable, .xerproj). PX
  badges + adaptive ctx menu in media bin. Deferred: proxy LRU cap,
  generated-source proxies.
- Build friction resolved en route: truncated YA_*.def regen
  (interrupted ninja), stale smoke shells locking DLLs, probe-list
  pseudo-group bug already fixed earlier.

## Decisions resolved 2026-11-22 / still open

- ORT route → **resolved**: vendored official binaries (user choice).
- Remaining open: Photo workspace UI (M3+), masks/adjustment/
  selections (M3/M4), `.xerphoto` open-flow, per-tile xerphoto
  encoding, model consumers for BiRefNet/EfficientSAM (matting /
  smart-select features — models staged, no callers yet),
  audio_mixer.cc/media_catalog.cc PCM copies (wave-1 leftovers),
  dead-path items 6–8 (ENABLE_CUDNN decorative, sd.exe stub,
  module_registry.h absent), stale `libopencv_calib-500.dll`.
