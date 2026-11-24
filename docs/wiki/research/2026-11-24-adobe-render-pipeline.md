# Adobe/Resolve render & playback pipelines — lessons for VideoEditor

Research date 2026-11-24. Repo claims verified in the working tree;
external claims carry URLs. Companion docs:
[2026-11-21-nle-architectures.md](2026-11-21-nle-architectures.md)
(Olive/MLT blueprint that shaped our cache + autocacher),
[2026-11-23-nle-workflow-anatomy.md](2026-11-23-nle-workflow-anatomy.md)
(monitor/transport chrome).

## 0. Where we stand (verified 2026-11-24)

| Piece | Where | What it actually does |
|---|---|---|
| Compositor | `tools/video_editor/compositor.{h,cc}` | CPU-only packed BGRA8 straight-alpha pipeline: decode → ordered `ClipEffect` chain → masks → `Transform` warp (always bilinear) → transition combine → alpha-over bottom→top onto `bg_color`. `is_adjustment` clips snapshot the already-composited canvas. Single consumer thread; `RenderFrameAt` assumes non-decreasing `t` per session. `SetPreviewQuality` only runs the four heavy spatial kernels (lens sphere/magnifier/ribbed glass/bokeh) on a downscaled copy (`compositor.cc:1374-1388, 1948-2055`) — decode and every other effect still run at full res. |
| Effect cache | `tools/video_editor/effect_frame_cache.{h,cc}` | In-memory-only LRU, 512 MiB default (~60 1080p BGRA frames), `XER_VE_FX_CACHE_MB` override. Stores **raw uncompressed BGRA8** post-mask clip-space frames. Key = `{clip_id, source_time_us, state_hash}`; `state_hash` = FNV-1a over path + resolver output + source kind/param + ordered (effect type, raw param bits) + reframe/object/mask track samples + masks + preview-quality + canvas dims (`compositor.cc:841-924`). `script:`/`ocio:`/non-builtin `plugin:` chains excluded (`ChainCacheable`, `compositor.cc:820`). A separate `MemoHash` memoizes the post-warp layer when transform repeats (`compositor.cc:935`). **No disk tier.** |
| Preview engine | `tools/video_editor/playback/preview_engine.{h,cc}` | One worker thread; wall-clock playhead `t = anchor + Δnow·rate`; each tick renders the frame *containing* `t` (floored to frame boundary so cache keys match autocache, `preview_engine.cc:438-441`). Slow renders → the next tick's `t` simply skips ahead: **implicit frame dropping with NO counter**. Sleep = `frame_dt/|rate|`. Audio free-runs 120 ms ahead into a blocking WASAPI sink — a slow render can never starve audio (Premiere's "audio never drops, video does" policy, already correct by construction). Any backward move or >0.5 s forward jump re-opens decoder sessions. |
| AutoCacher | `tools/video_editor/playback/auto_cacher.{h,cc}` | Olive-model idle pre-render: private compositor + own decoder sessions on a `THREAD_PRIORITY_BELOW_NORMAL` worker (`auto_cacher.cc:384`), fills the shared cache over `[ph−0.5 s, ph+3 s]` in **strictly increasing frame order** (FIFO deque — not a priority queue). 400 ms seek-settle, stale-drop at pop, dedup set, budget capped to 50 % of LRU bytes / frame size, max 360 frames. Cooperative `Compositor::Cancel` on seek. |
| Proxies | `tools/video_editor/proxy_manager.{h,cc}` | One background worker transcodes to h264/MP4 ≤960-long-edge proxies; `ResolveDecodePath` resolver seam into compositor + autocache + posters; file-mtime LRU storage cap; never in export/audio path. |
| Export | `tools/video_editor/export_engine.{h,cc}` | Own worker thread, full quality, **no cache injected, no resolver** (proxies can never reach the encode). Honors `Project::work_in/out`. No "use previews" fast path. |
| Monitor | `tools/video_editor/ui2/monitor_surface.*`, `monitor_panel.cc` | Push/pull BGRA feed; manual quality select Full/½/¼ (`monitor_panel.cc:318-325`) — **no dropped-frame badge, no paused-vs-playing split, no auto ladder**. |
| Timeline | `tools/video_editor/ui2/timeline_surface.cc` | Ruler with work-area bar handles + snap indicator — **no render-status bar**. |
| Dormant disk frame cache | `src/video/clip_frame_cache.{h,cc}` | `ClipFrameCache` writes `frame_N.dxcfz` per frame under a keyed root with TTL (only consumer: `music_bass_eq_clip.cc`). **No editor consumer** — the Premiere "preview file" analog already exists unused. |

---

## 1. Premiere Pro anatomy

### 1.1 Mercury Playback Engine — what's GPU, what's CPU

- MPE is explicitly heterogeneous: the **CPU is the master scheduler** —
  it handles UI, disk I/O, source decode of every codec, and parses the
  timeline to determine what must render per frame; the **GPU gets kicked
  for image-processing stages**: scaling, deinterlacing, blending, color
  conversions, plus a defined GPU-accelerated effects list
  (NVIDIA/Adobe MPE whitepaper,
  https://www.nvidia.com/docs/IO/85230/CUDA_MPE_WP-05380-001_v01.pdf).
- Renderer selectable per project: Mercury GPU Acceleration
  (CUDA/Metal/OpenCL) vs Software Only
  (https://helpx.adobe.com/premiere/desktop/get-started/technical-requirements/enable-mercury-playback-engine-gpu-accelerated-renderer.html).
- GPU-accelerated effects carry a badge in the Effects panel; the
  published list (Jan 2026) includes Lumetri Color, Gaussian Blur,
  Transform, Crop, Corner Pin, Drop Shadow, ProcAmp, Sharpen, Ultra Key,
  Warp Stabilizer, Twirl, Alpha Adjust, Levels, Mosaic, Ramp, VR set —
  i.e. per-pixel color/geometry/blur kernels, the same class as our
  builtin `ClipEffect`s
  (https://helpx.adobe.com/premiere/desktop/add-video-effects/types-of-effects/list-of-accelerated-effects.html).
- **Whole-chain CPU fallback per clip**: one CPU-only effect anywhere in
  a clip's effect stack pulls the *entire* chain for that clip onto CPU —
  the round-trip GPU→CPU→GPU copy costs more than staying on CPU
  (Adobe engineer, https://community.adobe.com/t5/premiere-pro-discussions/pre-render-is-handled-by-cpu/m-p/14745747).
  SDK-level detail: fallback is decided at GPU-effect *instance
  creation*, no mid-render fallback
  (https://deepwiki.com/docsforadobe/premiere-plugin-guide/6.1-gpu-programming-model).
- Effect render order: fixed/intrinsic effects (Motion, Opacity,
  Time-remap) are conceptually separate from standard effects applied
  top-down in Effect Controls — our chain→masks→warp order is the same
  "pixels first, transform last" boundary.

### 1.2 Render bars — the needs-render model

The thin bar at the bottom of the Timeline ruler is a **static cost
estimate per sequence region**, not a measured one ("*probably*" — Adobe
blog, https://blog.adobe.com/en/publish/2011/02/20/red-yellow-and-green-render-bars):

| Color | Meaning |
|---|---|
| **Green** | Rendered preview file exists → full-quality realtime *guaranteed* (plays the file). |
| **Yellow** | No preview file; realtime render per frame; *probably* keeps up. |
| **Red** | No preview file; *probably* can't keep up. |
| **None** | Source codec simple enough to be its own preview (legacy DV/DVCPRO). |

Triggers (GPU mode): hard codec (AVCHD-class) or clip/sequence settings
mismatch → yellow; GPU-accelerated effect/transition → yellow; CPU-only
effect/transition → **red** (transitions only paint over their duration).
In Software-Only mode everything shifts a tier worse (settings mismatch
→ red, any effect → red). So the bar is literally a *which-processor*
indicator.

Playback/render commands: Spacebar plays whatever's there without
rendering; Enter = "Render Effects In Work Area" (red segments);
"Render Entire Work Area" covers red+yellow. Rendering writes **preview
files** to disk.

### 1.3 Preview files — codec choice is a scrub-format decision

- Preview files are sequence-resolution, I-frame-intermediate codec
  files. Default was **I-Frame Only MPEG**, changed to **ProRes 422 LT**
  in Premiere 23.0 — better quality-per-byte and it enables *smart
  rendering* (export re-uses the compressed preview frames verbatim when
  the export codec matches); Cineform and DNxHD/HR also selectable
  (Adobe principal PM,
  https://community.adobe.com/questions-734/discuss-use-prores-as-the-default-codec-for-sequence-rendering-313242;
  codec-change FAQ,
  https://community.adobe.com/questions-729/faq-how-to-change-the-codec-for-video-preview-files-1393003).
- Intra-frame-only is the point: every frame self-contained → cheap
  random access, no GOP decode chain for scrub/loop. Premiere renders
  the *whole composite* to the preview file — its cache boundary is the
  sequence output, not the per-clip layer.
- "Use Previews" on export = speed hack with a quality warning;
  "Match sequence preview settings" enables smart rendering.
- Separate machinery: the **media cache** (`.cfa` conformed audio +
  `.pek` peaks) normalizes audio at import — decode-time, not
  render-time, cache.

### 1.4 Degradation ladder during playback

- **Playback Resolution** and **Paused Resolution** are independent
  (Full/½/¼, deeper fractions only on >1080 frame sizes); scrubbing uses
  playback res; paused frames can stay Full for focus checks
  (https://helpx.adobe.com/nl/premiere/desktop/get-started/source-and-program-monitor-adjustments/set-display-quality-for-the-source-and-program-monitors.html).
- Default playback quality is *slightly softer* than paused even at the
  same resolution; "High Quality Playback" removes the bump at a
  drop-frames cost (https://helpx.adobe.com/premiere/desktop/get-started/source-and-program-monitor-adjustments/enable-high-quality-playback.html).
- **Dropped Frame Indicator**: stoplight icon under both monitors,
  green→yellow when drops occur, tooltip shows the count, resets per
  playback (https://helpx.adobe.com/premiere/desktop/get-started/source-and-program-monitor-adjustments/enable-dropped-frame-indicator.html).
  Policy: audio plays continuously; *video frames drop* to hold sync —
  the same implicit policy our wall-clock playhead implements.
- **Dynamic Link**: Premiere hosts AE comps (and Audition etc.) via a
  headless hand-off — Premiere requests frames, the AE engine renders
  them into the sequence with no intermediate file; DL clips are the
  classic red-bar content and "Render and Replace" bakes them to media.

---

## 2. After Effects anatomy

### 2.1 RAM preview — why realtime is only possible *after* caching

AE's render-per-frame cost is orders of magnitude above an NLE's
(layers × effects × motion blur × 3D), so it assumes realtime is
impossible until frames are already rendered:

- Preview first *renders* frames into RAM — green bar fills the time
  ruler — then plays the cached span at framerate. Edits that touch a
  cached frame's inputs blow its cache slice. When the RAM cache fills,
  AE caches only what fits and plays that
  (https://helpx.adobe.com/in/after-effects/using/memory-storage1.html).
- **Cost-aware caching**: AE does *not* cache frames that are cheap to
  re-render — cache is reserved for expensive frames (same source). This
  is a per-frame cost gate we don't have (our LRU caches everything
  reachable).
- **Global performance cache (CS6+)**: cache entries keyed by full
  render-state identity survive invalidation — undo an edit and the old
  frames come back without re-render; duplicated comps and reopened
  project versions hit the same frames
  (https://www.provideocoalition.com/after-effects-cs6-preview/). This
  is exactly our `state_hash` model — key purity instead of
  invalidation lists.
- **Layer-level caching**: modified layers re-render while unmodified
  layers composite from cache — a per-layer render boundary like our
  per-clip post-mask boundary.
- **Persistent disk cache**: RAM overflow for *expensive* frames goes to
  disk (blue bar), survives app restart, shared across projects;
  "Cache Work Area in Background" renders a comp's work area into the
  disk cache on a deliberately low-priority path while the user keeps
  working (Creative COW confirms it runs *slower* than foreground — by
  design, https://creativecow.net/forums/thread/cache-work-area-in-background-very-slow/).
  **Speculative Preview** (2021+): AE auto-renders the comp (and its
  precomps) in the background whenever the app goes idle — the direct
  ancestor of our AutoCacher
  (https://creativecow.net/a-faster-after-effects-is-now-available-for-testing-new-multi-frame-rendering-features-in-public-beta/).
- Purge: Edit ▸ Purge ▸ All Memory / All Memory & Disk Cache — explicit
  user flush for pathological states
  (https://helpx.adobe.com/after-effects/desktop/memory-storage-performance/memory-and-storage/memory-storage1.html).

### 2.2 Multi-Frame Rendering (MFR) — frame-level, not effect-level

- MFR renders **multiple frames of the same comp concurrently** on a
  worker pool sized by "Dynamic Composition Analysis" (cores, free RAM,
  VRAM, comp complexity). It is *inter-frame* parallelism — not
  intra-frame or per-effect threading; a %CPU-reserved slider (default
  10 %) caps the pool so the machine stays usable. Reported 1.2–4×
  export speedups, and it covers preview, Media Encoder, and Dynamic
  Link renders (https://helpx.adobe.com/after-effects/desktop/render-and-export/multi-frame-rendering/multi-frame-rendering.html;
  launch post, https://blog.adobe.com/en/publish/2021/03/10/multi-frame-rendering-now-in-after-effects-beta).
- Effect constraint: stateful/non-thread-safe effects force serial
  rendering of the frames they touch — the mirror image of our
  `ChainCacheable` purity gate.
- Render Queue HUD splits progress into exported / cached-ready /
  in-flight frames — per-frame state visualization.

### 2.3 Evaluation order + collapse transformations

- Per frame: layers evaluated **one at a time, bottom→top**; per layer
  masks → effects → transform (memory doc confirms per-layer render and
  that the most expensive *layer* bounds per-frame RAM). Nested comps
  render once as a unit and are memoized into the parent — the
  precomp-cache trick.
- **Collapse transformations / continuously rasterize**: flips the
  render boundary — the nested comp's layers render *through* the
  parent's transform at parent resolution instead of being flattened
  first. Costs quality-vs-memory: correct infinite-zoom vector
  rasterization but loses the pre-rendered layer cache. Same class of
  decision as our "cache pre-warp, warp runs every time" boundary.

### 2.4 Degradation ladder (richer than Premiere's)

- Resolution menu Full/Half/Third/Quarter/Custom — but **Auto** is the
  star: *adaptive resolution* drops the render scale during playback
  when the framerate can't be held, recovers on pause; plus Fast Preview
  modes (Adaptive Resolution / Draft / Fast Draft / Wireframe).
- **Draft 3D** kills lights/shadows/DOF/motion-blur in one switch;
  Fast Draft skips eligible effects — fidelity classes, not just
  resolution (domer.io and bluefx summaries of the shipped prefs).
- **Region of Interest**: render only a sub-rectangle of the comp —
  spatial degradation, complementary to resolution.
- Preview panel also offers **frame Skip** (render every Nth frame to
  hold real-time) and work-area confinement — a temporal degradation
  we don't have.
  (https://helpx.adobe.com/after-effects/desktop/memory-storage-performance/memory-and-storage/memory-storage1.html
  and preview-settings docs.)

### 2.5 Frame blending — Frame Mix vs Pixel Motion (vs our RIFE)

- Layer frame-blend modes: Whole Frames (nearest/duplicate), Frame Mix
  (cross-dissolve of adjacent frames — fast, ghosty), **Pixel Motion**
  (dense per-pixel motion vectors, warps neighbors into a synthesized
  middle frame — the classic liquify-on-occlusion artifact);
  Timewarp effect = tuned Pixel Motion (Kronos/The Foundry Furnace)
  with vector-detail/error-threshold controls
  (https://helpx.adobe.com/mena_ar/after-effects/desktop/animate-in-after-effects/time-stretching-and-time-remapping/time-stretching-time-remapping.html).
- Our `ai/rife_interp.cc` is the ML successor of Pixel Motion —
  occlusion-aware learned flow; the lesson is AE's *tiering*: offer
  Whole/Frame-Mix for interactive rates and reserve the neural/optical
  method for render-time or a flagged "quality" retime.

---

## 3. DaVinci Resolve anatomy (counterpoint)

- **Three cache levels, three boundaries**: Source cache (clip's
  used span, pre-grade, at timeline resolution — absorbs debayer/hard
  codec), Sequence cache (Edit-page effects: transitions, non-Normal
  composites, opacity, speed/retime), Node/Color cache (color-page
  nodes — lets you cache before/after a specific corrector node so a
  tweak to node 5 doesn't re-render nodes 1–4)
  (https://ltbits.github.io/davinci-resolve-manuals/DR12/DR12-RM-05.pdf;
  https://vfxstudy.com/tutorials/performance-cache-levels/).
- **Smart vs User**: Smart auto-flags processor-heavy items (RAW/H.265,
  Fusion comps, composites, transitions, NR); User = manual per-clip
  "Render Cache Clip Output" / Color-output flags. Timeline ruler shows
  red → blue per clip as cache lands; edits flush only that clip's
  cache (https://docs.imaster.org/davinci-resolve-21-manual/part-2-setup-and-workflows/ch.-8-improving-performance,-proxies,-and-the-render-cache;
  https://creativevideotips.com/tutorials/davinci-resolve-render-cache-essentials).
- **Idle-triggered background caching**: "Enable background caching
  after N seconds" (default ~5 s) — caching starts during user
  inactivity, a *time-since-last-edit* trigger rather than our
  playhead-window trigger (Resolve 21 manual ch. 6,
  https://docs.imaster.org/davinci-resolve-21-manual/part-2-setup-and-workflows/ch.-6-project-settings).
- **Cache is codec'd media on disk**: Render Cache Format selectable —
  ProRes, DNxHR, or uncompressed 10/16-bit float `.dvcc`; HDR variants
  preserve super-whites. Same lesson as Premiere preview files: baked
  output at timeline res in a scrub-friendly format, persistent across
  sessions (Blackmagic forum quoting the manual,
  https://forum.blackmagicdesign.com/viewtopic.php?f=21&t=203618).
- Separate-but-related: **Optimized media** (user-generated ProRes/DNxHR
  intermediates for hard sources, half/quarter res option),
  **proxy media** (lightweight edit copies — our `ProxyManager`
  analog), **Render in Place** (bake a clip to a new source file —
  strongest-boundary cache), Fusion tools' per-node *Cache to Disk*
  memory mode, and **Delivery-side reuse** of cached/optimized media
  when the output codec matches (smart rendering again).
- Neural-engine ops (Magic Mask, Speed Warp/retime, SuperScale, Voice
  Isolation) offload to GPU — Resolve's analog of Premiere's
  accelerated-effects badge.

---

## 4. Anatomy comparison table

| Axis | Premiere Pro | After Effects | DaVinci Resolve | **XER VideoEditor (today)** |
|---|---|---|---|---|
| Render boundary (cache point) | Sequence composite → preview file | Per-layer frames → RAM; comp frames → disk cache | 3 tiers: source / sequence / node per clip | Per-clip post-mask **pre-warp** frame + post-warp layer memo |
| Cache storage | Disk, I-frame codec (ProRes LT / I-frame MPEG / CineForm / DNxHR) | RAM uncompressed + persistent disk cache (expensive frames only) | Disk, ProRes/DNxHR/float .dvcc | **RAM only, raw BGRA8**, 512 MiB LRU |
| Invalidation | Region-level (edit in region kills its preview span) | Full state hash per frame (undo restores frames — Global cache) | Per-clip auto-flush on modify | Full input hash per key — **already AE-style pure**; move/retime hits survive because key is media-time |
| Cost model / needs-render UI | Render bar: static green/yellow/red/none by processor-class + codec | Green/blue bars = cached-to-RAM / cached-to-disk (coverage, not prediction) | Red → blue per clip = needs-cache → cached | **None** — no bar, no needs-render estimate, no cache-coverage display |
| Background render | Only on explicit command (Enter / Render Entire Work Area) | Speculative Preview on idle; Cache-Work-Area-in-Background on demand | Auto after N s idle (default ~5) + during playback | AutoCacher: always-on playhead window [−0.5,+3 s], 400 ms settle |
| Scheduler / priority | CPU master scheduler; GPU kernels; playback realtime-critical | MFR worker pool frames-in-flight; background cache deliberately *slower* than foreground | Background cache low-prio during idle; playback preempts | Autocache = `BELOW_NORMAL` thread, FIFO increasing-order queue, cooperative cancel — **no priority ordering among items, no demand-miss feed** |
| Degradation ladder | Playback res Full/½/¼(+/8/16) **+ separate Paused res** + HQP toggle + frame-drop w/ indicator | Adaptive res (Auto) + Draft/Fast Draft/Wireframe + ROI (spatial) + frame Skip (temporal) + work area | Timeline-proxy mode + proxy res tiers | Manual Full/½/¼ only; quality folds decode? — **no**: decodes stay source-res, only heavy-fx downscale + output nearest downsample; no auto ladder, no paused split, no drop counter |
| A/V sync under load | Audio continuous, video frames drop; dropped-frame badge | Preview can skip frames; audio opt-out | Same (video drops) | Same policy implicit (audio free-runs 120 ms ahead; playhead is wall clock) — **but unmeasured/unreported** |
| Export reuse | Use-Previews checkbox / smart rendering when codec matches | Render-queue reuses RAM/disk cached frames | Deliver can use cached/optimized media | **None** — export never injects the cache (by design: single pass) — but preview files could feed it |
| Parallel render | Per-frame GPU+CPU split; export via AME queue | MFR: N frames concurrently (frame-level) | Multiple cache render threads | **Single-threaded compositor everywhere** — no MFR analog |
| Idle work | None (explicit render commands) | Speculative preview + background comp caching | Idle-seconds background caching | Continuous autocache (closest to AE/Resolve hybrid) |

---

## 5. Gap analysis vs ours

1. **No needs-render status surface.** Premiere's red/yellow/green and
   Resolve's red→blue both answer "will this play?" and "what's
   cached?". We have *better internals than either*: `ClipStateHash`
   gives clip-state identity, `ChainCacheable` gives a purity/cost-ish
   gate, `EffectFrameCache::Stats` + `AutoCacher::Stats` give coverage
   counters — but nothing computes a per-region cost class or paints a
   bar. Cheapest honest version: green = "this frame's key is in cache
   (or will be — autocache coverage)", yellow = cheap chain (all
   builtin, no masks/adjustment), red = heavy/spatial/script/plugin or
   adjustment clip. It's a *static estimate*, same honesty level as
   Premiere's.
2. **Cache is raw BGRA in RAM only.** Every Adobe/BMD pipeline persists
   to disk in an I-frame/intermediate format so pre-render survives the
   session and scales beyond RAM. Our 512 MiB ≈ 60×1080p frames ≈ **2 s
   of runway per clip** — the autocache window (3 s ahead) can exceed it
   for multi-clip frames, which is why the budget cap exists. The
   dormant `video::ClipFrameCache` (`src/video/clip_frame_cache.h`,
   `.dxcfz` per-frame + TTL) is a ready L2 tier; a real I-frame codec
   (even intra DCT/zstd-compressed BGRA, or ProRes via our FFmpeg
   bridge) would cut bytes/frame ~5-20×.
3. **Scheduler is FIFO, not prioritized.** AutoCacher renders
   increasing frame order; a cache-miss during live playback can't
   re-prioritize the queue ("the frame that just missed is the most
   valuable"), nor can a user mark a work area for render. Premiere's
   answer is explicit render commands; AE/Resolve's is idle-triggered
   background render; ours is playhead-proximity — all correct, but we
   lack demand-miss priority and user-marked regions.
4. **Degradation is manual and shallow.** `SetQuality` does not reduce
   decode resolution, does not reduce most effect work — it
   downsamples the composited output (cheap) and downscales only the
   four heavy spatial kernels. Compare Premiere (true decode/effect
   resolution change + paused/playing split) and AE (adaptive +
   fidelity classes + ROI + skip-frames). Our badge says "½" but the
   honest work reduction is small for decode-bound timelines (where
   proxies already help) and moderate only for the four heavy kernels.
   No auto ladder: measured frame time never feeds back into quality.
5. **Dropped frames are silent.** The wall-clock playhead *does* drop
   correctly (t advances regardless of render duration; audio
   free-runs), matching the Premiere policy — but there is no counter,
   no indicator, no way to observe it. `FramesPresented` counts
   deliveries, not drops.
6. **No explicit "Render Work Area" / preview-reuse in export.** Enter
   semantics + green-bar pre-render + Use-Previews/smart-render on
   export are all absent. Our export's no-cache rule is right for
   integrity, but a disk preview tier (gap 2) would enable the
   legitimate "match preview settings" fast path.
7. **No frame-level parallelism anywhere.** MFR's win is precisely our
   gap: compositor is single-consumer-thread; export and preview each
   own one. Segmented compositors (each owns a time range, independent
   decoder sessions — the AutoCacher already proves two compositors
   can coexist) give MFR-style export without breaking the session
   contract.

Non-gaps worth noting: our hash-key model is *stronger* than
Premiere's region invalidation (Premiere kills a whole preview span on
any edit in it; we lose only the changed clip's keys, and moved/retimed
clips keep entries under media-time identity). Our audio/video drop
policy already matches Premiere. Our adjustment-layer exclusion
mirrors AE's "input can't be keyed" problem.

---

## 6. Ranked implementation tickets

Ordered by value-per-effort; sizes S (≲½ day) / M (1–3 d) / L (>3 d).

| # | Pri | Size | Ticket | Files | Rationale |
|---|---|---|---|---|---|
| T1 | **P0** | S | **Dropped-frame counter + stoplight badge.** In `RenderAndDeliver`, compute `expected_frame = floor(t/frame_dt)` at tick entry vs last delivered frame index; count skipped indices as drops; expose `DroppedFrames()`/`ResetDrops()`; badge (green→yellow + count tooltip parity) in the monitor header row. | `playback/preview_engine.{h,cc}`, `ui2/monitor_panel.cc`, `ui2/preview_binding.cc` | Every lesson doc's first finding: the drop policy already exists — it's just invisible. Enables every later adaptive decision to be *measured*. |
| T2 | **P0** | M | **Render-status bar on the timeline ruler.** Two-layer model: (a) coverage — frames whose cache key is known-present (query `EffectFrameCache` keys? cleaner: autocache publishes a coverage bitmap of rendered_ + cache stats) → green; (b) static cost class per contributing clip — no fx & easy codec → none/grey; all-builtin cheap chain → yellow; heavy-spatial/`script:`/non-builtin `plugin:`/`ocio:`/adjustment/transition span → red. Reuse `ClipStateHash`'s input domain for the cost classifier (new pure fn next to `ChainCacheable`). | `compositor.cc` (new `ClipCostClass()`), `playback/auto_cacher.{h,cc}` (coverage query), `ui2/timeline_surface.cc` (`RenderWorkArea` band / ruler strip) | Premiere's #1 surface cue; our keys make *coverage* easy and *cost* a 30-line classifier. Also gives users the "where do I need proxies" answer. |
| T3 | **P1** | S | **Paused-vs-playing quality split.** `present_pending_` renders (paused presents, seek landing) already form a distinct path — render them at effective quality 1.0 while playing ticks keep the selected tier; badge shows playback tier. | `playback/preview_engine.cc` (`RenderAndDeliver` caller), `ui2/preview_binding.cc` | Premiere Paused Resolution for free: users inspect focus/edges on pause without paying during playback. |
| T4 | **P1** | M | **Adaptive quality ladder (measured).** EMA of per-frame render ms; if EMA > `frame_dt·0.8` step down {1→½→¼→⅛?} with hysteresis + hold-off; if EMA < `frame_dt·0.4` for N frames step up. Wire to the same `SetQuality` command path so autocache keys track automatically (quality is key material — autocache's `OnQualityChanged` already handles it). Pair with T1 badge (show active tier + drop count). | `playback/preview_engine.{h,cc}`, `ui2/monitor_panel.cc` | AE adaptive-resolution + Premiere ½/¼ — but driven by measurement, not a menu. The existing nearest-downsample + heavy-fx downscale make the ladder honest-ish today; deepens naturally with T6. |
| T5 | **P1** | M | **Priority-queue autocache + demand-miss feed.** Replace the increasing-frame deque with a small priority structure ordered by distance-from-playhead (playhead-adjacent-forward first, backfill last); add `OnDemandMiss(frame_idx)` — when the playback render misses cache for frame n, n jumps the queue. Keep the sequential-forward *render* pass per compositor session (reorder within windows only; a backwards reorder = session reopen, so bucket by session-contiguous ranges). Add explicit `RenderRange(in,out)` for work-area user renders. | `playback/auto_cacher.{h,cc}`, `playback/preview_engine.cc` (miss hook) | Convert the FIFO into Resolve/AE-style priority: the frame most likely needed next renders first; misses self-heal instead of waiting for window rebuild. |
| T6 | **P1** | M | **Honest quality tiers: decode-res + chain-res, not just output res.** Quality <1 should (a) prefer proxy decode at the tier (resolver already exists — generate/choose a ½-res proxy), and (b) run the *whole* `ClipEffect` chain at reduced res for a "draft" tier, not only the four heavy kernels. Add a third semantic tier beyond ¼ = "draft" (AE Fast Draft: skip eligible expensive kernels entirely — mark per-effect `draft_eligible` in the effect table). | `compositor.{h,cc}` (`SetPreviewQuality` semantics + per-kernel eligibility table), `proxy_manager.*`, `ui2/monitor_panel.cc` (4th select entry) | Makes the quality selector reduce real work (the #4 gap) instead of only output pixels. |
| T7 | **P2** | L | **Disk preview tier (L2 cache) in an I-frame format.** Two-stage: (a) cheap — wire `video::ClipFrameCache` `.dxcfz` as an L2 spill of `EffectFrameCache` (evict → disk write-through on autocache renders, read-through on miss); (b) real codec — store per-frame compressed BGRA (zstd) or ProRes/DNx via FFmpeg bridge at canvas res, keyed by state-hash hex; invalidate-by-key like RAM tier. Cap via existing persistent-storage TTL. Enables cross-session cache + preview-file playback + a future smart-render export. | `effect_frame_cache.{h,cc}` (two-tier), `src/video/clip_frame_cache.*`, `playback/auto_cacher.cc`, `compositor.cc` | Premiere preview files / Resolve cache-format parity; 512 MiB→GB-scale runway; prerequisite for T8/T9. |
| T8 | **P2** | M | **"Render Work Area" command + green-bar play.** Menu/keyboard command renders the work area through the autocache pipeline (or a dedicated job) until coverage = 100%; Enter parity; bar (T2) shows progress. | `playback/auto_cacher.{h,cc}` (`RenderRange`), `ui2/ve_app.cc`, `ui2/timeline_surface.cc` | The explicit-render half of the Premiere model; trivial once T5's range API exists. |
| T9 | **P3** | L | **MFR-style segmented export.** ExportEngine spawns K workers; each owns a Compositor (+ its decoders) bound to a disjoint frame subrange [n₀,n₁) — session contract per worker is preserved because each session only walks forward; encoder consumes frames in order via a small reorder buffer; audio stays single-mixer (chunks are index-addressed). Cap K by core count & RAM (AE's CPU%-reserved slider analog). Optional `use_previews` flag consumes T7's disk tier when keys match. | `export_engine.{h,cc}`, `compositor.{h,cc}` (Init is already snapshot-cheap) | MFR's headline win maps 1:1; the AutoCacher already proves multi-compositor coexistence. Biggest single throughput lever on export. |
| T10 | **P3** | M | **Cache-coverage HUD + purge controls.** Expose cache stats/coverage to the perf HUD; "Purge cache" menu item (`EffectFrameCache::Clear` + L2 sweep) — AE Purge parity; "cache work area in background" toggle = autocache on/off per project. | `effect_frame_cache.h`, `ui2/monitor_panel.cc`/app menu, `playback/auto_cacher` | Cheap observability; AE's manual escape hatch for pathological memory states. |

---

## 7. Sources

**Premiere Pro**
- Render bars meaning/triggers: https://blog.adobe.com/en/publish/2011/02/20/red-yellow-and-green-render-bars ; https://creativecow.net/what-adobe-premiere-pro-is-trying-to-tell-you-about-performance-2/
- MPE GPU renderer + list: https://helpx.adobe.com/premiere/desktop/get-started/technical-requirements/enable-mercury-playback-engine-gpu-accelerated-renderer.html ; https://helpx.adobe.com/premiere/desktop/add-video-effects/types-of-effects/list-of-accelerated-effects.html ; NVIDIA MPE whitepaper https://www.nvidia.com/docs/IO/85230/CUDA_MPE_WP-05380-001_v01.pdf ; per-chain CPU fallback https://community.adobe.com/t5/premiere-pro-discussions/pre-render-is-handled-by-cpu/m-p/14745747 ; SDK fallback-at-instance https://deepwiki.com/docsforadobe/premiere-plugin-guide/6.1-gpu-programming-model
- Preview codec: https://community.adobe.com/questions-734/discuss-use-prores-as-the-default-codec-for-sequence-rendering-313242 ; https://community.adobe.com/questions-729/faq-how-to-change-the-codec-for-video-preview-files-1393003
- Resolutions/HQP/dropped-frames: https://helpx.adobe.com/nl/premiere/desktop/get-started/source-and-program-monitor-adjustments/set-display-quality-for-the-source-and-program-monitors.html ; https://helpx.adobe.com/premiere/desktop/get-started/source-and-program-monitor-adjustments/enable-high-quality-playback.html ; https://helpx.adobe.com/premiere/desktop/get-started/source-and-program-monitor-adjustments/enable-dropped-frame-indicator.html

**After Effects**
- Caches/purge: https://helpx.adobe.com/in/after-effects/using/memory-storage1.html ; https://helpx.adobe.com/after-effects/desktop/memory-storage-performance/memory-and-storage/memory-storage1.html ; global/persistent cache https://www.provideocoalition.com/after-effects-cs6-preview/ ; https://www.manualsdir.com/manuals/753844/adobe-after-effects.html?page=664
- MFR: https://helpx.adobe.com/after-effects/desktop/render-and-export/multi-frame-rendering/multi-frame-rendering.html ; https://blog.adobe.com/en/publish/2021/03/10/multi-frame-rendering-now-in-after-effects-beta ; https://www.provideocoalition.com/best-hardware-for-multi-frame-rendering-in-after-effects/ ; speculative preview https://creativecow.net/a-faster-after-effects-is-now-available-for-testing-new-multi-frame-rendering-features-in-public-beta/
- Preview degradation: https://domer.io/blog/after-effects-preview-settings-workflow-speed ; https://bluefx.net/tutorials/set-up-after-effects-to-render-fast/ ; https://pixelnpixel.com/after-effects-performance-optimization/
- Frame blending: https://helpx.adobe.com/id_id/after-effects/using/time-effects.html ; https://helpx.adobe.com/mena_ar/after-effects/desktop/animate-in-after-effects/time-stretching-and-time-remapping/time-stretching-time-remapping.html ; https://flylib.com/books/en/2.104.1/manipulating_time_itself.html

**DaVinci Resolve**
- Cache levels + smart/user: https://ltbits.github.io/davinci-resolve-manuals/DR12/DR12-RM-05.pdf ; https://docs.imaster.org/davinci-resolve-21-manual/part-2-setup-and-workflows/ch.-8-improving-performance,-proxies,-and-the-render-cache ; https://vfxstudy.com/tutorials/performance-cache-levels/
- Background caching + formats: https://docs.imaster.org/davinci-resolve-21-manual/part-2-setup-and-workflows/ch.-6-project-settings ; https://forum.blackmagicdesign.com/viewtopic.php?f=21&t=203618 ; https://filmmakingelements.com/davinci-resolve-best-render-cache-settings/ ; https://creativevideotips.com/tutorials/davinci-resolve-render-cache-essentials
