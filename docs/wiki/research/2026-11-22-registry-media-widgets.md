# Registry media widgets — shadcn-ecosystem component/API survey for our C++ widget kit

Research for the `src/ui/widgets` kit (~74 widget pairs). Surveys ten
registry/docs sites for audio, video, tactile and image-crop components
worth porting to native C++ for the NLE mixer/monitor/timeline domains.
External claims carry URLs and were fetched 2026-11-22; repo claims
verified in the working tree the same day. Subagent research; parent
filed the page. Research only — no code was written.

## 0. Our inventory and verified gaps (repo, 2026-11-22)

`src/ui/widgets` holds ~74 `*.h/*.cc` pairs. Media-relevant existing
widgets: `slider`, `waveform_widget`, `audio_player_widget`,
`video_preview_widget`, `eq_editor`, `fx_rack_widget`, `marker`,
`node_graph_editor_widget`, `chain_map_widget`, `progress`,
`context_menu`, `dropdown_menu`.

Verified gaps (headers read directly):

- **`waveform_widget.h` is a decorative strip, not a peak display.** It
  draws "procedural bars driven by layered sines plus a per-column
  hash", takes no sample/peak data, and `HandleEvent` returns `false`
  (no seeking). Everything a clip waveform needs (peaks, range window,
  playhead, regions, markers, hover) is absent.
- **`slider.h` is a plain linear slider** — no dB taper, no detents, no
  bipolar origin fill, no fine/coarse steps, no editable readout,
  `SetOnValueChanged` only (no committed-vs-live distinction).
- **No rotary knob** — mixer channels, EQ knobs, pan pots have nothing.
- **No level meter** — no peak/RMS bars, ballistics, clip hold, or dB
  scale widget.
- **No XY pad** (surround panner, dual-parameter macros).
- **No transport bar widget** beyond `audio_player_widget` (compact
  audio transport); no scrubber-with-chapters, no timecode readout
  widget, no speed/quality menus.
- **No radial/pie menu** — `context_menu` is linear.
- **No image crop/zoom primitive** — `aspect_ratio` is a layout box only.
- **No numeric scrub field** (drag-to-change readout), no LCD/VFD
  readout widget, no detent/stepped dial.

## 1. audio-ui.dev — deepest catalog; the primary model for our audio kit

https://www.audio-ui.dev — Ouest Labs "Audio UI", a shadcn-style
registry (React/Vue/Svelte/Web headless primitives + styled layer) with
~90 component docs and a full DAW workstation section. Component index
from https://www.audio-ui.dev/sitemap.xml (fetched 2026-11-22): Elements
(Knob, Fader, XY Pad, Envelope, Parameter Field, Rocker Switch, Piano,
Pad, Meter, Needle meter, LED, LCD, Value Display, Waveform, Plot,
Spectrum Analyzer, Tuner), Workstation (Arrangement, Audio editor,
Waveform Lane, Spectrogram Lane, Sound Lane, Piano roll, Sequencer,
Transport, Scrubber, Mixer, Device chain, Device browser, Rack, Export
dialog), Devices (~40 effect panels), plus `use-*` hooks
(`use-audio-levels`, `use-transport-clock`, `use-timeline-zoom`,
`use-marquee-selection`, `use-track-reorder`, `use-undo-history`).

### 1.1 Knob — https://www.audio-ui.dev/docs/components/base/knob

Interaction model worth copying wholesale:

- `dragMode`: `"circular"` (default, 288° sweep), `"vertical"` (150 px
  travel = full range), `"revolution"` (one 360° turn = full range).
  Shift during any drag = 4× slower, and "only the stretch moved with
  Shift held is slowed, small moves add up rather than rounding away".
- Double-click/double-tap (≤ ~12 px, ≤ ~320 ms) or Alt+click/Delete
  resets to `defaultValue` (default: midpoint). Wheel steps by `step`.
- `scale`: `"linear"` | `"log"` | `{to, from}` custom pair — log for
  frequency-type params; on a log scale keys/wheel move a *share of
  travel* (1%, 10% with Page keys) instead of a `step`, min ≥ one step.
- `anchor`: where the indicator arc grows from (0 on a −1…1 pan knob).
- `marks`: `(number | {value, label})[]` graduations; those reached from
  `anchor` light up. `editableValue`: type the value in the readout.
- `onValueChange(value, details)` vs `onValueCommitted(value, details)`
  — details carries `reason` ("drag"|"wheel"|"keyboard"|"reset"),
  `source` ("mouse"|"touch"|"pen"|"keyboard"|"programmatic"|"agent"),
  `event`, and `cancel()` to refuse the change. **The live-vs-committed
  split is the single most portable idea** — our Slider only has the
  live callback.
- Parts: `Root / Control / Track / Indicator (SVG arc) / Thumb / Value /
  Mark / Provider` (Provider sets default dragMode for a subtree — maps
  to a mixer-wide "all knobs drag vertical" setting).

### 1.2 Fader — https://www.audio-ui.dev/docs/components/base/fader

Same Root/Control/Track/Indicator/Thumb/Mark/Value anatomy, vertical or
horizontal, thumb marks, press-track-to-jump, `onValueCommitted` on
gesture end, thumb follows pointer even when the controlled value lags.

### 1.3 Meter — https://www.audio-ui.dev/docs/components/base/meter

- `value`/`peak`: number arrays, one per channel (dB). `warnThreshold`
  and `clipThreshold` (default 0 dB) drive `data-warning` /
  `data-clipping` states; a clip is **sticky** (`data-clipped` until the
  user clicks the meter — the DAW convention).
- `source` prop = external store `{getSnapshot(), subscribe(listener)}`
  — engine writes levels 60×/s straight to the widget, bypassing the UI
  tree. Direct analogue for a C++ kit: a `FrameSource`/`LevelsSource`
  the widget polls per frame instead of pushing through property setters.
- Parts: `Root / Channel{index} / Indicator / Peak / Mark / Value /
  Label`; root exposes `{channelCount, orientation}` so channels follow
  the source. Peak-hold decay is computed by the *source* (the
  `use-audio-levels` hook: RMS per channel per frame with smooth fall,
  peak held a moment then falls), keeping the widget clock-free.

### 1.4 Waveform — https://www.audio-ui.dev/docs/components/base/waveform

The flagship design to copy for `waveform_widget`:

- `values`: `ArrayLike<number>` **or `{min,max}` peak pairs per slice**
  (±1) — i.e. real peaks, computed once per file at high resolution
  ("a few thousand slices covers a zoomed-in clip editor"); the canvas
  resamples to width. `computePeaks` takes the lowest+highest sample
  across *all* channels per slice so hard-panned audio still draws.
- `range: [start,end]` — window onto the source (clip offset/length);
  playhead, seeking, regions and markers all follow it; position outside
  range sets `data-outside` and hides the playhead rather than pinning
  to an edge.
- `shape`: `"mirror" | "bars" | "line"`; `barWidth`, `barGap`,
  `barRadius`; drawn on the device pixel grid, repaint only on change;
  >16 384 px canvas ⇒ coarser bitmap.
- Interaction: press jumps, drag scrubs (`reason: "press"|"drag"`),
  Shift+drag = region selection (`onSelectionChange`), arrows step,
  Home/End, PageUp/Down largeStep.
- Parts: `Canvas / Bars / Control / Playhead / Thumb / Hover / Marker /
  Region / RegionHandle`; `LiveWaveform` (scrolling mic input),
  `BarVisualizer` (frequency bands, `mirrored`).
- `source` for live playhead + `levels` for live signal overlay;
  `loading` sweeps bars while decoding.

### 1.5 XY Pad — https://www.audio-ui.dev/docs/components/base/xypad

Independent `minX/maxX/stepX` and `minY/maxY/stepY`; press jumps, drag
follows, wheel/trackpad moves both axes by scrolled distance; Shift+arrow
= 10 steps; `onValueChange{reason: "press"|"drag"|"wheel"|"keyboard"}`;
position exposed as `--xy-pad-x/y` for crosshairs. Value is `{x,y}`,
`onValueCommitted` on gesture end.

### 1.6 Transport / Scrubber / Needle meter / small parts

- **Transport** (…/base/transport): `source` live position, `tempo
  {bpm, beatsPerBar, beatUnit}`, `signatures[]` for time-sig changes,
  `playing/onPlayPause`, `onStop`, `looping`, `recording`, `countIn`
  (bars), `punch`, `metronome`/`rhythm`/`sound`. Bar is composed of
  clusters — copy the *cluster composition*, not the DAW feature set.
- **Scrubber** (…/base/scrubber): minimal — `source` position,
  `duration`, `playing`, `onPlayingChange`, `onSeek(seconds)`.
- **Needle meter** (…/base/needle-meter): `source`, `reference`
  (default −18 dB = 0 VU) — a VU is just a live meter + a reference
  offset + a needle part.
- **LCD/LED/Value display/Parameter field/Rocker switch**: small
  display parts; ParameterField = label + control + formatted value +
  edit affordance (good shape for FX param rows).
- **Spectrum Analyzer** (…/base/spectrum-analyzer): live
  `FrameSource<VisualFrame>` band data, log-frequency axis, peak hold.

## 2. audiocn.dev — second audio registry; ballistics + taper vocabulary

https://audiocn.dev — fortysevenfx/orcdev. Component list from sitemap:
audio-device-select, audio-player, bar-visualizer, channel-strip,
channel-toggle, clip-indicator, db-readout, db-scale, fader, knob,
lcd-display, lcd-meter, level-meter, live-waveform, mixer, pan-control,
parameter-slider, smooth/electric waveforms, sound-pad, spectrum,
tempo-control, track-list, volume-control; hooks use-mixer,
use-web-audio-mixer, use-audio-analyser, use-frame-source,
use-waveform-data, use-tap-tempo, use-clip-hold.

### 2.1 LevelMeter — https://audiocn.dev/docs/components/level-meter

The best meter API we found. Three feed modes:

```tsx
<LevelMeter source={analyser.meter}/>            // FrameSource<MeterFrame>, paints itself
<LevelMeter peakDb={-9.1} rmsDb={-18}/>          // slow data; ballistics smooth it
meter.current?.paint({channels:[{peakDb:-9.1}]}) // caller-owned frame loop via actionsRef
```

- `ballistics: "peak" | "vu" | "instant" | Partial<BallisticsOptions>`
  — documented behavior: peak rises in 15 ms and falls steadily; vu is
  slow and averaged; instant follows input exactly.
- `taper: "linear" | "audio" | Taper` — "audio" gives the loud end most
  of the track (console/recorder convention).
- `zones: MeterZone[]` + `variant: "solid"|"segmented"|"gradient"` +
  `minDb/maxDb`; `data-clipping` held 1.5 s; `--meter-level/--meter-rms/
  --meter-hold` live CSS vars per channel.
- Parts: `LevelMeterChannel{index}`, `LevelMeterBar{measure:"peak"|"rms"}`
  (two bars layered = dual peak/RMS meter), `LevelMeterHold` (peak-hold
  tick), `LevelMeterScale/Value/Clip` wrapping `DbScale`, `DbReadout`,
  `ClipIndicator`. ARIA updates throttled to 4 Hz.

### 2.2 Fader — https://audiocn.dev/docs/components/fader

dB-native: `taper: "linear"|"audio"` decides where each dB sits on the
travel; thumb snaps to 0 dB while dragging; `detents: number[]`;
`silenceAtMin` reports −∞ at the bottom; `fineStep` (Alt+drag/arrows,
0.1 dB) vs `step` (0.5 dB) vs `largeStep` (6 dB); `origin` fills from
middle; `FaderValue editable` is a fixed-width readout ("as wide as the
longest text the range can show, so moving the fader never shifts the
layout") — adopt that trick. `FaderTrack` accepts a `LevelMeter` child
(meter inside the fader track = console strip).

### 2.3 Knob — https://audiocn.dev/docs/components/knob

`dragDirection: "vertical"(default)|"horizontal"|"circular"`,
`sensitivity` (px for full range, default 200), `origin`, `arc`,
`resetValue`, `fineStep` (Shift = 10× finer, press/release mid-drag
without a jump), `scale:"linear"|"log"`, `clickSound` (synthesized tick
per detent — decaying sine + noise snap), `format`/`parse` for
type-in editing, `KnobScale{ticks, majorEvery, labelEvery}` +
`KnobCap{variant:"mini"}` metal-cap skin, `--knob-angle` live var,
`data-at-origin`. Ticks between origin and value light up (bipolar knob
lights from center).

### 2.4 Mixer + PanControl + support parts

- **Mixer** (…/components/mixer): `MixerChannels` scrolls along the
  strip axis while `MixerMaster` stays pinned; `MixerEmpty` zero state;
  every meter shares `minDb/maxDb/zones/ballistics` "so levels compare
  fairly across channels"; **Ctrl+arrow moves the same control to the
  neighbouring strip** — great keyboard model for our mixer.
- **ChannelStrip**: fader + meter + mute/solo/monitor `ChannelToggle`s.
- **PanControl** (…/components/pan-control): −1…1, **detent snap near
  center**, `formatPan` → "L30"/"C"/"R30" + `parsePan` for type-in.
- **DbScale** (…/components/db-scale): ticks from `ticks[]` or common
  values, `taper:"audio"` so fader/meter/scale align, `side`, major
  ticks — decorative (`aria-hidden`) since the parent reports values.
- **Spectrum** (…/components/spectrum): `source:FrameSource<VisualFrame>`,
  `minHz/maxHz/minDb/maxDb`, `scale:"log"`, `peakHold`, `grid`,
  `variant:"bars"|"line"|"area"`, level+frequency axes parts.
- **LcdMeter**: dot-matrix segments aligned to the display's pixel grid
  (segment = 3 dots + 1-dot gap; "a lit segment lights whole"), same
  LevelMeter data model; CLIP flag inverts while clipping, is a button
  that clears it.
- **Waveform** (…/components/waveform): `peaks`, `duration`, `time:
  FrameSource<number>` playhead without re-renders, `onSeek` vs
  `onSeekCommitted`, `variant:"bars"|"line"|"mirror"`, `barWidth/Gap/
  Radius`, `WaveformRegion{start,end,resizable,draggable,minLength}`,
  `WaveformMarker{time}` — confirms the audio-ui design.
- **TempoControl**: BPM input with tap tempo + rolling average;
  **ParameterSlider**: label + slider + unit + marks + reset for
  "everything that is not a volume".

## 3. waves-cn.vercel.app — wavesurfer.js wrappers; zoom/minimap/regions

https://waves-cn.vercel.app — thin shadcn wrappers over wavesurfer.js;
component docs all share a base prop set (`src`, `defaultVolume`,
`waveColor`/`progressColor`, `waveHeight`, `barWidth/barGap/barRadius`,
`onPlay/onPause/onFinish/onTimeUpdate`). Per-component extras
(https://waves-cn.vercel.app/sitemap.xml):

- **WaveTimeline** (…/components/wave-timeline): `defaultZoom/minZoom/
  maxZoom` in **px/sec** (the right unit for a zoom model), plus
  `topTimeline`/`bottomTimeline` ruler options `{height, timeInterval,
  primaryLabelInterval, secondaryLabelInterval, fontSize}` — a
  tick-density spec worth copying for timeline rulers.
- **WaveMinimap** (…/components/wave-minimap): overview strip
  (`minimapHeight`, colors) + viewport rectangle over the whole file —
  the pattern for a DAW navigator/minimap.
- **WaveRegions** (…/components/wave-regions): `editable` toggles
  drag-create/move/resize; `RegionData{id,start,end,color,content}`;
  `onRegionCreated/Updated/Removed/Clicked` — region lifecycle events.
- **useWavesurfer** (…/docs/use-wavesurfer): the escape hatch —
  `{container, url, peaks?: precomputed, duration, plugins[], ...rest}`
  returning `{wavesurfer, isReady, isPlaying, currentTime}`.
  Precomputed `peaks` + `duration` skips decode — same trick for us.
- Also: wave-envelope, wave-hover, wave-speed, wave-zoom, wave-recorder,
  wave-spectrogram, wave-video.

## 4. videocn.dev — video player chrome; the transport spec

https://videocn.dev/docs — a shadcn Video.js-style player (`VideoCn`
component). Its `controls` object is the cleanest transport-chrome spec:

- `visibility: "auto"|"always"|"never"` + `autoHideDelay` (default
  3000 ms) — overlay chrome model.
- Granular toggles: `play`, `scrubber` (`{chapters?: bool}`), `time`,
  `volume` (slider folds behind icon on touch), `fullscreen`,
  `pictureInPicture`, `chapters` (menu, only when chapters exist),
  `playbackRate` (`{rates:[0.5…2]}` sorted+deduped), `quality` (greyed
  not hidden when engine has no levels), `live` badge, `keyboard`.
- **Keyboard map**: Space/K play-pause, ←/→ ±5 s, ↑/↓ volume ±5%, M
  mute, F fullscreen, 0–9 jump to n×10%; arrows drive the focused
  slider; ignored inside inputs. `chapters` = `{time, label}[]` shown in
  scrubber + menu.
- `type:"native"|"hls"|"dash"` explicit override for extension-less
  signed URLs; engine (Shaka) lazy-loaded — the "heavy decoder behind an
  adapter" pattern we already use.
- Styling via two tokens: `--player-scrim` (gradient under controls),
  `--player-backdrop` (frame) — monitor chrome should get equivalent
  theme tokens.

## 5. videojs.org docs — media-chrome-style part taxonomy

https://videojs.org/docs/guides/installation/shadcn — the new Video.js
docs index lists a full part taxonomy worth mirroring as widget
decomposition: `media-play-button`, `media-mute-button`,
`media-seek-button`, `media-fullscreen-button`, `media-pip-button`,
`media-captions-button`, `media-playback-rate-button`,
`media-live-button`, `media-slider`, `media-time-slider` +
`media-time-slider-markers`, `media-volume-slider`,
`media-menu`/`media-popover`, `media-audio-track-radio-group`,
`media-captions-radio-group`, `media-playback-rate-radio-group`,
`media-quality-radio-group`, `media-time`, `media-title`,
`media-poster`, `media-thumbnail`, `media-tooltip`,
`media-buffering-indicator`, `media-seek-indicator`,
`media-status-indicator`, `media-volume-indicator`,
`media-dialog`/`media-error-dialog`, `media-gesture`, `media-hotkey`,
`media-status-announcer`; skins `video-skin`, `*-scaffold-skin`,
`live-*-skin`, `background-video-skin`. Takeaway: build transport as
composable `Media*` parts over a store, plus `media-hotkey` as a
behavior part.

## 6. framecn.vercel.app (framecn.dev) — animation/caption blocks

https://www.framecn.dev — paid catalog of motion components; sitemap
shows ~200 items mostly marketing-oriented (carousels, globes, scroll
animations, compositions). NLE-relevant subset: the **captions family**
(~30 `components/captions/*`: caption-clip-wipe, caption-editorial-
emphasis, caption-kinetic-slam, caption-highlight, caption-matrix-
decode, caption-parallax-layers, caption-particle-burst …) — a catalog
of caption *animation presets*, matching our missing caption-clip type
(gap noted in `2026-11-22-modern-nle-features.md`); and `odometer-text`
in the text family. No player chrome here; treat as reference for
caption preset design, not widgets.

## 7. tactileui.dev — hardware-physics parts; the "feel" vocabulary

https://tactileui.dev — 224-part kit + 39 standalone "objects"
(paid Pass; docs are public). Interaction details that transfer to C++:

- **Knob part** (https://tactileui.dev/parts/knob): attrs `min/max/step/
  value`, `labels[]` per step (a detented selector *speaks* "Team", not
  "1"), `default-value` reset via double-click/Delete/Backspace, ticks
  printed on the plate, drag up/right, Shift = 5× finer, **wheel only
  while focused** (page scroll never changes a setting by accident),
  `input` while turning vs `change` on release.
- **Detent Dial object** (…/objects/detent-dial): `detents` count,
  `detentPull` strength (how hard a notch holds before clicking over),
  `land only on detents` selector mode, `sweep` degrees, LED ring to
  value, rotary vs vertical drag, synthesized click.
- **Fader part** (…/parts/fader): native range under the hood; "Channel
  strip with its LCD" variant; slot + printed scale via repeating
  gradients; cap `data-material`.
- **Meter part** (…/parts/meter): built on the native `<meter>` —
  `min/max/low/high/optimum` gives zones + screen-reader value for free;
  `--lo/--hi` cut lines, `--n` segment count, head shows value + state
  in words with a green/amber/red lamp.
- **Scrub field** (…/parts/scrub): drag a numeric readout with a ruler
  in the well — `px-per-step` slow rate with smooth ×4 speedup, Shift
  ×10 / Alt ×0.1, `data-detent` (each step "moves the ruler a notch"),
  `data-lock` = **pointer lock for endless drags past screen edge**,
  click-to-type, clamp with "a knock at the stop", `unit`/`unit-text`.
  This is the inspector numeric field every NLE property panel needs.
- **Media bar** (…/parts/media-bar): transport bound to a media element
  by `for`/`media`; `skip` (arrow-key seconds), `speed/speeds` (rate
  menu), `announce` cadence for spoken position, `toggle()/seek(s)`
  methods, **loaded vs played lengths drawn on the same slot**
  (buffered under progress), live = can't scrub.
- **Odometer object** (…/objects/odometer): mechanical drum counter —
  a digit moves only when its right neighbour carries; spring
  `stiffness`/`damping` (<1 overshoots), `spinCap` digits/s before
  blurring, `count` per-second rate, locale grouping, reserved digits,
  red decimal drum. Fun candidate for frame/timecode counters.
- **Compass object** (…/objects/compass): liquid-damped card that
  swings past and settles; bezel in 1° detents; `lampFrom` degrees;
  read-only readout mode — the model for a jog/angle indicator.
- **Mixer object** (…/objects/mixer): N channel strips (trim, 3-band
  EQ, color filter, cue, fader + LED ladder; EQ/filter knobs click at
  middle) + crossfader with **smooth/linear/cut curves** + flip-to-back
  panel; ladders "rise at once and fall over about a third of a second".

## 8. piemenu.jpmarqu.es — radial menu; press-drag-release

https://piemenu.jpmarqu.es (+ /llms-full.txt, MIT). Parts: `PieMenu`
root, `PieMenuTrigger`, `PieMenuContent` (ring), `PieMenuItem`
(`onSelect`, `onHighlight`), `PieMenuIndicator` (hub arc that turns
toward the highlighted item, always the short way), `PieMenuCenter`.
Props: `openOn:"press"|"contextmenu"`, `radius` (96 px), `deadZone`
(20 px center that selects nothing). Gestures: press→drag out→release
selects; drag back into deadZone→release cancels; **nothing is
highlighted until the pointer leaves the center** (avoids accidental
selection); keyboard open highlights first item. Items can be added
while open — the ring re-flows and keeps the highlight; `data-selected`
marks the chosen item during exit. `usePieMenuAim()` hook for headless
use. For us: tool/action radial in timeline + monitor (marking menus).

## 9. medium-zoom.francoischalifour.com — image zoom model

https://medium-zoom.francoischalifour.com/docs — tiny API, still the
reference zoom interaction: options `margin` (px around zoomed image),
`background` overlay, `scrollOffset` (scroll distance that closes,
default 40, 0 = any scroll), `container` (viewport override or rect),
`template` (custom zoomed markup). Methods `open/close/toggle({target})`,
`attach/detach`, `update`, `clone`, `on/off`; events
`open/opened/close/closed/detach/update`. Interaction: click expands to
viewport, click/scroll/Esc dismisses — the "click a thumbnail, get the
asset" primitive for a media browser/lightbox.

## 10. zriz.dev — iOS-style image cropper; best crop primitive set

https://zriz.dev/docs — headless cropper (`@zriz/react` + `@zriz/core`).
Composition: `Cropper.Root{src}` > `Cropper.Viewport` > `Image`, `Mask`,
`Cropper.Area` (+ `Grid`, handles `position:"top-left"…` with
`data-edge`, `--zriz-handle-size` 44 px hit box) + sibling controls
`Dial`/`DialTicks`, `Zoom`/`ZoomThumb`/`ZoomStep{factor}`, `Rotate`,
`Flip`, `Reset`, `AspectRatio` menu, `Cropper.Value`, `actionsRef`.

- **Value model**: a `CropValue` (crop rect + rotation + zoom) — one
  serializable value, `setValue` still applies while `disabled`.
- **Straighten Dial** (…/docs/dial): the iOS Photos ruler — ±45°, ticks
  every 2.25°/9 px, first 10 px of drag only grabs the ruler then 4 px =
  1°, flick with inertia + spring-back at limits, **catch at 0°** (within
  1° it holds exactly 0, flicks landing within 1° snap to 0), photo
  auto-zooms as angle grows and restores on return.
- **Crop area**: drag body to move, 8 handles to resize, `data-active`
  per handle, thirds/fine/none `Grid`, `minSize` can clamp below
  `maxZoom`.
- **Zoom**: `ZoomStep{factor}` multiplicative steps, press-on-track
  jumps, `maxZoom` on Root caps everything.
- **Export** (…/docs/exporting): `exportCrop(cropper, {type, maxSize,
  signal})` → blob, or send `CropValue` to the server and cut there —
  same split we'd want (Skia render vs export pipe).
- Locking: `disabled` freezes gestures but queued aspect/rotation/zoom
  apply on unlock; `data-interacting` for dimming chrome mid-gesture.

## 11. Prioritized recommendations

| # | New/updated widget | Our file | API sketch (C++) | Effort | NLE use-case |
|---|---|---|---|---|---|
| 1 | **AudioMeter** | `audio_meter_widget.{h,cc}` | `SetSource(shared_ptr<MeterSource>)` polled per frame; `SetBallistics(Ballistics::Peak/Vu/Instant, {attackMs,releaseMs,holdMs})`, `SetTaper(Audio|Linear)`, `SetRange(minDb,maxDb)`, `zones{warn,clip}`, sticky clip cleared by click; channels render peak bar + rms overlay + hold tick + DbScale | M | Mixer strips, monitor output meters, record levels (audio-ui Meter + audiocn LevelMeter) |
| 2 | **Knob** | `knob.{h,cc}` | `SetRange(min,max,step,largeStep,fineStep)`, `SetScale(Linear|Log|custom)`, `SetDragMode(Circular|Vertical|Revolution)`, `SetAnchor`, `detents[]`/`detentPull`, `resetValue`, `marks[]`, callbacks `OnValueChanged(v,Details{reason,source,cancel})` + `OnValueCommitted`; wheel-only-when-focused; Shift=4–10× fine | M | Mixer pans/trims, EQ params, FX params everywhere (audio-ui + audiocn + tactile detent-dial) |
| 3 | **PeakWaveform** (rewrite) | `waveform_widget.{h,cc}` | `SetPeaks(span<const Peak{min,max}>, duration)` real data; `SetRange(t0,t1)` window; `SetShape(Mirror|Bars|Line)`, `SetPlayheadSource`, regions `OnSelectionChanged/Committed`, `AddMarker(t)`, `OnSeek/OnSeekCommitted`, `loading` sweep; device-pixel canvas | M | Clip waveforms in timeline/source monitor — replaces procedural sine strip (audio-ui Waveform + audiocn + waves-cn) |
| 4 | **Fader (dB)** | `fader.{h,cc}` (or extend `slider`) | dB-native: `SetTaper(Audio)`, `detents` (0 dB snap), `silenceAtMin`, `step/largeStep/fineStep` (0.5/6/0.1 dB), `origin` bipolar fill, editable fixed-width readout, optional embedded meter in track | S–M | Mixer channel faders, monitor volume (audiocn Fader) |
| 5 | **TransportBar** | `transport_widget.{h,cc}` | Composable parts: play/stop/loop, `Scrubber` with `chapters[]`+buffered-under-progress, `time` readout, `speed` menu `{rates[]}`, `SetVisibility(Auto|Always|Never,autoHideMs)`, keyboard map (Space/K, arrows ±5 s, 0–9 jump), `source` live position, `OnSeek(seconds)` | M | Program/source monitor chrome (videocn controls + videojs Media* parts + audio-ui Scrubber) |
| 6 | **LevelFrameSource** (infra, not a widget) | `ui/frame_source.h` | `template<class T> struct FrameSource{ virtual T Snapshot()=0; virtual void Subscribe(listener)=0; }` — engine pushes 60 Hz, widget paints without tree invalidation | S | Feeds meters, playheads, spectrum, live waveforms — the one pattern every site converged on |
| 7 | **DbScale + DbReadout** | `db_scale.{h,cc}` | `SetRange(minDb,maxDb)`, `SetTaper`, `ticks[]`, major ticks, `side`; readout updates ≤4 Hz, fixed width ("−12.0 dB", "Silent") | S | Rulers beside meters/faders, inspector readouts (audiocn) |
| 8 | **RadialMenu** | `radial_menu.{h,cc}` | `items[]{icon,label,onSelect,onHighlight}`, `radius`, `deadZone`, `openOn(Press|ContextMenu)`; press-drag-release, drag-back-to-center cancels, nothing highlighted until pointer leaves deadZone, live re-flow | M | Tool marking menu on timeline/monitor, workspace switching (piemenu) |
| 9 | **ScrubField** | `scrub_field.{h,cc}` | Drag-to-change numeric readout: `pxPerStep` with speed-up, Shift ×10/Alt ×0.1, `detents`, pointer-lock endless drag, click-to-type with `parse`, `unit`, clamp "knock" | S | Every numeric inspector field (timecode, opacity, speed %) — tactile scrub |
| 10 | **XYPad** | `xypad.{h,cc}` | `SetRangeX/Y(min,max,step)`, `{x,y}` value, press-jump+drag, wheel moves both axes, crosshair via value fraction, committed callback | S | Surround/ambisonics panner, 2-param FX macros (audio-ui XYPad) |
| 11 | **SpectrumAnalyzer** | `spectrum_widget.{h,cc}` | `SetSource(FrameSource<Bands>)`, `SetFreqRange(minHz,maxHz,Log)`, `SetDbRange`, `peakHold`, `grid`, `variant(Bars|Line|Area)` | S–M | Monitor/analysis panel; EQ overlay (audiocn Spectrum + audio-ui) |
| 12 | **ImageCropper** | `image_cropper.{h,cc}` | `CropValue{rect,rotation,zoom}`; Viewport+Mask+Area+8 handles+Grid(thirds), `StraightenDial` (±45°, 0° catch, inertial flick), `ZoomStep{factor}`, `maxZoom`, disabled-queues-ops | M | Photo workspace crop/straighten, thumbnail reframing (zriz) |
| 13 | **ImageZoom** (lightbox) | fold into `hover_card`/`dialog` | `Open(target)` expands to viewport with `margin`, `background` scrim, scroll/Esc/click closes; `OnOpened/OnClosed` | S | Media browser asset preview (medium-zoom) |
| 14 | **VuNeedle / LcdReadout / Odometer** | `vu_meter.{h,cc}` etc. | NeedleMeter: same MeterSource + `reference` dB=0 VU + needle part; LCD: dot-matrix segmented readout; Odometer: carrying drums + spring | S | Retro meter bridge, timecode counters, hardware-skin themes (audio-ui needle-meter + tactile) |
| 15 | **Caption preset catalog** | not a widget — `assets/scripts/fx` | Port framecn caption animations as JS fx presets once a caption clip type exists | L | Text-based captions feature (framecn captions + modern-nle-features gap) |

**Cross-cutting design rules to adopt** (all sites agree):

1. **Live-vs-committed split** on every value widget
   (`OnValueChanged`+`OnValueCommitted{reason,source,cancel}`).
2. **Pull-based `FrameSource`** for 60 Hz data (levels, playhead,
   spectrum) — push props only for slow state.
3. **dB taper + detents + silenceAtMin** on audio faders; **0 dB/0 pan
   snap** on everything bipolar (`origin`/`anchor` fill).
4. **Wheel only while focused**; Shift=fine, Page/Home/End map; reset
   via double-click/Delete/Alt+click to `resetValue`.
5. **Parts decomposition** (Root/Control/Track/Indicator/Thumb/Value/
   Mark) so mixer strips, FX panels and monitor chrome compose rather
   than fork.

## Sources

- https://www.audio-ui.dev (+ /sitemap.xml, /docs/components/base/{knob,fader,xypad,meter,needle-meter,waveform,scrubber,transport,lcd,led,value-display,parameter-field,spectrum-analyzer,envelope}) — fetched 2026-11-22
- https://audiocn.dev (+ /docs/components/{knob,fader,level-meter,lcd-meter,mixer,pan-control,waveform,spectrum,db-scale,live-waveform,channel-strip}) — fetched 2026-11-22
- https://waves-cn.vercel.app (+ /docs/components/{wave-player,wave-regions,wave-minimap,wave-timeline,wave-zoom}, /docs/use-wavesurfer) — fetched 2026-11-22
- https://videocn.dev/docs — fetched 2026-11-22
- https://videojs.org/docs/guides/installation/shadcn — fetched 2026-11-22
- https://www.framecn.dev (+ /sitemap.xml) — fetched 2026-11-22
- https://tactileui.dev (+ /parts/{knob,fader,meter,scrub,media-bar,tumbler}, /objects/{odometer,compass,detent-dial,mixer}) — fetched 2026-11-22
- https://piemenu.jpmarqu.es (+ /llms.txt, /llms-full.txt) — fetched 2026-11-22
- https://medium-zoom.francoischalifour.com/docs/reference/javascript/{options,methods} — fetched 2026-11-22
- https://zriz.dev/docs/{root,dial,zoom,crop-area,exporting} — fetched 2026-11-22
