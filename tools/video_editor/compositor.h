// Frame-accurate timeline -> canvas BGRA renderer for the VideoEditor
// core (XER Studio Phase 1C). UI-free, single consumer thread —
// RenderFrameAt calls must be serialized (the export worker and a
// future preview engine each own an instance). Init() snapshots the
// project (copies it); decoders open lazily on a clip's first
// contributing frame and close after its last.
//
// Pixel pipeline (all packed BGRA8, straight alpha — NOT
// premultiplied): per video track, contributing clips are the union
// of nominal [timeline_start, end) PLUS transition handle extensions
// (ClipTransitionHandles: duration/2 past each endpoint). Each clip
// decodes sequentially forward with nearest-PTS frame selection,
// runs the CPU ClipEffect chain (video::VideoEffects), warps by its
// affine Transform into a canvas-sized layer (WarpClipFrame — the
// contract the 1D preview must match), transition pairs combine via
// ApplyTransitionPixels, and track layers alpha-over bottom->top onto
// bg_color. A clip with Clip::is_adjustment instead runs its
// effect/mask chain on a snapshot of the already-composited canvas
// (everything below its track) and writes the result back through its
// transform rect — a Premiere adjustment layer. Skia is used ONLY
// inside VideoEffects dispatch; the compositor's own pixel loops are
// plain CPU BGRA.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "effect_frame_cache.h"  // EffectFrameCache (PUBLIC dep)
#include "frame.h"            // video::Frame — XER_Video (PUBLIC dep)
#include "media_decoder.h"    // IMediaDecoder + MediaDecoderFactory
#include "timeline_model.h"

namespace XER::VideoEditor {

// ---- pure helpers (exposed for tests + 1D preview reuse) ----

// Normalized transition progress w in [0,1]: 0 at window start
// (cut - duration/2), 1 at window end (cut + duration/2), clamped.
// `cut_seconds` = ClipEndSeconds(from_clip) == to_clip.timeline_start.
// Degenerate duration <= 0 -> w = 1 for t >= cut, else 0 (such
// transitions never form a render window anyway).
double TransitionWeightAt(double t_seconds, const Transition& transition,
                          double cut_seconds);

// Composites two canvas-sized straight-alpha BGRA layers — `under`
// (from/outgoing) and `over` (to/incoming) — per the transition at
// weight w, into `out` (canvas-sized, may not alias inputs). All three
// frames must share width/height/format(BGRA8); `out` is allocated
// here when needed. `weight` is clamped to [0,1] internally.
//
// Conventions (fixed — the 1D preview must match):
//   Crossfade: premultiplied-alpha lerp (correct for straight-alpha
//     layers; equals MixSystem::Blend(Alpha,w) for opaque inputs).
//   Wipe<D>: hard-edge reveal, no feather (v1) — `over` shows where
//     the directional test passes, `under` elsewhere; inside the
//     revealed region `over` alpha-composites OVER `under` (its
//     transparency lets `under` show — correct for transformed
//     clips). Revealed tests use pixel centers (x+0.5)/W etc.:
//       WipeLeft:  (x+0.5)/W < w        (incoming grows left->right)
//       WipeRight: (x+0.5)/W > 1-w
//       WipeUp:    (y+0.5)/H < w
//       WipeDown:  (y+0.5)/H > 1-w
//   Slide<D>: BOTH layers translate toward D — `under` is displaced
//     -w*dim px (exits toward D) while `over` is displaced
//     +(1-w)*dim px from the opposite side, so the composite motion
//     is toward <D> (SlideLeft = everything moves left). Per canvas
//     pixel: under_px = bilinear(under, x+0.5-udx, y+0.5-udy),
//     over_px  = bilinear(over,  x+0.5-odx, y+0.5-ody) with outside
//     sampling -> transparent, result = AlphaOver(under_px, over_px).
//     At w=0 -> under only; w=1 -> over only.
// Returns false on size/format mismatch or allocation failure.
bool ApplyTransitionPixels(Transition::Type type, double weight,
                           const video::Frame& under,
                           const video::Frame& over,
                           video::Frame* out);

// Renders `src` (packed BGRA8, any dims) into a canvas-sized
// transparent-black layer applying `tr`: the scaled rect
// (src_w*scale x src_h*scale) is placed with its TOP-LEFT at
// (offset_x*canvas_w, offset_y*canvas_h) — the 1B convention — then
// rotated `rotation_deg` about the scaled rect's CENTER, and alpha is
// multiplied by `opacity`. Inverse-mapped bilinear sampling (always
// bilinear in v1 — no nearest fast path); pixels outside the source
// stay transparent (alpha 0). `out_layer` must be canvas_w*canvas_h
// BGRA8 (allocated by caller or here; fully rewritten each call).
// Returns false on invalid inputs/allocation failure.
bool WarpClipFrame(const video::Frame& src, const Transform& tr,
                   int canvas_w, int canvas_h, video::Frame* out_layer);

// Same warp, but the sampled source is a pixel-space sub-rect
// (region_x, region_y, region_w, region_h) of `src` — the smart-
// reframe crop window. The region is treated as the source rect: the
// scaled dest rect is region_w*scale x region_h*scale and the
// transform's offset/rotation apply to it exactly as WarpClipFrame
// does for the full frame. Sampling can bleed a few pixels past the
// region edge on rotation (it clamps at the FRAME edge).
bool WarpClipFrameRegion(const video::Frame& src, double region_x,
                         double region_y, double region_w,
                         double region_h, const Transform& tr,
                         int canvas_w, int canvas_h,
                         video::Frame* out_layer);

// Multiplies `frame`'s alpha by the product of all `masks` (straight
// alpha — only the A channel is scaled; RGB passes through). Masks are
// defined in CLIP-FRAME fractions: shape centered at
// (cx*W, cy*H) sized (w*W, h*H) pixels; feather is a linear alpha
// falloff over feather*min(w_px, h_px) pixels centered on the shape
// boundary (0 -> hard edge); inverted flips inside/outside. Multiple
// masks MULTIPLY coverage. Degenerate masks (non-positive or
// non-finite w/h) cover nothing (coverage 0, or 1 when inverted).
// Returns false when `frame` is null/not a valid BGRA8 frame; an empty
// `masks` list is a no-op true.
bool ApplyMaskPixels(video::Frame* frame,
                     const std::vector<ClipMask>& masks);

// Composites `src` onto `dst` (both packed straight-alpha BGRA8, same
// dims) using `mode`: Normal/SrcOver take the dedicated alpha-over
// fast path; every other mode is delegated to
// video::MixSystem::BlendFrames under its Skia blend name, giving the
// full W3C Porter-Duff / separable / non-separable composite for all
// 29 bridge modes. (Semantic delta vs the retired inline loop for the
// original six modes: translucent layers now also weight by backdrop
// alpha — identical for opaque composites.)
// Returns false on size/format mismatch or invalid frames.
bool ApplyBlendModePixels(BlendMode mode, video::Frame* dst,
                          const video::Frame& src);

// ---- the compositor ----

struct CompositorOptions {
    // Empty -> DefaultDecoderFactory() (real FFmpeg bridge). Tests
    // inject fakes.
    MediaDecoderFactory decoder_factory;
    // Optional shared effect-chain result cache (see
    // effect_frame_cache.h). Null -> caching disabled — the export
    // default, since a sequential single-pass render never re-requests
    // a frame. Preview/interactive callers inject an instance they own
    // so the cache survives compositor session reopens (every backward
    // seek / loop wrap re-binds the compositor — PreviewEngine) and
    // scrub-back over already-rendered frames hits.
    std::shared_ptr<EffectFrameCache> frame_cache;
    // Optional media-path resolver applied ONCE per clip session:
    // maps clip.source_path -> the path the decoder actually opens
    // (e.g. ProxyManager::ResolveDecodePath swaps in a ready proxy for
    // preview decode). Empty -> the source path is used verbatim.
    // Resolution is folded into the clip state hash so toggling the
    // resolver's answer invalidates memoized/cached layers. Generated
    // and adjustment clips never resolve (they own no media file).
    // EXPORT MUST NEVER SET THIS: ExportEngine builds default options
    // so only original media can reach the encode — a proxy leaking
    // into export is a data-integrity bug.
    std::function<std::string(const std::string&)> media_resolver;
};

// Frame-accurate timeline -> canvas BGRA renderer. UI-free, single
// consumer thread (RenderFrameAt calls must be serialized — the
// export worker and a future preview engine each own an instance).
// Init() snapshots the project (copies it); decoders open lazily on a
// clip's first contributing frame and close after its last.
//
// RenderFrameAt assumes non-decreasing `t` per session for
// frame-accurate decode (export drives t forward); a backward jump
// keeps the already-decoded frames (no re-seek) — preview scrubbing
// backward may show a stale frame until the session re-opens.
class Compositor {
public:
    Compositor();
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // Binds a snapshot of `project`. Fails only on invalid canvas
    // (width/height <= 0) or empty fps — clip-level problems (missing
    // files, undecodable media, audio-only-on-video-track) surface as
    // RenderFrameAt errors or Warnings(), not Init failures.
    // `warnings_out` (optional) collects non-fatal notes: unknown
    // ClipEffect.type strings, Skia-only effects when Skia is
    // unavailable, transitions referencing clips not on video tracks.
    bool Init(const Project& project, const CompositorOptions& options,
              std::string* error_out = nullptr,
              std::vector<std::string>* warnings_out = nullptr);

    void Shutdown();   // close all decoders + release frames (idempotent)

    int CanvasWidth() const;
    int CanvasHeight() const;
    double DurationSeconds() const;    // project.duration()
    // Output frame count for fps_num/fps_den:
    // llround(duration * fps_num / fps_den), >= 1 when duration > 0.
    std::size_t FrameCount(int fps_num, int fps_den) const;

    // Renders canvas at timeline time `t` (seconds; clamped to
    // [0, duration]) into `out_bgra`: width*height*4 packed BGRA8,
    // row-major, stride width*4. Returns false + error_out on decode/
    // render failure (missing file, undecodable clip, allocation) —
    // export treats that as fatal. After Cancel(), the next call
    // returns false with "export cancelled"-style error.
    bool RenderFrameAt(double t, std::vector<std::uint8_t>* out_bgra,
                       std::string* error_out = nullptr);

    // ui2 monitor-gizmo seam: replaces one clip's STATIC transform in
    // the bound project snapshot. Sessions stay open and the
    // per-layer memo/effect caches re-key on the transform hash, so a
    // direct-manipulation preview (monitor drag) re-renders through a
    // plain RenderFrameAt — no re-Init, no decoder re-open.
    // Keyframed props still evaluate over the static value via
    // EffectiveTransformAt, matching the inspector's static-field
    // edits. Returns false when the id isn't bound or the compositor
    // isn't initialized. Single-consumer-thread like RenderFrameAt.
    bool UpdateClipTransform(const std::string& clip_id,
                             const Transform& transform);

    // Preview-quality hint in (0,1]: below 1.0 the expensive spatial
    // clip effects (lens sphere / magnifier / ribbed glass / bokeh)
    // run on a bilinear-downscaled copy of the clip frame — with
    // pixel-space params scaled by the same factor — then upscale
    // back to full size. Visually close at monitor sizes, several
    // times cheaper per frame. Sticky per compositor instance;
    // defaults to 1.0. Export and screenshot paths never set it, so
    // the full-quality render stays pixel-identical. NaN/<=0/>1 all
    // clamp back to 1.0.
    void SetPreviewQuality(float scale);
    float PreviewQuality() const;

    // Effect-chain cache counters — a zero Stats when no frame_cache
    // was injected (export/screenshot compositors). Lightweight;
    // intended for perf HUDs and smoke logs.
    EffectFrameCache::Stats FrameCacheStats() const;

    // Non-fatal issues seen so far (Init warnings + runtime notes like
    // "clip X has no video stream"). Cleared by Init/Shutdown.
    const std::vector<std::string>& Warnings() const;

    void Cancel();          // cooperative; checked per RenderFrameAt
    bool Cancelled() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::VideoEditor
