# Lens & Glass effects, waveform Audio Sync, Curve Editor

## Glass / lens effects (compositor, CPU kernels)

Native `ClipEffect.type`s, dispatched in `compositor.cc` next to the
other built-ins (see `KnownEffectTypes`). All spatial remaps snapshot
the frame into a shared thread-local scratch buffer first.

| type | params | notes |
|---|---|---|
| `lens_sphere` | radius frac, bend, chroma px, edge dark, specular | crystal ball: sphere-normal refraction (`nz = sqrt(1-r²)`); bend > ~1.2 inverts the scene like a real ball lens; fresnel rim + glint |
| `magnifier` | radius frac, zoom, chroma px | thin-lens bulge, `bend = 1-1/zoom` |
| `ribbed_glass` | period px, amplitude px, axis, softness | fluted/reeded pane — per-rib cylindrical refraction + haze taps |
| `bokeh_dof` | mode (band|radial), cx, cy, band, falloff, max radius, highlight gain | DOF: CoC grows away from the focus zone; 24-tap Vogel-spiral disc sampling, luma-weighted → aperture-shaped bokeh balls on speculars |
| `bokeh_blur` | radius px, highlight gain | uniform variant of the same kernel |

Depth: there is no true scene depth without a depth map — `bokeh_dof`
exposes the focus zone (linear band or radial point) as a parameter,
which is how practical DOF plugins work. A depth-map input (e.g. from
a luma/alpha channel or an AI depth pass) is a natural extension of
`ApplyBokehDOFPixels`.

GPU: kernels are pure bilinear gathers — a Vulkan/compute variant can
swap the inner loops 1:1. Tracked as a follow-up; the existing
`clip_gpu_accel` OpenCL helper is clip-specific and not a general
effect pipeline yet.

## Waveform audio sync (`audio_sync.{h,cc}`)

`ComputeAudioSyncPlan(project, ref_clip_id, report, error)` is a pure
analysis: unique source paths decode once to peak buckets via
`DecodeAudioPeaks` (media-prep path, subprocess-isolated + disk
cached), each becomes a mono energy envelope decimated to ~21 ms
cells, DC-stripped, and normalized-cross-correlated against the
reference envelope (min 25 % overlap). Best lag → signed timeline
shift at `cell/rate` resolution; confidence < 0.30 is reported but
not applied.

The envelope (not raw PCM) is what makes distant camera mics lock to
a recorder feed: fine spectral detail differs per mic distance, the
energy contour does not.

UI: **Timeline → Sync Audio by Waveform**. Selected clip = reference,
else earliest clip on the lowest track. Each confident move goes
through `TimelineHistory::MoveClip` — one undo step per clip, normal
edit semantics.

## Curve editor (keyframe easings)

`KeyEase` extended: `EaseInCubic`, `EaseOutCubic`, `ExpoOut`, `Back`
(overshoot, c1=1.70158), `Elastic` (decaying ring), `Bounce`
(4-hop landing). Segment easing lives on the segment's START key;
endpoints stay exact. Serialized in project JSON (`ease` field).

Keyframe editor gestures:
- double-click a diamond → cycle through ALL eases
- **Ctrl+C / Ctrl+V** → copy/paste the full curve (times+values+ease)
  between properties and clips (shared clipboard)
- **Ctrl+S** → stagger: shift every other animated property's keys by
  a per-property step so curves interleave (checkerboard layout)
