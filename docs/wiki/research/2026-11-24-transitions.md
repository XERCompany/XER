# XER-native transitions — research notes

Date: 2026-11-24. Companion to `2026-11-24-cool-effects-tech.md`
(which evaluated the FFmpeg `xfade` bridge). This doc covers the
HAND-WRITTEN CPU transition set (`tools/video_editor/transitions_lib.*`,
ids `xer.tr.*`, `Transition::Type::XerFx`) — distinct from the
libavfilter `xfade` names handled by `xer_transition_bridge.*`.

## 1. Sources & licenses

### GL-transitions — MIT (primary)

https://github.com/gl-transitions/gl-transitions — ~320 GLSL
transition shaders. Repo `LICENSE`: MIT, "Copyright (c) 2017-present
gl-transitions contributors", with the note that individual
transitions may carry their own license in file header comments —
every shader we referenced carries an explicit `// License: MIT`
header EXCEPT `InvertedPageCurl.glsl` (BSD-3-Clause, Hewlett-Packard),
noted below.

Catalog: `https://api.github.com/repos/gl-transitions/gl-transitions/contents/transitions`
(320 files, fetched 2026-11-24). Shader sources read from
`raw.githubusercontent.com/gl-transitions/gl-transitions/master/transitions/<name>.glsl`.

Per-shader provenance of the ports/adaptations (all MIT, authors as
named in file headers):

| GL shader | Author | Used for |
|---|---|---|
| `WhipPan.glsl` | gre | `xer.tr.whip_pan` — panned strip + jittered motion-blur taps |
| `CrossZoom.glsl` | rectalogic (port of rendermix/glfx.js) | `xer.tr.zoom` — zoom-blur crossfade |
| `Morph.glsl` | paniq | `xer.tr.morph` — color-driven displacement crossfade |
| `GlitchDisplace.glsl`, `GlitchMemories.glsl` | Matt DesLauriers / Gunnar Roth | `xer.tr.glitch` — slice jitter + block noise + RGB split |
| `WhipSpin.glsl` | gre | `xer.tr.spin` — roll-angle cubic + spin blur + per-tap chroma; also informs `xer.tr.chroma` |
| `DirectionalWipe.glsl` | gre | `xer.tr.directional_wipe` — L1-normalized direction + smoothstep edge |
| `CircleOpen.glsl` | gre | `xer.tr.iris` — SDF shape open/close (extended to diamond/box + close variants) |
| `RandomSquares.glsl` | gre | `xer.tr.pixel_displace` — per-block hash threshold + displacement |
| `PerlinDissolve.glsl` | Rich Harris | `xer.tr.ink` — value-noise reveal (smoothstep on a noise field) |
| `FilmStrip.glsl` | gre | `xer.tr.film_roll` — simplified: roll + frame-line + blur (no sprockets) |
| `InvertedPageCurl.glsl` | Hewlett-Packard, **BSD-3-Clause** | NOT ported — `xer.tr.page_curl` is an original diagonal shadowed-roll approximation (no HP code) |
| `RotateScaleFade.glsl` | Fernando Kuteken | reference for spin-zoom compositing |

### NLE conventions (convention targets, no code)

- DaVinci Resolve 21 manual, "Ch. 55 Using Transitions":
  https://docs.imaster.org/davinci-resolve-21-manual/part-7-editing-effects-and-transitions/ch.-55-using-transitions
  — stock bins: Dissolve (Cross/Film/Additive/Subtractive/Highlights/
  Shadows styles — the luma-ordered dissolve convention `xer.tr.luma_fade`
  follows: dark regions first by default), Iris (shape open), Wipe,
  Motion, plus Fusion effects.
- Resolve 18.5 reference manual video-transition table:
  https://ltbits.github.io/davinci-resolve-manuals/DR18/DR18-RM06-EditingEffectsAndTransitions.pdf
- Premiere/AE signature set (community convention — morph cut,
  seamless zoom, whip pan, luma fade, film roll, page curl, light
  leak, glitch, chromatic aberration, ink, particle disperse,
  directional blur, whip spin): all are achievable as pure 2-frame
  pixel ops EXCEPT a true morph cut (needs correspondences/optical
  flow — the GL `Morph` soft-warp version is what we ship).

## 2. The set (ordered wow-per-effort, as implemented)

| id | params (dir/soft/int/seed) | source |
|---|---|---|
| `xer.tr.whip_pan` | dir=pan axis (0 left/1 right/2 up/3 down), int=blur | WhipPan port |
| `xer.tr.zoom` | int=strength | CrossZoom port |
| `xer.tr.luma_fade` | dir 0=dark-first/1=bright-first, soft | Resolve convention |
| `xer.tr.morph` | int=displacement | Morph port |
| `xer.tr.glitch` | int, seed | GlitchDisplace/GlitchMemories-derived, original |
| `xer.tr.chroma` | int=split px | WhipSpin-derived, original |
| `xer.tr.light_leak` | dir=sweep, int, seed | film-kit tie-in, original |
| `xer.tr.film_roll` | dir 0=up/1=down, int=blur | FilmStrip simplification |
| `xer.tr.page_curl` | dir=corner 0..3, soft | original (NOT the HP shader) |
| `xer.tr.directional_wipe` | dir 0..7 compass, soft | DirectionalWipe port |
| `xer.tr.iris` | dir 0..2 open / 3..5 close (circle/diamond/box), soft | CircleOpen port + extensions |
| `xer.tr.flash` | int=peak | Premiere dip-to-white + overshoot, original |
| `xer.tr.spin` | dir 0=CCW/1=CW, int=blur | WhipSpin port |
| `xer.tr.pixel_displace` | soft, int, seed | RandomSquares + displacement |
| `xer.tr.ink` | soft, int=scale, seed | PerlinDissolve port |

## 3. Design constraints honored

- Frame contract identical to `ApplyTransitionPixels`: packed BGRA8
  straight-alpha, `under`=outgoing, `over`=incoming, endpoints
  memcpy-exact (dispatcher short-circuits w<=0/w>=1 so kernels can't
  drift at the ends).
- Deterministic: kernels are pure functions of (A, B, w, params);
  all noise/hash fields derive from `seed` + integer cell coords
  (GL `fract(sin(dot))` hash + integer xorshift — no wall time).
- No allocations inside pixel loops: `out` is the only buffer and the
  compositor reuses `transition_scratch` across frames.
- Unknown `xer.tr.*` id → graceful crossfade fallback, same contract
  as an unknown xfade token.
