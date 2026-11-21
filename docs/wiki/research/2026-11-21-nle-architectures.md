# NLE architectures & embeddable libraries — research

Research for the Cut/Motion workspaces (`suite-architecture.md`
Phases 1–2). Repo claims verified in the working tree, 2026-11-21;
external claims carry URLs. Subagent research; parent filed the page.

## 0. Where we stand (repo inventory, 2026-11-21)

| Asset | Where | What it is |
|---|---|---|
| Timeline model | `tools/video_editor/timeline_model.{h,cc}` | Pure-data `Project{tracks,clips,transitions}`, transactional mutators, `.xerproj` JSON v1 store |
| Compositor | `tools/video_editor/compositor.{h,cc}` | CPU BGRA8 straight-alpha; per-clip lazy `IMediaDecoder` sessions; sequential forward decode; linear `ClipEffect` chain per clip; `is_adjustment` = adjustment layer |
| Preview | `tools/video_editor/playback/preview_engine.h` | One worker thread, wall-clock playhead, session-continuity model — ANY backward seek re-opens decoders |
| Export | `tools/video_editor/export_engine.*` | Worker-side streaming mux, atomic temp→commit |
| Decode isolation | `media_worker_{client,server,protocol}.*` | `--media-worker` headless JSON-lines subprocess; crash isolation |
| Prep caches | `media_prep.*`, `poster_cache.*` | Bounded pool, dedup queue; poster LRU 64 MiB; peaks LRU 64 |
| Own plugins | `plugin_host.{h,cc}`, `sdk/xer_plugin.h` | `plugin:<id>` DLL effects, C ABI, param descriptors |
| VST3 audio | `vst/vst3_factory.*` | Full VST3 host; dual-mono stereo wrap; native IPlugView editors |
| JS fx | `js_fx.*`, `assets/scripts/fx/` | `script:<id>` video/audio/transition via V8 |
| Extensions | `extensions/ext_host.*`, `xsjo_jit.*` | `.xexrj` packages, in-process Clang + LLJIT, ve.core.h v3 |
| Color | `color/ocio_transform.*`, `lut_manager.*` | OCIO display transform (delay-loaded), .cube/.3dl LUTs |
| Frame cache (dormant) | `src/video/` `ClipFrameCache` | `.dxcfz` disk frame cache — exists in `XER_Video`, NO editor consumer (2026-11-21) |
| Render infra | `render-worker-infra.md` | `RenderStore` shared-mem BGRA cache, D2D/Vulkan backends — display-side |

**Honest gaps (verified):** no per-frame render cache (every preview
tick re-decodes and re-composites), no proxies, CPU-only decode (no
FFmpeg hwaccel), CPU-only pixel pipeline (bokeh_dof = 24 taps/px
won't hold 1080p realtime), linear per-clip chains only (no node
graph), no video plugin standard beyond own ABI + JS.

## 1. Open-source NLE architectures

### 1.1 MLT (Kdenlive's engine) — canonical reusable NLE core

- Everything is a service: producers produce `mlt_frame`, consumers
  pull; filters 1 frame in → 1 out; transitions two tracks →
  modified-A + unmodified-B. Lazy evaluation.
  https://mltframework.org/docs/framework/
- Playlist = row of clips, tractor = stack of rows; `mlt_field`
  plants filters/transitions on tracks. The entire multi-track
  model — no bespoke timeline class.
  https://mltframework.org/doxygen/classMlt_1_1Field.html
- XML is the project format AND the render description:
  `.kdenlive` IS MLT XML — `melt project.kdenlive` renders
  headlessly. https://github.com/KDE/kdenlive/blob/master/dev-docs/fileformat.md
- Generic property bag on every service; FX in RPN order.

**Maps to us:** our Compositor is already a pull consumer;
`IMediaDecoder` ≈ producer; `ClipEffect` ≈ attached filter. Idea to
copy: one service registry + property bag (producers/consumers/
filters/transitions/sinks by name — our PluginHost covers effects
only). Keep `.xerproj` render-complete so a headless `--render`
(melt-equivalent) consumes it verbatim.

### 1.2 Shotcut — MLT + Qt, filter UI as data

Filter UI is metadata: `FilterController` merges per-filter QML
descriptor files with repository services — engine services and
their editor UI fully decoupled. Our `fx.json` catalog +
`XER_ParamDesc` is the same pattern — keep UI descriptors as data.
https://deepwiki.com/mltframework/shotcut/5.1-filter-framework

### 1.3 Olive — closest architectural match

- Node-based: Block/Clip/Track are Nodes; RenderProcessor walks the
  graph. https://deepwiki.com/olive-editor/olive/3-node-system
- RenderManager owns `FrameHashCache` (key = hash of node state +
  time), `AudioPlaybackCache`, `DecoderCache` (inactivity sweep),
  `ShaderCache`; async RenderTickets.
- **PreviewAutoCacher**: private project copy, background-renders
  ±playhead, reprioritizes on movement, invalidates on node changes.
- OCIO sole CMS; cache lives in reference space, view transform at
  display.
- Status caveat: alpha, single-author, no commits since ~2024-12 —
  read the code, don't wait for releases.
  https://github.com/olive-editor/olive/issues/2395

**Maps to us:** FrameHashCache + AutoCacher + DecoderCache are
precisely our top gap. `MediaPrep` (dedup queue, generation
invalidation) is already the right skeleton for an auto-cacher.

### 1.4 Blender VSE — proxy system and cache discipline

- Strict split: `source/blender/sequencer` = core (strips, proxies,
  caching, prefetch); `editors/space_sequencer` = UI. DNA structs
  carry invariants — mutate through core API only.
- Proxy: re-encoded files at 25/50/75/100%, per-strip `StripProxy`
  (size flags, quality, VFR timecode remap); build = GOP-chunked
  threaded transcode.
  https://developer.blender.org/docs/features/sequencer/proxy_system/
- Runtime prefetch + frame cache stored on `Editing`.

**Maps to us:** `StripProxy`-like record + GOP-chunked transcode job
through `media_worker` + timecode remap for VFR.

### 1.5 Flowblade / Pitivi+GES

- Flowblade: edge-based compositors (explicit src/dst tracks);
  Container Clips = (program, unrendered, rendered) per-clip bake.
- GES: `GESTimeline` IS a GstElement — timeline-as-pipeline-element
  framing. GLib/GObject alien runtime → skip as dependency, but the
  "our Compositor should satisfy a producer interface so sequences
  nest as clips" idea transfers.

### 1.6 Avidemux / VirtualDub — filter-chain lineage

- VirtualDub VF1→VF2 is the key lesson: VF1 = strict
  one-in/one-out Process() (predictable, host-prefetchable); VF2 =
  `Prefetch()` declares needed source frames + `Run()` — temporal
  filters expressible AND parallelizable.
  https://www.virtualdub.org/blog2/entry_064.html

**Maps to us:** our `XER_ApplyVideoFn`/`ClipEffect` is VF1 — fine
for point filters, wrong shape for temporal fx (motion blur, echoes).
Before Motion phase the effect ABI needs a frames-needed
declaration (VF2-style = OFX `getFramesNeeded`).

## 2. Plugin / FX frameworks

### 2.1 OpenFX (OFX) — industry image-effect standard

- ASWF, BSD-3 headers; C API: opaque handles + named properties +
  versioned suites. Hosts: Nuke, Resolve, Fusion, Natron, Vegas;
  plugin universe = Sapphire, Boris FX, RE:Vision, Neat Video.
  https://openfx.readthedocs.io/en/latest/Reference/ofxCoreAPI.html
- Generic C++ `HostSupport` layer in the repo is reusable;
  host-specific work = translating render/ROD/ROI/getFramesNeeded/
  isIdentity actions + param UI from descriptors. Natron's
  `Engine/Ofx*` is the living reference; TuttleOFX (LGPL-weak,
  unmaintained ~8y) the dead one to study.
- Minimal CPU host = weeks; production-grade = quarters.
- **Verdict: worth it at Motion phase** — the only standard buying
  a commercial plugin ecosystem; gated on frame cache existing
  (plugins assume `getFramesNeeded`-competent hosts).

### 2.2 Frei0r — minimal video-effect API

- Tiny C API (source/filter/mixer types), 100+ plugins; hosted by
  FFmpeg/MLT/Kdenlive. Host effort ≈ few hundred lines — a foreign
  `xer_plugin` ABI. https://dyne.org/frei0r/
- **License caveat: plugin collection is GPL-2.0** (API header
  free) — OK while internal/GPL-compatible; decision needed before
  commercial distribution. MSYS2 `frei0r-plugins 3.6.0`.

### 2.3 Audio — VST3 already covers it

LADSPA/LV2 are Linux-ecosystem; skip both.

## 3. GPU / video stacks

### 3.1 FFmpeg hwaccel — near-free decode speedup unused

- Prefer hwaccel APIs (d3d11va/nvdec/qsv: lavc parses, API
  reconstructs, runtime-switchable) over whole-pipe hw decoders.
- Pattern: `av_hwdevice_ctx_create` → `get_format` picks hw pixel
  fmt → GPU surfaces → `av_hwframe_transfer_data` to system mem.
  https://www.ffmpeg.org/doxygen/trunk/hw__decode_8c_source.html
- Our bridge is software→BGRA today; adding hwaccel = one new
  branch, no new dependency; keeping D3D11 textures also feeds the
  GPU render path without copy-back.

### 3.2 VapourSynth — scriptable frame server

Lazy pull graph, planar VSFrame COW, four filter threading modes.
MSYS2 package stale (R65 vs 81); overlaps js_fx. **Defer** — copy
its filter-mode taxonomy when the ABI grows temporal filters.

### 3.3 GStreamer / GES — skip (GLib runtime), reference only.

### 3.4 libplacebo — the high-quality GPU frame renderer

- mpv's renderer as a library: Jinc/polar scaling, dynamic HDR tone
  mapping, Dolby Vision p5, ICC/BT.1886/.cube 3DLUTs, pluggable user
  shaders (FSRCNNX-class), debanding, deinterlacing, error-diffusion
  dither. Vulkan + OpenGL + **D3D11** backends. `pl_frame_from_avframe`
  + `pl_render_image`. https://github.com/haasn/libplacebo/
- LGPL-2.1; MSYS2 `libplacebo 7.387` (deps vulkan/shaderc/
  spirv-cross/lcms2/libdovi — most already staged).
- For us: (a) high-quality canvas scaler for the monitor, (b)
  Vulkan/D3D11 compute substrate for GPU filters, (c) display-side
  HDR/color composing with OCIO. Seam = post-composite present stage
  first, then WarpClipFrame/blends onto shaders. Fits delay-load
  convention.

### 3.5 OBS libobs — module taxonomy worth stealing

Four plugin types: sources (media AND filters), outputs, encoders,
services; `obs_properties` → generated settings UI; three threads.
Our PluginHost could adopt the same 4-type registry shape.

## 4. Caching / proxy — the blueprint section

- **Premiere**: Media Cache dir + DB; `.pek` peaks + `.cfa` conformed
  audio; recreated on demand. We have peaks/posters; missing:
  conformed audio, cache-dir setting, eviction.
- **Resolve**: Optimized Media (proxies), Timeline Proxy Resolution
  (½/¼ playback), Render Cache Off/Smart/User — Smart auto-flags
  expensive clips, caches during play AND pauses, invalidate on
  modify; codec/res configurable.
- **Olive**: hash cache + AutoCacher + DecoderCache (most directly
  implementable).
- **Blender**: per-strip proxies + prefetch + VFR remap.

**Concrete plan for our preview:**
1. FrameHashCache-analog: key = hash(clip id, source t, effect
   params, transform, quality); value = video::Frame (mem tier via
   PosterCache-style byte-LRU; disk tier via `ClipFrameCache` .dxcfz).
2. AutoCacher = MediaPrep-shaped pool, private project snapshot,
   ±N s radius, invalidate by param hash.
3. Smart-cache flag: auto-mark clips whose effect chain exceeds a
   measured cost (bokeh/lens) → bake .dxcfz during pauses.
4. Proxies: StripProxy record + GOP-chunked transcode via
   media_worker + per-sequence toggle + VFR remap.
5. DecoderCache: warm `IMediaDecoder`s with inactivity sweep —
   kills the documented backward-seek reopen cost.

## 5. Deliverable matrix

| Library | License | MSYS2 | What it gives | Effort | Call |
|---|---|---|---|---|---|
| **libplacebo** | LGPL-2.1 | 7.387 | GPU scale/composite/HDR/shaders (Vulkan+D3D11) | M | **VENDOR** |
| **Frei0r** | API free; plugins GPL-2.0 | 3.6.0 | ~100 video fx, ~300-line host | S | VENDOR if license OK |
| **OFX headers+HostSupport** | BSD-3 | vendor | industry FX hosting | L | VENDOR at Motion |
| TuttleOFX | LGPL-weak | dead | OFX host reference | — | READ-ONLY |
| **FFmpeg hwaccel** | LGPL (in-tree) | — | d3d11va/nvdec/qsv decode | M | IMPLEMENT |
| VapourSynth | LGPL-2.1 | R65 stale | scripted sources | M/H | DEFER |
| MLT | LGPL-2.1 | yes | NLE engine | — | copy concepts only |
| GStreamer+GES | LGPL | yes | pipelines | — | SKIP (GLib) |
| LADSPA/LV2 | var. | partial | audio plugins | — | SKIP (VST3) |
| OBS libobs | GPL-2.0 | n/a | module taxonomy | — | copy pattern |
| OpenTimelineIO | Apache-2 | `otio` | EDL interchange | S | LATER |

## 6. Recommendation — top-3 by value/effort

1. **Hash-keyed render cache + PreviewAutoCacher** — no dependency,
   pure architecture; the biggest UX gap. Ports almost mechanically
   onto MediaPrep + PosterCache + dormant ClipFrameCache (.dxcfz).
2. **libplacebo + FFmpeg hwaccel** — two halves of one GPU path:
   hw decode → AVFrame → pl_frame_from_avframe → GPU
   composite/present. Fixes bokeh/4K realtime without hand Vulkan.
3. **Frei0r host now (license check), OFX host at Motion phase** —
   OFX is the strategic play (commercial FX ecosystem, BSD headers,
   sidesteps GPL plugin issue).

| Survey idea | Our gap | Lands where |
|---|---|---|
| Olive caches trio | no frame cache; backward seek reopens | new render-cache design; `playback/` |
| Blender/Resolve proxies | no proxy system | `media_catalog` + `media_worker` |
| MLT service registry | 3 ad-hoc registries | unify + param descriptor schema |
| VF2 prefetch / OFX getFramesNeeded | ABI is 1-in/1-out | Motion: temporal-fx ABI decision |
| MLT XML = project+render | `.xerproj` nearly there | headless `--render` consumer |
| GES timeline-is-element | compositor isn't a producer | nested comps (Motion) |
| Premiere .cfa conform | peaks only | conformed audio in cache dir |

## 7. Sources

MLT framework/melt/mltxml docs + Field doxygen + mlt_tractor.c;
Kdenlive dev-docs/fileformat + sabaoon.dev case study; Shotcut
AGENTS.md + deepwiki; Olive deepwiki node-system/rendering + issues
#237/#2395; Blender sequencer docs + D10394; Flowblade webhelp +
Pitivi/GES docs; admWiki + VirtualDub VF2 blog + VirtualDub2 wiki;
openfx.readthedocs.io + ASWF openfx + Natron host docs + TuttleOFX;
dyne.org/frei0r + MLT frei0r deepwiki; ffmpeg hw_decode.c +
hwconfig; libplacebo repo/docs + MSYS2 pkg + mpv Vulkan-video FAQ;
vapoursynth docs; OBS backend-design; Adobe media-cache help;
Resolve 12/18 manuals.
