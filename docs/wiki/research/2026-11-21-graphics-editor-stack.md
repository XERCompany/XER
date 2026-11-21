# Graphics editor stack — research (Photoshop-class raster editor)

Research for the "Photo" workspace (`suite-architecture.md` Phase 3 —
scope decision recorded in `decisions/video-editor-scope.md`,
"Photoshop = real image-editing inside the suite"). All repo claims
verified in the working tree, 2026-11-21. Subagent: b8930d81.

## 1. What we already have (2026-11-21 inventory)

The suite is closer to an image editor than the phase list suggests:

| Asset | Where | What it gives us |
|---|---|---|
| Skia bridge | `src/wrapper/skia/skia_engine_bridge.h` | Photoshop-level flat API on `std::vector<uint32_t>` RGBA buffers: all 29 SkBlendMode names, raster primitives/paths/clips, PNG/JPEG codec, transforms, full adjust suite (HSL/exposure/levels/curves/color-balance/vibrance/channel-mixer/selective-color/sepia/threshold/posterize/invert/grayscale/color-replace), filter suite (box/gaussian/motion/radial blur, sharpen, emboss, edge, noise, median, pixelate, oil-paint, vignette, chromatic aberration, arbitrary convolve), gradients, flood fill w/ tolerance, drop-shadow/glow, text, ApplyMask/MaskErode/MaskDilate/MaskFromColorRange, histogram/auto-levels/equalize |
| `XER_Image` lib | `src/image/` | `ImageBuffer` (RGBA8 + RGBA32F float plane), `LayersManager` (named stack, opacity/visibility/blend-mode, Flatten via Skia), `RetouchTool` (clone stamp, inpaint, healing), `CropTool`/`TransformTool`, `HistogramAnalyzer`, `FilterChain` (named non-destructive op list), VFX set, decoders incl. flattened PsdDecoder + EXR/TIFF/WebP/RAW/HEIF/SVG/GIF |
| Object Lab | `tools/video_editor/lab/` | `object_ops.h` masks/cutout/copy-region/inpaint (exemplar, progress+cancel); `mask_refine.h` Refine-Edge (feather/grow/contrast); `AsyncOp` — the pattern to reuse for long paint/filter ops |
| Compositor | `tools/video_editor/compositor.h` | CPU straight-alpha BGRA layer stack, `ApplyBlendModePixels` (7 modes), `ApplyMaskPixels`, `WarpClipFrame`, and `Clip::is_adjustment` — renders effect chain onto snapshot-of-below = exact non-destructive-adjustment-layer mechanic |
| OIIO | `tools/video_editor/io/oiio_image.h` | EXR/DPX/TIFF/RAW/PSD-flattened reads, PNG/EXR/TIFF writes, delay-loaded |
| OCIO | `tools/video_editor/color/ocio_transform.h` | display transform + `.cube`/`.3dl` profiles, `assets/color.yaml` |
| Canvas UX | `tools/preview/photo_viewer_widget.h` | working zoom-at-cursor/pan/crop/rotate widget — M1 reference; `src/ui/canvas/canvas.{h,cc}` Skia-backed canvas (MSYS2 libskia = raster only) |
| Undo | `node_graph_history.h`, `timeline_history.*` | Command{Apply/Revert/MergeWith} + History{Execute/Undo/Redo/BeginMacro} |
| Worker infra | `render-worker-infra.md` | `RenderStore` shared-mem BGRA upload, `kPushLayer`/`kSetTransform` ops, D2D + Vulkan backends |
| Workspace | `assets/ui/workspaces.yaml` | named presets are data — "Photo" = yaml + new panels |
| OpenCV | `src/wrapper/opencv/` | OpenCV 5.0 delay-loaded — analysis/smart-select later |

**Honest gaps:** no tile store (everything full-frame copies — fine
at 4 MP, memory-explosive at 100 MP × 20 layers), no per-stroke undo,
no brush engine (dabs/spacing/pressure), no layered-PSD R/W, no
selection model beyond Lab masks, no stills document format; bridge
API is buffer-in/buffer-out with no ROI.

## 2. Candidate engines

### 2.1 Skia (already linked) — YES as raster core, with limits

- All 29 blend modes name-mapped; SkImageFilters already included;
  `SkRuntimeEffect` (SkSL runtime shaders) not yet used — the natural
  "user filter" escape hatch, works on CPU raster.
- `SkColorSpace` covers ICC-tagged sRGB/AdobeRGB/Rec2020; no CMYK,
  `kRGBA_F16` is the deep-format path.
- Why Krita doesn't use Skia: Krita's composition is colorspace-aware
  at arbitrary depth (pigment/LCMS) and tile-based; Skia raster is
  display-resolution sRGB-optimized. Skia answers "draw pixels fast",
  not "store editable multi-depth document". Verdict: display/
  compositing/effects engine, NOT the document store.
- GPU: MSYS2 libskia is raster-only (no Ganesh D3D headers). Chrome
  shipped Graphite default on Apple Silicon July 2025, opt-in
  elsewhere; Ganesh maintenance-mode. GPU filter acceleration should
  be custom Vulkan compute, not Skia GPU. Sources:
  https://blog.google/chromium/introducing-skia-graphite-chromes/,
  https://skia.org/docs/user/api/skcanvas_creation/

### 2.2 Krita — the blueprint to copy (conceptually)

- pigment: colorspace plugins over LCMS → maps to our OCIO + tagged
  buffers.
- Tile engine: `KisPaintDevice → KisTiledDataManager`: 64×64 tiles,
  sparse/auto-growing, copy-on-write, default-pixel; undo =
  `KisMementoManager` storing old tile data per transaction —
  stroke-level undo costs only touched tiles. Sources:
  https://community.kde.org/Krita/Transactions_Design
- paintops: pluggable brush engines driven by serialized presets
  with sensor curves; KisPainter dabs onto a paint device with
  transactions. Copy: preset-as-data, sensor curves,
  paint-onto-temporary-device-then-composite.
- Layers: paint/group/adjustment/transparency-mask/filter-mask/
  transform-mask/selection-mask/clone/generator. Our subset:
  raster/group/adjustment/text/vector + transparency masks.

### 2.3 GEGL + babl — powerful, probably wrong for us

- babl = any-to-any pixel-format engine; GEGL = node-graph ops over
  tiled GeglBuffer. GIMP 3.0 (2025-03-16) ships GEGL-based NDE
  filters — proven model. https://www.gimp.org/release-notes/gimp-3.0.html
- License: LGPL-3 core but package mixes GPL-3 ops — every op needs
  audit; GPL ops can't ship in closed binary.
- Weight: GLib/GObject/GModule alien runtime.
- Verdict: don't vendor. Copy two ideas — buffer tiles + ops-as-
  nodes-with-inputs+params — as ~1k-line "GEGL-lite" on our types.
  https://developer.gimp.org/api/gegl/

### 2.4 libmypaint — realistic tier-2 brush engine

- C library: realistic dabs (radius/opacity/hardness/smudge/wetness/
  direction/jitter), sensor mapping; used by GIMP, Krita, OpenToonz.
  ISC license. MSYS2 `mingw-w64-clang-x86_64-libmypaint 1.6.1-5`;
  `mypaint-brushes2` presets package. https://github.com/mypaint/libmypaint
- Caveats: 1.6.1 (2020) effectively unmaintained; 15-bit-ish
  RGBA16-premul surface via MyPaintSurface callbacks over our tile
  device. Verdict: vendor behind interface after own dab engine —
  painterly quality for ~a week of binding.

### 2.5 References

- GIMP 3.0 confirms adjustment-layer-as-params model.
- Photopea (closed, JS+WebGL, single dev): PSD spec documents parse
  only; blend-mode + layer-fx semantics reverse-engineered. Uses GPU
  compositing — 10-MP × N-layer at 60fps needs it. Lesson: per-layer-
  effect semantics is the real cost. blog.photopea.com writeups.

### 2.6 PSD/PSB I/O

| Option | License | Read | Write | Layers | Notes |
|---|---|---|---|---|---|
| **PhotoshopAPI** (EmilDohne) | BSD-3 | ✅ psd+psb | ✅ | full nested, all depths, masks, text, smart objects | modern C++20, active; same author fixed OIIO's PSD reader |
| psd_sdk (MolecularMatters) | BSD-2 | ✅ | limited | hierarchy/masks | dated; fork adds PSB/smart-obj |
| OIIO (linked) | Apache-2 | ✅ | ❌ | 8-bit subimages; 16/32-bit layer data historically broken | keep for flattened + other formats |
| own `PsdDecoder` | ours | probe+flatten | ❌ | none | thin |

Verdict: vendor PhotoshopAPI at M5; OIIO stays flattened path. PSD
adjustment-layer/fx semantics mapping is M5's real cost, not parsing.

### 2.7 Color — existing OCIO, defer lcms2

OCIO display transform + .cube/.3dl profiles cover display + filmic.
lcms2 needed only for CMYK/ICC authoring (soft-proof/print) — later.

## 3. Recommended architecture

```
tools/video_editor/photo/            (Phase-3 workspace, UI-free core)
  document.{h,cc}     Document {canvas w/h, working space, layer tree}
  tile_store.{h,cc}   TiledDevice: 64x64 tiles, COW, sparse hash-map,
                      default pixel, memento undo (KisTiledDataManager)
  layers.{h,cc}       Layer{opacity, blend(SkBlendMode name), visible,
                      mask: TiledDevice(A8)} + Raster/Group/Adjustment/
                      Text/Vector kinds
  op_graph.{h,cc}     Node{inputs[], params, eval(rect)} — GEGL-lite
  brush.{h,cc}        DabEngine: stamp tip w/ spacing/flow/opacity/
                      jitter/pressure onto scratch device, composite
                      at stroke end (Krita stroke isolation)
  selection.{h,cc}    u8 mask + marching-ants; rect/lasso/poly/
                      magic-wand (MaskFromColorRange)
  photo_history.{h,cc} Commands reuse node_graph_history; strokes hold
                      tile mementos, not copies
```

- Working format: RGBA8 tiles at M1; Tile typed to promote to
  RGBA_F16 — don't bake 8-bit into op_graph signatures.
- Compositing: bottom-up per-dirty-rect eval → RenderStore upload →
  kDrawResource; OCIO display transform when enabled.
- Non-destructive: adjustment layer = params + op-graph eval against
  composite-below (compositor's is_adjustment snapshot mechanic).
- Undo: structural ops as Commands; strokes as tile mementos.
- Brushes: M1 own dab engine; M2+ IBrushEngine + libmypaint binding.
- Workspace: `Photo` entry in workspaces.yaml + panels; viewport
  reuses photo_viewer_widget zoom/pan.

## 4. Build order

| MS | Deliverable | Contents | Risks |
|---|---|---|---|
| M1 | Canvas + paint | Document, TiledDevice, viewport pan/zoom/checker, dab brush, eraser, fill/gradient, PNG IO | tile-COW correctness (stroke→undo→redo pixel-exact test); RenderStore 64MB cap; pressure input untested |
| M2 | Layers | tree, 29 blend modes, panel, move/transform, merge/flatten, groups | straight-vs-premul alpha trap; mask ordering; group isolation semantics |
| M3 | Adjustments + filters | adjustment layers via bridge suite, FilterChain persistence, gallery | curves/HSL widgets new; 8-bit banding → F16; param→undo schema |
| M4 | Masks + selections | A8 mask devices, mask painting, Refine-Edge, rect/lasso/magic-wand, clone/heal/inpaint | selection↔mask↔stroke UX fiddliest; inpaint cost |
| M5 | PSD I/O + doc format | vendor PhotoshopAPI; `.xerphoto` (JSON+tile blob) native; OIIO flattened fallback | PSD semantics mapping deep; smart objects flatten w/ warning; MSYS2/clang build unverified |

## 5. Vendor vs write

| Library | License | MSYS2 | Role | Call |
|---|---|---|---|---|
| Skia | BSD-3 | in-tree | raster/blends/filters | KEEP |
| OIIO | Apache-2 | in-tree | flattened PSD + formats | KEEP |
| OCIO | BSD-3 | in-tree | display color | KEEP |
| PhotoshopAPI | BSD-3 | vendor | layered PSD R/W | VENDOR M5 |
| libmypaint | ISC | `libmypaint`+`mypaint-brushes2` | painterly brushes | VENDOR tier-2 |
| lcms2 | MIT | `lcms2` | CMYK/ICC | LATER if print scope |
| GEGL+babl | LGPL+GPL mix | `gegl` | filter graph | SKIP → GEGL-lite |
| OpenCV | Apache-2 | in-tree | CV assists | KEEP for smart-select |
| Tile engine/layers/dab/selections | — | — | document core | WRITE (Krita-derived) |
| GPU filter path | — | — | accel | DEFER Vulkan compute post-M5 |

## 6. Open questions

- Native format: `.xerphoto` (JSON+zlib tiles) recommended over
  PSD-as-native.
- 8-bit data model + F16-capable tile type; revisit before M3.
- `tools/video_editor/photo/` workspace vs sibling `tools/photo_editor/`
  — suite-architecture leans workspace mode.
- Verify SkRuntimeEffect/SkImageFilters presence in MSYS2 libskia at
  build time (`__has_include` only confirmed intent).
