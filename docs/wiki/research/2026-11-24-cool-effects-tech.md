# Cool effects & tech wave — research (signature looks, transitions, GPU, plugin ABIs, ACES, audio)

Date: 2026-11-24. Third-party-look wave for the creative suite, scoped to
`tools/video_editor` + the `Photo` workspace. Repo claims verified in the
working tree 2026-11-24 (paths + line numbers below); external claims carry
URLs. Deliberately NON-AI: waves already adopted RIFE / realesr-x4v3 /
depth / VAD / beats / SAM2 (`2026-11-23-ai-features-wave2.md`,
`2026-11-23-hf-org-scan.md`, `2026-11-22-ai-model-stack.md` — which also
pre-adopted fast-neural-style ONNX "nearly free", see §2.9). OFX /
frei0r / libplacebo were first scored in
`2026-11-21-nle-architectures.md`; this wave goes deeper on *what we
would ship*, not re-survey engines.

## 0. What exists today (verified 2026-11-24)

Effect surfaces — five parallel registries, all CPU, all BGRA8:

| Surface | Where | Contents |
|---|---|---|
| Builtin plugin descriptors | `builtin_fx.cc:1204-1400` | ~50 `PluginDesc`s: 43 video `xer.fx.*` (8 adjust, 6 blur, 8 color, 5 distort, 6 overlay, 7 style, 3 no-op track stubs) + 7 audio `xer.af.*` (incl. GTCRN `voice_isolate`). Every video fn is a per-frame pixel loop or a `Skia::` bridge call (`builtin_fx.cc:95` `ApplySkia`). |
| Legacy clip-effect strings | `compositor.cc:727` `KnownEffectTypes()` | 23 types incl. `chromatic_aberration`, `oil_paint`, `lens_sphere`, `magnifier`, `ribbed_glass`, `bokeh_dof`, `bokeh_blur` — a second, older registry that does NOT appear in `xer.fx.*` picker lists. |
| JS fx sandbox | `js_fx.h`, `assets/scripts/fx/fx.json` | V8 runtime, **zero-copy** `Uint8ClampedArray` over the frame, watchdog `TerminateExecution`, `ctx.clip_id` scoping, `xerFxTransition` easing hooks. Catalog today: 5 video (sepia/invert/rgb_shift/film_grain/scanlines) + audio + transitions. |
| Transitions | `timeline_model.h:249-255` | 11 types (crossfade, 4 wipe, 4 slide, dip-black/white, additive) + `Script` = JS-eased crossfade only (`compositor.cc:2585-2601`). |
| Photo adjustments | `photo/adjust_ops.cc` `Ops()` | 15 ops (brightness/hsl/exposure/levels/curves-gamma/color-balance/vibrance/channel-mixer/opacity/selective/invert/gray/sepia/threshold/posterize). |

Hidden inventory already paid for but NOT surfaced:

- **Skia bridge ops with no `xer.fx` descriptor**: `FilterConvolve` (arbitrary
  kernel!), `FilterAddNoise` (gaussian option unused — `film_grain` passes
  `gaussian=false`), `FilterOilPaint`, `FilterPixelate`,
  `FilterChromaticAberration`, `RasterGlow` (inner/outer), 3 gradient
  fills, `RasterFloodFill`, `RasterDropShadow`
  (`src/wrapper/skia/skia_engine_bridge.h:306-420`). Several exist only
  via the legacy string table. Cheap catalog growth.
- **Audio effects_rack = 15+ DSP classes** already built:
  compressor/expander/gate/limiter, graphic+parametric EQ,
  chorus/flanger/phaser/extra, auto-tune-correct, **algorithmic AND
  convolution reverb**, bitcrush, tube (`src/audio/effects_rack/fx_*`).
  Only 6 made it into `xer.af.*` — the other ~9 are dead code from the
  editor's point of view.
- **VST3 hosting already ships** — `vst/vst3_chain.cc` is the
  *production* per-clip audio plugin chain wired into
  `audio_mixer.cc:22-23`. Third-party audio FX is a solved problem;
  video has no equivalent host.
- **OCIO 2.6 staged and wired**: `color/ocio_transform.cc` does
  DisplayViewTransform CPU application; `ocio:<name>` is already a clip
  effect (`compositor.cc:1793`); `lut_manager.cc` scans
  `.cube/.3dl/.look` from 4 search dirs; `color.yaml` Profiles support
  `Source:`/`Display:`/`View:` — header comment literally shows
  `Source: "ACEScc"` (`ocio_transform.h:57`). Staged:
  `libOpenColorIO_2_6.dll`, `YA.VideoEditor-OCIO.dll`. What ACES lacks
  is only a bundled ACES config + input transforms (§2.7).
- **GPU bits staged, none used for pixels**: `vulkan-1.dll`,
  `libshaderc_shared.dll`, `ggml-vulkan.dll`, `libonnx.dll`,
  `onnxruntime.dll`, `libskia.dll` in `out/build/.../bin`.
  `src/gpu_video/` has working CUDA decode + nvenc; `src/frame_render/`
  detects the Vulkan loader (progress/2026-11-21-gpu-ai-modules.md).
  All *pixel* work is single-threaded CPU (compositor loops,
  `thread_local` scratch only).
- **libavfilter is a linked dep** — `cmake/FindFFMPEG.cmake:11-19`
  components include `libavfilter`. xfade/tblend/zoompan etc. are one
  AVFilterGraph away.
- **Full node-graph stack unused by the VE**:
  `src/node_graph/` (Graph, Executor, history, JSON store, validation)
  + `src/ui/widgets/node_graph_editor_widget.{h,cc}` — pan/zoom, typed
  ports, minimap, subgraph collapse, palette — consumed only by
  `src/app`.
- `src/image/effects/vfx/displacement_map.h` — a real displacement-map
  engine exists in-tree, unused by builtin_fx.
- `bitmap_font.cc` — bitmap glyph atlas available for ASCII-art fx.

Current gaps vs. the brief: no halation/bloom/temporal-grain-variants/
gate-weave/light-leaks/print-LUTs; no lens distortion/fisheye/polar/
kaleidoscope/displacement *surfaced*; no dither/ASCII/Kuwahara/Voronoi/
crosshatch/riso; no anamorphic streak/Orton/god-rays; no spin-blur/
tilt-shift/shaped-bokeh/channel-blur; no datamosh/pixel-sort/CRT/interference;
no temporal echo/slit-scan/time-displacement for video; no Perlin/paper/
dust overlays. Transitions: 11 vs. competitors' ~60+.

## 1. Effect candidates (by family)

Effort S ≈ ≤1 day (pixel loop or bridge call + descriptor + params),
M ≈ days (multi-pass, scratch buffers, or new infra touch),
L ≈ week+ (new subsystem/ABI). Wow 1–5 = demo value to a non-technical
viewer. Every video item drops into the `Desc()`/`PluginDesc` pattern at
`builtin_fx.cc:1188` unless noted; multi-input/temporal items are marked
**compositor** (like `smart_reframe`/`track_mask` no-op descriptors).

### 1.1 Film emulation

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Halation** | Warm red-orange halo bleeding around highlights — the signature "film" glow Resolve/Dehancer/FilmConvert sell | Highlight extract (luma > thresh, soft knee) → heavy blur (reuse `FilterGaussianBlur` on a half-res scratch) → tint toward amber (channel mixer or multiply `~[1.0,0.45,0.15]`) → `screen`/`additive` blend back via `MixSystem::BlendFrames` (Additive exists). 3 passes + one downscale. | S–M | **5** |
| **Bloom / soft glow** | Neutral white glow on brights (dreamy 80s/Pro-Mist) | Same pipeline minus the tint; radius+threshold+strength params. Ships as sibling to halation sharing the scratch path. | S | 4 |
| **Proper film grain v2** | Resolving, chroma-aware grain that doesn't look like TV static | Current `film_grain` = `FilterAddNoise` uniform, boiling per frame, flat over luma. Upgrade: synthesize a larger-than-frame grain field once per N frames (or tile a precomputed patch), scale-aware amplitude `a*(1-luma)^0.5` (shadow-weighted like real film), optional mono/chroma split, per-channel response. Time-seeded RNG (params get `clip_time_s` — same seam JS fx uses). | S–M | 4 |
| **Gate weave** | Sub-pixel frame jitter/drift of telecined film | Per-frame `ImageCompositeWarp`/bilinear shift on a slow random-walk (amplitude ~0.5px, Hz ~0.2-2). Needs a translated resample — compositor warp stage or a whole-frame remap kernel like `twirl`. | S | 3 |
| **Film burn / light leak** | Amber/red flares creeping from frame edges, projector-burn frames | Procedural: 2-3 animated radial-gradient blobs (`RasterRadialGradient` exists) w/ screen blend + noise-modulated opacity over time; or ship 4-6 CC0 overlay loops blended `screen`. Generator variant is S; asset variant needs license-clean sources. | S–M | 3 |
| **Print stock / film LUTs** | Kodak 2383/2393, Fuji 3510 print-emulation looks | `lut_manager` already scans `.cube` from project/assets/user dirs and `ocio:<name>` applies them. Ship a `luts/film/` folder. Two legal routes: (a) generate stocks from published print-film curves via a small builder script (no license issues), (b) bundle permissively-licensed community LUTs (verify per-file — most commercial LUT packs are NOT redistributable). | S | 4 |
| **Subtractive color** | Print-film CMY response — colors deepen instead of clip | Per-pixel `out = 1 - (1-in)*gain` in CMY space ≈ per-channel multiply + offset; expressible via `AdjustChannelMixer` diagonal or a 12-line kernel. | S | 3 |
| **Split-tone printer lights** | Resolve "printer points" / RGB offset grading | Already ~covered by `channel_mixer`/color_balance; expose as a named preset. | S | 2 |

### 1.2 Distortion / warp

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Lens distortion (barrel/pincushion)** | Fisheye bulge or stretch-lens pinch, k1/k2 params | Inverse-map kernel `r' = r(1+k1 r²+k2 r⁴)` — same structure as `twirl`/`wave_h` ported kernels (`builtin_fx.cc:470+`). Bilinear sample + edge clamp. | S | 3 |
| **Fisheye / panini** | 180° dome or Vedutismo-preserved wide pano | Fisheye: `theta=atan(r)` remap. Panini (cylindrical-perspective hybrid, keeps verticals straight) — published math (Sharpless et al., "Pannini: a new projection", J. Graphics GPU 2010, http://tksharpless.net/vedutismo/Pannini/panini.pdf). | M | 3 |
| **Displacement map** | Second image/noise field warps the frame (heat-haze, liquid) | `src/image/effects/vfx/displacement_map.h` exists — wrap it: params = generated field type (perlin/turbulence) + scale + strength; external-map input is a **compositor**-level (2nd clip) variant later. | S–M | 4 |
| **Polar coordinates** | Rect↔polar swirl, "tiny planet" from panoramas | Inverse map `out(x,y)=src(r·W, θ·H)` — 20 lines, bilinear. | S | 3 |
| **Kaleidoscope** | N-fold mirrored wedge symmetry | Wedge-fold angle + rotate + sample — remap kernel, params (segments, angle offset, cx/cy). | S | 3 |
| **Chromatic aberration v2 (radial)** | Color fringing growing toward frame edges (real lens behavior) | Bridge `FilterChromaticAberration` only does *fixed* xy offsets. Radial version: per-pixel offset = `k·dir(p-c)` — sample R at `p+k·d`, B at `p-k·d`. | S | 3 |
| **Perspective/corner-pin** | Quad-to-quad warp, screen-replacement staple | Bilinear/projective inverse map with 4 corner params; inverse via `MatrixInvert` (bridge has 3x3). | M | 3 |

### 1.3 Stylize

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Ordered dither (Bayer)** | Retro 8×8 matrix dithering, lo-fi/game aesthetic | `out = posterize(in + (Bayer8[x%8][y%8]-0.5)*step)` — 8×8 constant table + per-pixel lookup; combine with existing posterize for palette size. | S | 3 |
| **ASCII / mosaic** | Frame rendered as terminal glyphs | We own `bitmap_font.cc`: tile frame → pick glyph by cell luminance → blit glyph tinted by cell color. Params: cell size, charset, colorize. | M | 4 |
| **Kuwahara / anisotropic Kuwahara** | Painterly flat regions with preserved edges — far better than `oil_paint` | Kuwahara: 4 quadrant min-variance select (classic 1976 filter). Anisotropic variant follows structure-tensor orientation (Kyprianidis et al., "Image and video abstraction by anisotropic Kuwahara filtering", Pacific Graphics 2009 — commonly implemented in GIMP/GEGL `newsprint`/`kuwahara`; GEGL source = reference). | M | 4 |
| **Stained glass / Voronoi** | Cells of flat color with dark grout borders | Jittered-grid Voronoi on a coarse lattice: assign cell-average color, darken by distance-to-border. One pass + neighbor check. | M | 3 |
| **Crosshatch / engraving** | Pen-and-ink hatching, density ∝ darkness | 4-6 luminance bands each filling a different hatch direction; hatch = rotated line pattern via `(x*cos+y*sin)*freq mod period < width`. Single pass, LUT over luma. | S–M | 3 |
| **Risograph** | 2-3 spot-color print look: misregistration + dither + paper | Composite recipe: duotone (exists) + ordered dither + per-channel offset (radial CA kernel reused) + paper texture multiply + slight noise. A *preset chain* — could ship as one fx that calls 4 kernels, or as a JS-fx composition. | M | 4 |
| **Posterize++ (palette)** | Map to a named N-color palette w/ dither | Posterize exists; palette LUT (per-channel or RGB nearest) + optional Bayer pre-dither. | S | 3 |
| **Neon/rim upgrade** | Current `neon_edges`/`edge_glow` are monochrome-ish | Add color param + threshold + blur-radius → colored glow edges. Trivial param additions on existing kernels. | S | 3 |

### 1.4 Glow / emit

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Anamorphic streak** | Horizontal lens flare lines through bright sources (JJ Abrams look) | Highlight-extract → *horizontal-only* long blur (separable 1D gaussian/box, anamorphic ratio param) → optional blue-tint → additive. Cheaper than it looks: 1D blur of radius 64 on a half-res buffer. | S–M | **5** |
| **God rays / crepuscular** | Light shafts from a sun point (or off-frame) | Highlight-extract → iterative radial samples toward light point (existing `zoom_blur` math w/ cx/cy already ported at `builtin_fx.cc:212`) → weighted additive, ~60-100 taps or half-res few-iteration. | M | 4 |
| **Orton glow** | Soft dreamy landscape glow (blur screened onto itself) | `FilterGaussianBlur` + screen blend — two existing ops glued; params radius/strength. | S | 3 |
| **Bloom on alpha edges** | Glow emitted from mask/subject edges | Edge-glow kernel already computes edges; add outward-only blur for rim/aura. | S | 3 |

### 1.5 Blur family

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Spin blur** | Rotary smear around a center (wheels, vortex) | Tangential variant of `radial_blur` (`builtin_fx.cc:212`) — sample along arc instead of ray; ~15 lines of delta on the ported kernel. | S | 3 |
| **Tilt-shift** | Miniature-model look: sharp band, blurred top/bottom | Vertical linear mask → two blur levels (or one blur + lerp by mask). Reuses `fast_gaussian` + a mask lerp. | S | 3 |
| **Shaped bokeh (hexagon/cat-eye)** | DOF discs with aperture-polygon or swirly edges | Current `bokeh_dof`/`bokeh_blur` (compositor ~2046) is flat disc. Shaped = scatter/gather conv with a polygonal kernel bitmap, cat-eye = vignetted kernel. Kernel from a small generated bitmap — moderate. | M | 3 |
| **Channel blur** | Only chroma (or one channel) softens — cheap "film softness" | Blur B/G planes only via bridge on a repacked channel, or split-blur-merge. | S | 2 |
| **Deflicker** | Removes time-lapse flicker (exposure jitter between frames) | Temporal: per-pixel or global frame mean equalization vs. history — **compositor** (needs history buffer; see §2 echo infra). FFmpeg has `deflicker` (§2.3). | M | 3 |

### 1.6 Glitch / data

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Pixel sorting** | Iconic glitch: runs of pixels sorted by luminance along rows/cols (Æsthetic of Kim Asendorf) | Threshold rows into runs → sort each run by luma. Reference: ASDFPixelSort (Processing, https://github.com/kimasendorf/ASDFPixelSort — MIT). CPU-per-row sort is fine at preview res. Params: threshold, direction, span. | M | 4 |
| **Datamosh (approx)** | P-frame smear/in-between melt when I-frames drop | True moshing needs bitstream surgery (drop P→I refs) — **L and fragile**. Honest approx: temporal frame-hold + motion-vector-free smear (hold N frames then release w/ warped blend); or use RIFE flow (adopted wave-2) to advect pixels — **compositor**, M. | M–L | 4 |
| **Channel shift / RGB split** | R/G/B planes offset spatially or temporally | JS `rgb_shift` already demos the spatial variant on CPU; native port = 3 offset samples/px (S). Temporal split needs history (M). | S | 3 |
| **CRT / scanline+grille** | Full phosphor-tube look: barrel curvature + aperture grille + bloom + interlace shimmer | Composite: scanlines (exists) + subtle barrel warp (lens kernel) + RGB grille mask (periodic 3px pattern) + light bloom + optional interlace row offset per frame. | M | 4 |
| **Signal interference / VHS** | Horizontal band tearing, chroma errors, sync wobble | Row-offset noise (per-row random x-shift bands) + chroma smear (blur B/R channels horizontally) + periodic sync-jitter line + noise. All S-sized kernels; the "sell" is parameter choreography. | S–M | 3 |
| **JPEG/block corruption** | Macroblock mosaic + chroma subsample artifacts | Quantize 8×8 DCT approx ≈ downsample to 1/8 + back, optional per-block offset corruption. | S | 2 |

### 1.7 Time (all **compositor**-class — need frame history)

The compositor is strictly per-frame today; a small ring buffer
(like `ClipFrameCache` but effect-local) unlocks this family. FFmpeg
`tblend`/`framestep`/`lagfun` give 80% of trails if we bridge lavfi (§2.3).

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Echo / trails** | Ghost copies fading behind motion (sport replays, psychedelic) | Ring buffer of last K frames → blend w/ decay (screen or lerp). `tblend=all_mode=screen` equivalent exists in lavfi. | M | 4 |
| **Slit-scan** | Each output row taken from a different time (classic "2001" stargate) | Per-row index into a deep history buffer (`row → t` map param). Needs K=height frames of history — memory-bounded at preview res (~1920×2MB). | M–L | 4 |
| **Time displacement** | Per-pixel temporal offset via a map (ripples propagate backward in time) | History + per-pixel index map (procedural wave field or luma of source) — slit-scan generalized. | M | 3 |
| **Frame hold / strobe** | Posterize-time (N fps look), smear release | Hold output for K frames — trivial once a "last output" slot exists; `framestep`/tblend equivalent. | S | 2 |

### 1.8 Texture / overlay

| Effect | What it looks like | Implementation sketch | Effort | Wow |
|---|---|---|---|---|
| **Perlin/turbulence overlay** | Animated fog/cloud noise composited over footage | Value-noise octaves synthesized to a tileable scratch → blend mode (screen/overlay — all 29 Skia modes available via `BlendModeNames`). Time-seeded scrolling. | S | 3 |
| **Paper / canvas texture** | Physical media grain under/over image | Ship 3-5 **CC0** textures (e.g. textures.com CC0? — verify per-asset; safer: photograph/generate our own) → multiply/overlay via `ImageComposite` + named blend mode. | S | 2 |
| **Dust & scratches** | Projector dirt, hair in the gate, vertical scratches | Procedural: seeded specks (poisson points, small alpha blobs) + 1-3 vertical scratch lines w/ per-frame x-jitter + screen/overlay blend. More convincing than a looping asset. | S | 3 |
| **Anamorphic flare overlays** | Cinematic blue streaks/ovoid ghosts | Procedural streak sprite (like §1.4 anamorphic but drawn, not extracted) or CC0 flare assets, additive at tracked/untracked position. | S | 3 |
| **Cinematic bars** | Animated letterbox (bars slide in), per-ratio presets | `letterbox` exists but is static; add animated ease + ratio presets (2.39:1 etc.). | S | 2 |

## 2. Technologies to leverage

### 2.1 GPU compute — current state is 100% CPU

Verified: every pixel op above runs single-threaded over
`std::vector<uint32_t>` (bridge) or strided BGRA (compositor).
`thread_local` scratch exists (`compositor.cc:1851`, `video_effects.cc`)
but no `std::async`/pool anywhere in the pixel path. Staged-but-unused:
`vulkan-1.dll`, `libshaderc_shared.dll`, `ggml-vulkan.dll`, `libskia.dll`
(raster-only usage — no `GrDirectContext`/ganesh/graphite references
anywhere in `src/` — verified grep), OpenCL headers shim
(`cmake/deps/configs/opencl/`), and `pthreadpool`/`fxdiv` are already
vendored deps (AGENTS.md dep list).

Cheapest→biggest ladder:

| Option | What it is | Effort | Realistic speedup | Verdict |
|---|---|---|---|---|
| **Row-parallel CPU (do first)** | `parallel_for` over rows: partition each kernel's y-loop across the vendored `pthreadpool` (or `std::async` chunks). Zero new deps, zero new risk; keeps single code path | **S–M** | 4–10× on a modern 8-16-core for blur/warp/convolve | **ADOPT FIRST** — every other option multiplies this |
| Vulkan compute via staged shaderc | GLSL → SPIR-V (shaderc staged), upload BGRA→SSBO/image, dispatch, readback. `src/frame_render` already probes the Vulkan loader | M–L | 20–100× on heavy ops; readback cost (~1-3ms/1080p) means batching chains matters | Strategic — right answer long-term, needs a `GpuFrame` path |
| OpenCV OpenCL (`cv::UMat`) | OpenCV already a dep w/ opencl shim; ggml sees `OpenCL 3.0 CUDA` on the dev RTX 3070 | M | Similar class to Vulkan but only for cv ops | Skip for fx (bridge isn't cv-based); keep for cv::dnn |
| Skia GPU (ganesh/graphite) | libskia.dll is staged but raster-only; enabling GPU surfaces = build-flag + context work | M–L | Real but bridges' API is buffer-based — repack cost stays | Revisit when GPU frame exists |
| libplacebo | Whole GPU render pipeline (below, §2.10) | M–L | Best-in-class incl. tonemap | Strategic, pairs w/ Vulkan |

### 2.2 FFmpeg filter graph (libavfilter) — linked already, unused for fx

`FindFFMPEG.cmake:18` components include `libavfilter`; nothing in
`tools/` builds an `AVFilterGraph` for effects (verified grep — only
decode/encode paths). A generic seam = wrap `AVFilterGraph` around our
BGRA8 frames: `buffersrc(format=bgra)` → named filter → `buffersink` →
back to `video::Frame`. One-time graph init per effect instance (param
updates via `avfilter_process_command` where supported — most filters
accept runtime commands).

Instant wins by filter (ffmpeg.org/ffmpeg-filters.html):

- **`xfade`** — §2.4 below; needs two inputs (compositor seam, not
  clip-fx seam).
- **`zoompan`** — Ken Burns pan/zoom on stills — a whole feature for free.
- **`vignette`, `hue`, `eq`, `colorbalance`, `curves`, `noise`,
  `gblur`, `boxblur`, `median`, `unsharp`, `edgedetect`, `colorchannelmixer`**
  — mostly duplicates of ours; only useful where ours lacks params.
- **`noise=alls=..:allf=t+u`** — temporal+uniform noise ≈ instant film
  grain upgrade.
- **`tblend`** + `framestep`/`tmix` — temporal frame blending = the
  echo/trails family without building history ourselves (§1.7).
- **`xmedian`** — temporal median (dust-removal / clean-plate).
- **`deflicker`**, **`deshake`/`vidstab`** (vidstab needs optional lib),
  **`drawbox`/`drawgrid`**, **`gradients`** (procedural generator!),
  **`life`/`cellauto`** (fun generators), **`lagfun`**, **`lumakey`,
  `chromakey`, `colorkey`** (keyer upgrade), **`floodfill`**,
  **`rotate`, `perspective`**, **`lensfun`** (real lens database — needs
  optional lensfun dep in our FFmpeg build — verify).
- **Custom expr filters**: `xfade=transition=custom:expr=...` and
  `geq`/`lut`/`lut3d` — user-scriptable pixel math as an escape hatch.

Caveats: CPU-only (same ceiling as ours), per-filter BGRA↔planar-YUV
conversions can negate gains (keep `format=bgra` chains), graph init
cost per clip, error surface. **Seam effort M; then ~free.**

### 2.3 xfade — ~58 transitions, near-free

FFmpeg 8.1's `xfade` enumerates 58 named transitions + `custom` expr
mode (verified enum: ffmpeg.org/doxygen vf_xfade_vulkan.c — FADE, all
WIPE/SLIDE dirs, CIRCLECROP, RECTCROP, DISTANCE, FADEBLACK/WHITE,
RADIAL, SMOOTH×4, CIRCLE/VERT/HORZ OPEN/CLOSE, DISSOLVE, PIXELIZE,
DIAG×4, ×SLICE×4, HBLUR, FADEGRAYS, WIPETL/TR/BL/BR, SQUEEZEH/V, ZOOMIN,
FADEFAST/SLOW, ×WIND×4, COVER×4, REVEAL×4; see also
ffmpeg-filters.html §xfade). vs. our 11 today.

Two adoption routes:
- **(a) lavfi route**: the transition renderer (`compositor.cc:2700+`)
  already materializes `transition_scratch` with both sources — feeding
  an `xfade` graph per transition instance is mechanical. Gets all 58
  + `custom` + future ones.
- **(b) native port**: xfade transition bodies are ~5-15 lines of
  per-pixel C each (vf_xfade.c is public). Porting the top ~20 (dissolve,
  pixelize, slices, winds, covers/reveals, smooths, radial, circleopen)
  = ~1-2 days, no FFmpeg interop, keeps our blend/premult conventions.

License: FFmpeg LGPL-2.1+ in our build config — fine either way.
**S–M. Wow 5 — transitions are the most visible category gap.**

### 2.4 GL-transitions — ~100 MIT shader transitions

github.com/gl-transitions/gl-transitions — **MIT** (LICENSE verified;
per-file header notes may carry own terms — check the handful we port).
Collection ≈ 100 single-function GLSL transitions (directional warp,
glitch, burns, dreamyzoom, morph…). Most are short warp/dissolve math
portable to a CPU per-pixel kernel in an afternoon each — port the top
15-25 (e.g. `glitch-displace`, `directionalwarp`, `burn`, `dreamyzoom`,
`crosszoom`, `flyeye`, `perlin`). When/if a GPU path lands (§2.1), the
same GLSL bodies run unmodified — this library is *the* seed content
for a shader transition system. **M total. Wow 4.**

### 2.5 OpenFX (OFX) hosting — the strategic lever

- ASWF spec + headers: BSD-3 (github.com/AcademySoftwareFoundation/openfx
  — verified). `HostSupport/` C++ host library + `Examples/` ship in the
  same repo. Docs: openfx.readthedocs.io.
- Hosts: Resolve/Fusion, Nuke, Natron, Vegas, Flame — an OFX host
  inherits the entire pro-plugin ecosystem (commercial suites AND free
  packs like openfx-misc, INK, openfx-arena — note several free packs are
  GPLv2+; *hosting* a user-installed GPL plugin is the same arrangement
  Kdenlive/VST users already have, but don't bundle them).
- Fit: prior doc already ruled "OFX host at Motion phase"
  (`2026-11-21-nle-architectures.md:223`). Our ABI today is
  1-frame-in/1-frame-out positional params — OFX's
  `getFramesNeeded`/clip props/mask clips exceed it; the mapping lands
  on the compositor's `PluginDesc` + a temporal-capable v2 ABI.
- **L effort, massive leverage.** Ranked in top-10 as the Motion-phase
  flagship, not a Cut-phase item.

### 2.6 frei0r — cheap spec, GPL plugins

Single-header API (`frei0r.h`), ~100+ plugins used by Kdenlive/Shotcut/
MLT. Plugin collection is **GPL-2.0+** (dyne/frei0r LICENSE + Fedora/Arch
metadata — verified). Hosting = a day of dlopen wrapper; but shipping
the plugins taints distribution for a proprietary suite — acceptable only
as an optional-user-install host, same gray zone as prior wave noted
("host now (license check)" — now checked: plugins GPL, header itself
fine). Given xfade+GL-transitions cover the same space cleanly,
**frei0r host = optional/low priority.**

### 2.7 ACES / OpenColorIO — staged and wired, missing the config

Already true (verified): OCIO 2.6 DLL staged; per-frame
`ApplyDisplayTransform`; `ocio:<name>` clip effects; `Profiles:` in
color.yaml support arbitrary `Source→Display/View` (header demo uses
`Source: "ACEScc"`); LUT manager scans `.cube/.3dl/.look`.

What "ACES color management" still needs:

- **A bundled OCIO config.** The AcademySoftwareFoundation/OpenColorIO-Configs
  repo ships reference configs (aces_1.2/1.3 cg-config and studio-config;
  the CG config is small — the studio config carries ~200 LUTs).
  Configs are Apache-2.0/CC per repo LICENSEs (verify at vendor time).
  Slim route: ship cg-config + the LUTs for Rec.709/sRGB/P3/ACEScct only.
- **Input transforms per clip**: camera-log → ACEScct/ACEScg (Sony S-Log3,
  Canon C-Log, DJI D-Log, ARRI LogC are all in the config as named
  colorspaces — becomes a clip property dropdown, same `ocio:` seam).
- **Working-space float**: our pipeline is BGRA8 — ACES really wants
  f32. First pass: apply IDT+ODT around the 8-bit stack (visible win:
  log footage looks right; HDR->SDR tonemap via OCIO view). Float pipeline
  is the L follow-up.
- Effort **S–M** for a credible "ACES managed" badge; **flagship wow 5** —
  nothing else on this list markets itself as loudly to pros.

### 2.8 JS fx sandbox → "Shadertoy mode"

`js_fx.h` already gives zero-copy BGRA mutation under a watchdog — it IS
the Shadertoy sandbox, just with JS instead of GLSL and no editor UI.
Cheap expansions: (a) ship 10-15 more scripts covering the §1 list
(dither, pixel-sort, swirl, god-rays all expressible today — film_grain.js
proves the pattern), (b) a live-script panel that reloads `*.js` on save
(the catalog reload is already `LoadCatalog`-idempotent). Shadertoy GLSL
can't be bulk-imported (per-shader licenses vary wildly), but the *format*
is proven. **S. Wow 3** (dev-darling feature, demos well).

### 2.9 Fusion nodes vs AE layer-fx — both, in different rooms

- DaVinci Fusion = full node graph; AE = layer stack with per-layer fx
  chains. Our timeline = AE model already (per-clip ordered `effects`).
- The node stack EXISTS unused: `src/node_graph/` (typed ports,
  Executor, validation, JSON) + `NodeGraphEditorWidget` (pan/zoom,
  minimap, palette, subgraphs). Prior suite-architecture already assigns
  a "Motion" workspace — node-compositing is its native home.
- Recommendation: keep AE-style fx chains on the Cut timeline (right UX
  for editing); wire `node_graph_executor` to a frame op-registry for
  Motion — each node ≈ one `PluginDesc` video fn + params. The executor's
  dirty-prop + per-node cache maps cleanly onto `effect_frame_cache.cc`.
- **M–L. Wow 5** as a workspace; not needed to ship the §1 effects.

### 2.10 libplacebo — GPU pipeline + HDR + user shaders

- LGPL-2.1 (github.com/haasn/libplacebo — verified; prior doc VENDOR'd it,
  MSYS2 `libplacebo 7.387` exists for the clang64 toolchain).
- Gives: Vulkan+D3D11 frame rendering, best-in-class scaling
  (ewa_lanczos/jinc), HDR tone-mapping algorithms, **mpv-style
  user-shader hooks** — i.e. a real Shadertoy/GLSL fx sandbox on GPU,
  which would also run GL-transitions natively (§2.4).
- Fits the §2.1 "strategic" slot: if we adopt one GPU lib, placebo is
  the most leverage per effort (display + tonemap + fx pipeline in one).
  **M–L. Wow 4** (enabler + HDR story).

### 2.11 Audio fx — the rack is the goldmine

| Item | Status | Notes |
|---|---|---|
| Convolution reverb (IR) | **In-tree**: `fx_reverb_convolution.cc` | Wire to `xer.af.reverb_ir` + ship 5-10 IRs. IR sources: record/generate our own (sine-sweep deconvolution is ~50 lines), or OpenAIR/CC-BY libraries (attribution-ok). **S, wow 4** — "real space" reverb is a flagship audio feature. |
| Rack exposure | compressor/limiter/gate/expander, graphic+parametric EQ, chorus/flanger/phaser, autotune, algo-reverb, tube | All instantiate `XER_PLUGIN_AUDIO_FX` descriptors like `soft_clip` does (`builtin_fx.cc:1148`). **S for the batch — ~9 new audio fx ≈ free.** |
| Multiband compressor | Not in rack | M new DSP (crossover + N compressors); or defer to VST3. |
| De-esser | Not in rack | M (sidechain EQ → the existing compressor). |
| Spectral denoise | **Shipped** — GTCRN `xer.af.voice_isolate` | Done already. |
| Transient shaper | Not in rack | M (envelope-followers, attack/sustain split). |
| Pitch shift / time-stretch | Not in rack; licenses matter | **SoundTouch = LGPL-2.1** (soundtouch.surina.net/license.html, vcpkg `soundtouch` port exists) — adoptable, quality ok for speech/music at moderate ratios. **Rubber Band = GPLv2+ / commercial** (breakfastquay.com/rubberband/license.html — verified) — high quality but GPL or paid; NOT for us unless purchased. Free fallback: FFmpeg `asetrate`/`atempo` (WSOLA, weaker). Clip-speed pitch-correct is the NLE use case → SoundTouch + atempo both fit, **M, wow 3**. |
| VST3 | **Shipped host** (`vst3_chain.cc`) | Everything the rack lacks can be user-supplied; our job is the built-in floor. |

## 3. Ranked top-10 adoption list (wow-per-cost)

| # | Item | Effort | Wow | Why now |
|---|---|---|---|---|
| 1 | **xfade transitions** — lavfi route (a) or port top-20 native (b) | S–M | 5 | 11→~40-58 transitions; the single most visible category gap; dep already linked |
| 2 | **Expose the effects_rack** as `xer.af.*` (+ IR convolution reverb w/ IR loader) | S | 4 | ~9-10 audio fx for the price of descriptors; convolver = flagship |
| 3 | **Film look kit**: halation + bloom + grain-v2 + gate-weave bundle | S–M | 5 | The "cinematic" checkbox every competitor sells; 3-pass reuse of existing blur+blend ops |
| 4 | **ACES finishing**: bundle slim OCIO config + per-clip input transforms + HDR view | S–M | 5 | Infra already wired (`ocio:`, LUT scan, DisplayViewTransform); only config+UX missing; flagship marketing |
| 5 | **Row-parallel pixel ops** on vendored pthreadpool | M | 4 (perf) | Unlocks preview framerate for everything above; zero new deps |
| 6 | **GL-transitions port** (top ~20, MIT) | M | 4 | License-clean, ports forward to GPU later, complements xfade |
| 7 | **Anamorphic streak + god rays** (highlight→directional blur→add) | S | 4–5 | Two iconic glow fx sharing one new scratch path |
| 8 | **Surface dormant bridge ops** (`FilterConvolve`, `AddNoise-gaussian`, `OilPaint`, `Glow`, gradients) as `xer.fx.*` descriptors + fold legacy `KnownEffectTypes` into the picker | S | 3 | ~8-10 catalog entries for pure plumbing; kills the two-registry confusion |
| 9 | **Stylize bundle**: ordered-dither/Bayer, pixel-sort, Kuwahara, CRT, displacement-map-wrap | S–M ea | 4 | The "creative suite" identity vs. plain NLE; displacement engine already in-tree |
| 10 | **Node-fx / OFX (Motion phase)**: wire `node_graph_executor`+`NodeGraphEditorWidget` to frame ops now, OFX host on top after | L | 5 | Strategic ceiling-raiser: own node compositor + industry plugin ABI |

Honorable mentions (rank 11+, still cheap): temporal kit
(echo/trails via history-buffer or `tblend`), JS-fx live panel,
paper/dust/leaks texture pack (needs CC0 sourcing pass), SoundTouch
pitch-correct for clip speed, pixel-sort/ASCII as JS-fx showcase.

Deliberately deferred/flagged: datamosh true-bitstream (L, fragile —
approx first), Rubber Band (GPL/commercial — SoundTouch instead),
frei0r plugin bundle (GPL — optional host only), native Vulkan compute
& libplacebo (strategic; do after row-parallel + when a GpuFrame seam
exists — don't bolt GPU onto a per-op readback model).

## 4. Sources

- xfade enum + docs: ffmpeg.org/ffmpeg-filters.html §11.292;
  ffmpeg.org/doxygen/8.1/vf__xfade__vulkan_8c.html (XFadeTransitions,
  FFmpeg 8.1); ffmpeg-cvslog "avfilter: add xfade" (expr/custom mode)
- GL-transitions: github.com/gl-transitions/gl-transitions — MIT LICENSE
  verified 2026-11-24 (per-file header licenses possible on individual
  transitions)
- OpenFX: github.com/AcademySoftwareFoundation/openfx — BSD-3-Clause,
  HostSupport/ + Examples/ in-tree; openfx.readthedocs.io
- libplacebo: github.com/haasn/libplacebo — LGPL-2.1 verified
  (mpv shader hooks, Vulkan/D3D11, HDR tonemap)
- frei0r: github.com/dyne/frei0r — GPL-2.0 (repo + Arch/Fedora package
  metadata, ~100 plugins, Kdenlive/Shotcut hosts)
- Rubber Band: breakfastquay.com/rubberband/license.html — GPLv2+ or
  commercial (verified)
- SoundTouch: soundtouch.surina.net/license.html / codeberg.org/
  soundtouch/soundtouch — LGPL-2.1, v2.4.1 latest
- Fast-neural-style ONNX (cross-ref adopted): onnx/models
  `validated/vision/style_transfer/fast_neural_style` (mosaic-9.onnx
  6.7 MB), jcjohnson/fast-neural-style, pytorch/examples
  fast_neural_style — per `2026-11-22-ai-model-stack.md` "Ship — nearly
  free"
- Kuwahara/aniso-Kuwahara: Kyprianidis et al. Pacific Graphics 2009;
  GEGL `kuwahara`/`newsprint` ops as reference impl
- Pixel sort: github.com/kimasendorf/ASDFPixelSort (MIT)
- Panini projection: Sharpless/Postle/German, "Pannini: a new projection
  for rendering wide-angle perspective images" (J. Graphics GPU Tools,
  2010); reference in hugin/panotools
- Prior waves (dedup): `2026-11-21-nle-architectures.md` (OFX/frei0r/
  libplacebo VENDOR verdicts, frei0r license check discharged here),
  `2026-11-22-ai-model-stack.md` (style-transfer adopted),
  `2026-11-23-ai-features-wave2.md` (temporal AI adopted — RIFE flow
  is the datamosh-approx substrate noted above)
