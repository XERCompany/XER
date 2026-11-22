// Headless timeline model for the VideoEditor core (XER Studio
// Phase 1B). Pure data + synchronous, UI-thread-owned mutations — no
// threads, no async, no engine objects inside Project. Media bounds are
// passed per-call via MediaBounds so the model stays decoupled from the
// decoder/catalog.
//
// Invariants enforced by the mutating ops (all validate-then-mutate,
// transactional — a rejected op leaves the project untouched):
//   * clip.track indexes Project::tracks and is in range
//   * audio-only media (MediaBounds::has_video == false) only on
//     TrackKind::Audio tracks
//   * clip ids are non-empty and unique
//   * no two clips overlap on the same track (adjacency allowed)
//   * transitions only between adjacent clips on the same VIDEO track
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace XER::VideoEditor {

enum class TrackKind { Video, Audio };

struct Transform {
    float offset_x = 0.0f;      // clip top-left, fraction of canvas w/h
    float offset_y = 0.0f;
    float scale = 1.0f;         // uniform multiplier on source pixels
    float rotation_deg = 0.0f;
    float opacity = 1.0f;
};
struct ClipEffect { std::string type; std::vector<float> params; };

// Photoshop-style layer blend applied when the clip's rendered layer is
// composited over what is below it on the canvas. The first seven keep
// their ordinal values and wire names for existing .xerproj files; the
// remaining entries cover the full Skia bridge blend-mode set (see
// Bridge::Skia::BlendModeNames()). SrcOver/Plus are listed separately
// from Normal/Add so project files can name either spelling — they
// composite identically.
enum class BlendMode {
    Normal, Add, Multiply, Screen, Overlay, Darken, Lighten,
    Clear, Src, Dst, SrcOver, DstOver, SrcIn, DstIn, SrcOut, DstOut,
    SrcATop, DstATop, Xor, Plus, Modulate, ColorDodge, ColorBurn,
    HardLight, SoftLight, Difference, Exclusion, Hue, Saturation,
    Color, Luminosity,
};

enum class MaskShape { Rect, Ellipse };
// Soft mask defined in CLIP-FRAME fractions (applied to the decoded
// frame's alpha BEFORE the transform warp).
struct ClipMask {
    MaskShape shape = MaskShape::Ellipse;
    float cx = 0.5f, cy = 0.5f;   // center, fraction of frame w/h
    float w = 0.5f, h = 0.5f;     // size, fraction of frame w/h
    float feather = 0.05f;        // soft edge, fraction of min(w,h)
    bool inverted = false;
};

// Animated clip property (After-Effects-style keyframes). Time is
// clip-local seconds [0, duration] — playback_rate does not stretch it.
enum class KeyProp { PosX, PosY, Scale, Rotation, Opacity, GainDb };
// Easing of the segment STARTING at a keyframe. Linear..Hold mirror
// classic bezier-style curves; Bounce/Elastic/Back are the curve
// editor's overshoot family (see tools/video_editor/ui/curve_presets).
enum class KeyEase {
    Linear, EaseIn, EaseOut, EaseInOut, Hold,
    EaseInCubic, EaseOutCubic, ExpoOut,
    Back,      // overshoot: dips below 0 then settles at 1
    Elastic,   // damped oscillation settling at 1
    Bounce,    // gravity bounce landing on 1
};
struct Keyframe {
    double time = 0.0;
    float value = 0.0f;
    KeyEase easing = KeyEase::Linear;
};

struct Marker {
    double time = 0.0;
    std::string name;
    std::array<std::uint8_t, 4> color{255, 214, 0, 255};
};

// Clip source kind. Media decodes source_path; generated kinds produce
// pixels/audio procedurally (Premiere "color matte / bars & tone /
// title" equivalents) and keep source_path empty.
enum class SourceKind { Media, ColorMatte, ColorBars, Text };
// source_param payload per kind:
//   ColorMatte — "#RRGGBB" or "#RRGGBBAA" hex
//   ColorBars  — reserved ("smpte75" default)
//   Text       — the UTF-8 text to render, optionally followed by
//                " | key=value" inline style tokens (size / color /
//                outline / outline_color / align — see ClipTextStyle
//                in generated_source.h)
struct ClipSource {
    SourceKind kind = SourceKind::Media;
    std::string param;
};

// A hosted audio plugin on a clip's FX chain (VST3 via
// src/audio/plugin_wrappers). params maps VST3 param id -> normalized
// value and is applied on every (re)instantiation.
struct ClipAudioFx {
    std::string plugin_path;   // .vst3 bundle/binary path (UTF-8)
    std::string name;          // display name (scanner/factory)
    bool enabled = true;
    std::map<std::uint32_t, float> params;
};

// AI object-detection tag on a clip — produced by "AI: Find Objects"
// (ai_tags.h / YA.AIFindObject). Times are SOURCE seconds (same domain
// as in_point), so tags survive re-trims and rate changes: the tag is
// visible on the timeline while its source range intersects the clip's
// consumed span [in_point, in_point + duration * playback_rate).
struct ClipTag {
    double start = 0.0;      // source seconds
    double end = 0.0;        // source seconds, >= start
    std::string label;       // detected class ("person", "car", ...)
    float confidence = 0.0f; // max confidence observed across the range
};

// Smart-reframe subject sample (ai/smart_reframe.cc — "Smart Reframe").
// One analyzed frame's PRIMARY subject box. Stored in SOURCE seconds
// (same convention as ClipTag) with box fields as fractions of the
// decoded frame so resolution changes never invalidate the track.
struct ReframeSample {
    double t = 0.0;        // source seconds
    float cx = 0.5f;       // subject center x, frame fraction 0..1
    float cy = 0.5f;       // subject center y, frame fraction 0..1
    float w = 0.0f;        // subject box width, frame fraction
    float h = 0.0f;        // subject box height, frame fraction
    float confidence = 0.0f;
    bool scene_boundary = false;  // a cut immediately precedes this
                                  // sample — the smoother may snap
                                  // instead of gliding across it
};

// Analyzed Smart Reframe track on a clip. The samples carry subject
// boxes only — the target aspect lives on the xer.fx.smart_reframe
// effect's target_ratio param, so re-shaping the output never needs
// re-analysis. Empty samples => the renderer falls back to a centered
// aspect-locked crop.
struct ClipReframe {
    std::vector<ReframeSample> samples;  // sorted by t, source seconds
};

// AI "Track object" result on a clip (ai/object_track.h — ctx-menu
// "Track object…" / inspector OBJECT TRACK). The smoothed bounding-box
// track that pinned content follows: baked title positions and the
// live xer.fx.track_mosaic censor both resolve from it. Samples reuse
// ReframeSample verbatim — same source-seconds / frame-fraction
// contract — but the track is stored SEPARATELY from Clip::reframe so
// pinning never fights the reframe analysis for the same storage (a
// clip can reframe a person while censoring a car). `label` names the
// dominant tracked class; `classes` lists every distinct label the
// scan observed — the "pick detection" candidates a re-scan can be
// narrowed to (ScanClipTrack's label_filter).
struct ClipTrack {
    std::vector<ReframeSample> samples;   // sorted by t, source seconds
    std::string label;                    // picked class ("person", …)
    std::vector<std::string> classes;     // labels observed in the span
};

struct Clip {
    std::string id;             // uuid (NewClipId)
    std::string source_path;    // UTF-8
    // Optional user label shown on the clip instead of the source
    // basename — the only way generated sources (matte/bars/title,
    // which keep source_path empty) can carry a custom name.
    std::string name;
    int track = 0;              // index into Project::tracks
    double timeline_start = 0;  // seconds
    double in_point = 0;        // source seconds
    double duration = 0;        // timeline seconds; out = in + duration
    double playback_rate = 1.0; // source t = in_point + (t-start)*rate
    bool muted = false;
    float gain_db = 0;
    float pan = 0.0f;           // stereo position [-1, 1]
    double fade_in = 0, fade_out = 0;   // seconds
    Transform transform;
    BlendMode blend_mode = BlendMode::Normal;
    std::vector<ClipMask> masks;
    std::map<KeyProp, std::vector<Keyframe>> keyframes;  // sorted by time
    std::vector<ClipEffect> effects;
    std::vector<ClipAudioFx> audio_fx;  // plugin chain, applied in order
    ClipSource source;                  // Media by default
    // Premiere-style adjustment layer (video tracks only): the clip
    // owns no pixels — its effect/mask chain runs on the composite of
    // everything below its track and the result is written back
    // through its transform rect. `source` is ignored.
    bool is_adjustment = false;
    std::string link_group;     // non-empty: clips in a group move together
    std::vector<ClipTag> tags;  // AI find-objects result (source seconds)
    ClipReframe reframe;        // AI smart-reframe subject track
    ClipTrack object_track;     // AI track-object result for pinning
    // Compound clip (Premiere "Nest" / Resolve "Compound Clip" /
    // FCP "Compound"): non-empty `compound` is the id of a Sequence in
    // Project::sequences whose timeline the clip renders/mixes. The
    // normal timing fields keep their meaning: in_point is a
    // SEQUENCE-local seconds offset, playback_rate maps sequence time
    // like media time, duration is the timeline footprint. source/
    // source_path stay empty — the sub-sequence is the source.
    std::string compound;
};
struct Transition {
    std::string from_clip, to_clip;     // adjacent on same VIDEO track
    enum class Type {
        Crossfade, WipeLeft, WipeRight, WipeUp, WipeDown,
        SlideLeft, SlideRight, SlideUp, SlideDown,
        DipToBlack, DipToWhite, AdditiveDissolve,
        Script              // blend weight comes from a JS fx script
    } type = Type::Crossfade;
    double duration = 1.0;              // seconds, centered on the cut
    // type == Script: transition id from scripts/fx/fx.json —
    // weight(progress) is called per frame; renders as a crossfade
    // with the script-supplied easing curve.
    std::string script;
};
struct Track {
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool muted = false;    // audio: silence; video: ignored (use hidden)
    bool hidden = false;   // video: not composited (+ its audio); ignored on audio
    bool locked = false;   // no clip-affecting edits allowed
    float gain_db = 0;     // applied by the audio mixer
    float pan = 0.0f;      // stereo position [-1, 1] — summed with
                           // clip.pan (clamped) by the audio mixer
};
// A named sub-sequence — the timeline content a compound clip (see
// Clip::compound) renders/mixes, and the editing unit the UI opens
// into a nested timeline level. Tracks/clips/transitions/markers have
// EXACTLY the Project-field meanings, in sequence-local seconds; a
// Sequence never stores canvas/fps — it inherits the owning
// Project's. `source_tracks` runs parallel to `tracks` and records
// the outer track index each inner track was copied from at
// MakeCompound time (-1 when created inside another sequence or by
// hand) — FlattenCompound prefers it to restore the original layout
// before falling back to greedy kind-matched assignment.
struct Sequence {
    std::string id;          // uuid — same namespace style as clip ids
    std::string name;        // shown on compound clips + breadcrumbs
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    std::vector<Transition> transitions;
    std::vector<Marker> markers;
    std::vector<int> source_tracks;   // parallel to tracks; -1 = none
    double work_in = 0.0, work_out = 0.0;
    double duration() const;   // max clip end, 0 if empty
};
struct Project {
    int width = 1920, height = 1080;
    int fps_num = 30, fps_den = 1;
    std::array<std::uint8_t,4> bg_color{0,0,0,255};
    std::vector<Track> tracks;
    std::vector<Clip> clips;
    std::vector<Transition> transitions;
    std::vector<Marker> markers;        // timeline markers, sorted by time
    // Named sub-sequences compound clips can reference via
    // Clip::compound. Sequences are stored — never embedded in a
    // clip — so a compound is one Clip + one Sequence row. Orphaned
    // sequences (no clip references them) are legal but
    // FlattenCompound deletes the sequence it restores when nothing
    // else uses it. Nesting is supported (a sequence may itself
    // contain compound clips) subject to kMaxCompoundDepth; cycles are
    // rejected at load and by MakeCompound construction.
    std::vector<Sequence> sequences;
    double work_in = 0.0, work_out = 0.0;   // export/preview range;
                                           // work_out <= work_in = whole
    float master_gain_db = 0.0f;       // post-mix master fader (dB),
                                       // preview + export
    // ---- proxy workflow (proxy_manager.cc) --------------------------
    // Decode through ready proxies for VIDEO decode in preview (the
    // compositor's media_resolver seam). Audio always comes off the
    // original; export never resolves. Default ON like Premiere —
    // it is a no-op until a proxy actually exists for a source.
    bool use_proxies = true;
    // Build proxies automatically when video media is imported
    // (Premiere ingest "create proxies"). OFF by default — on-demand
    // via the media-bin "Create proxy" item is the zero-surprise
    // mode.
    bool proxy_ingest = false;
    // Generation settings for newly built proxies (existing proxies
    // are unaffected — their own params ride the catalog row).
    int proxy_max_dim = 960;        // long-edge cap, px
    int proxy_crf = 28;             // h264 constant-quality target
    // Managed-proxy storage cap in MiB (0 = unbounded). ProxyManager
    // evicts least-recently-used proxy files + catalog rows once the
    // proxies dir exceeds it; sources in the open project are
    // protected (see proxy_manager.h EnforceStorageCap). The LRU
    // stamp is the proxy file's mtime — touched on every resolve.
    int proxy_cache_max_mb = 20480; // 20 GiB default
    double duration() const;   // max(timeline_start + duration), 0 if empty
};

// Source bounds for invariant checks. duration_seconds <= 0 means
// "unknown" → no upper clamp; has_video == false restricts the clip to
// audio tracks.
struct MediaBounds {
    double duration_seconds = 0.0;
    bool has_video = true;
};

enum class TrimEdge { In, Out };

// ---- queries ----
const Clip*  FindClip(const Project&, const std::string& clip_id);
Clip*        FindClip(Project&, const std::string& clip_id);
const Track* FindTrack(const Project&, int track_index);
double       ClipEndSeconds(const Clip&);                    // start + duration
bool         ClipActiveAt(const Clip&, double t);            // start <= t < end
// Strict [start, end) interval overlap — adjacency (end == start) is
// NOT overlap. Track equality is the caller's concern.
bool         ClipsOverlap(const Clip& a, const Clip& b);
std::vector<const Clip*> ClipsOnTrack(const Project&, int track_index);  // by start
std::vector<Clip*>       ClipsOnTrack(Project&, int track_index);
// Sorted unique snap candidates: 0, every clip start/end on
// `track_index` (all tracks when < 0), every marker time, work-area
// edges, plus `playhead` when >= 0.
std::vector<double> CollectSnapPoints(const Project&, int track_index,
                                      double playhead);
// `candidate` snapped to the nearest snap point within `threshold`
// seconds (ties → earlier point), else `candidate` unchanged.
double SnapTime(const Project&, int track_index, double candidate,
                double threshold, double playhead);

// ---- mutations (validate-then-mutate; transactional; error_out) ----
// Shared placement validation: track index valid; has_video==false
// requires TrackKind::Audio; id non-empty/unique (unless it equals
// exclude_clip_id); every double/float field finite — timeline_start,
// in_point, duration, gain_db, fade_in/out, transform, effect params
// (NaN defeats all relational checks and would persist as NaN/Infinity
// literals that ProjectStore::Save emits but its own Load rejects);
// duration > kMinClipDuration; start >= 0; in_point >= 0;
// in_point + duration <= media duration when known; no overlap on the
// track excluding exclude_clip_id.
bool ValidateClipPlacement(const Project&, const Clip&, const MediaBounds&,
                           const std::string& exclude_clip_id,
                           std::string* error_out);

int  AddTrack(Project&, TrackKind kind, const std::string& name);  // index or -1
// Only when the track holds no clips; reindexes clip.track > index.
bool RemoveTrack(Project&, int track_index, std::string* error_out);

bool AddClip(Project&, const Clip&, const MediaBounds&, std::string* error_out);
bool RemoveClip(Project&, const std::string& clip_id);
// Premiere "ripple delete": remove the clip and shift every clip that
// starts after it on the SAME track left by the clip's duration.
bool RippleDeleteClip(Project&, const std::string& clip_id,
                      std::string* error_out);
// Lift/Extract: remove every clip (or clip portion) intersecting
// [t0, t1) on ALL unlocked tracks. Clips spanning the range edges are
// split at the boundaries first. close_gap=false lifts (leaves a
// hole); close_gap=true extracts and shifts subsequent clips left.
bool LiftRange(Project&, double t0, double t1, bool close_gap,
               std::string* error_out);
bool MoveClip(Project&, const std::string& clip_id, double new_timeline_start,
              const MediaBounds&, std::string* error_out);
// Rolling edit on an adjacent pair (from ends where to starts, same
// track): delta > 0 moves the cut right — `from` grows into its out
// handle, `to` shrinks from its in side. Both re-validated against
// their own media bounds.
bool RollingEdit(Project&, const std::string& from_clip,
                 const std::string& to_clip, double delta_seconds,
                 const MediaBounds& from_bounds,
                 const MediaBounds& to_bounds, std::string* error_out);
// Slip: shift the clip's source window — in_point += delta, timeline
// position/duration unchanged. Bounded by [0, media - rate*duration].
bool SlipClip(Project&, const std::string& clip_id, double delta_seconds,
              const MediaBounds&, std::string* error_out);
// Slide: move the clip delta seconds while its neighbours absorb the
// shift — the preceding clip's out point and the following clip's in
// point/stretch change; the moved clip's in_point is unchanged.
bool SlideClip(Project&, const std::string& clip_id, double delta_seconds,
               const MediaBounds&, std::string* error_out);
// Premiere Speed/Duration: rate>0; keeps in_point and the consumed
// source span (duration*old_rate), sets duration = consumed/rate —
// the clip shrinks or grows on the timeline, gap/overlap re-validated.
bool SetClipRate(Project&, const std::string& clip_id, double rate,
                 const MediaBounds&, std::string* error_out);
// Insert-edit: shift every clip on clip.track with start >=
// clip.timeline_start right by clip.duration, then add.
bool InsertClipAt(Project&, const Clip&, const MediaBounds&,
                  std::string* error_out);
// Overwrite-edit: remove clips intersecting the clip's span on
// clip.track (splitting boundary spanners), then add.
bool OverwriteClipAt(Project&, const Clip&, const MediaBounds&,
                     std::string* error_out);
// Markers are sorted by time; equal times merge (update name/color).
bool AddMarker(Project&, const Marker&, std::string* error_out);
bool RemoveMarker(Project&, double time);
// out <= in resets to "whole project". Values are clamped to [0, dur].
bool SetWorkArea(Project&, double in_seconds, double out_seconds,
                 std::string* error_out);
// Toggles Track::locked. A locked track rejects every clip-affecting
// op (add/remove/move/trim/split/replace/transition/lift).
bool SetTrackLocked(Project&, int track_index, bool locked,
                    std::string* error_out);
// Assigns clip.link_group ("" = unlink). All clips sharing a non-empty
// group are intended to be moved/trimmed together — grouping is data
// only; coordinated moves are the caller's job (e.g. per-member
// MoveClip under one history macro).
bool SetClipLinkGroup(Project&, const std::string& clip_id,
                      const std::string& group);
// Replaces the sorted key list for one property. Empty `keys` removes
// the property. Keys outside [0, duration] or non-finite are errors.
bool SetClipKeyframes(Project&, const std::string& clip_id, KeyProp prop,
                      std::vector<Keyframe> keys, std::string* error_out);
// TrimEdge::In  — new_point is a new in_point (source s);
//   timeline_start += (new_in - old_in); duration = old_out - new_in.
// TrimEdge::Out — new_point is a new out_point (source s);
//   duration = new_out - in_point; timeline_start unchanged.
bool TrimClip(Project&, const std::string& clip_id, TrimEdge edge,
              double new_point, const MediaBounds&, std::string* error_out);
// split_timeline_t strictly inside (start + kMinClipDuration,
// end - kMinClipDuration). First half keeps `clip_id`; second half gets
// `new_clip_id` (caller-supplied, non-empty, unique). Second half:
// in_point += split_t - start; timeline_start = split_t;
// duration = end - split_t. Both keep muted/gain/transform/effects;
// fade_in stays on first half (second gets 0), fade_out stays on second
// (first gets 0). Transitions with to_clip==id keep pointing at the
// first half; transitions with from_clip==id are re-pointed to
// new_clip_id.
bool SplitClip(Project&, const std::string& clip_id, double split_timeline_t,
               const std::string& new_clip_id, std::string* error_out);
// Inspector/track-move edits: replacement.id is ignored — the stored
// clip keeps `clip_id`; all other fields (incl. track) come from
// `replacement` and are re-validated.
bool ReplaceClip(Project&, const std::string& clip_id, const Clip& replacement,
                 const MediaBounds&, std::string* error_out);
bool AddTransition(Project&, const std::string& from_clip,
                   const std::string& to_clip, Transition::Type type,
                   double duration, std::string* error_out,
                   const std::string& script = "");
bool RemoveTransition(Project&, const std::string& from_clip,
                      const std::string& to_clip);
// Retargets an existing transition's duration (junction-handle drag).
// Same contract as AddTransition minus the duplicate-endpoint checks:
// the transition must exist, its track must be unlocked, duration must
// be finite and positive. Media-handle bounds are NOT model state —
// callers clamp through TransitionDurationLimit beforehand.
bool SetTransitionDuration(Project&, const std::string& from_clip,
                           const std::string& to_clip, double duration,
                           std::string* error_out);
// Upper bound (timeline seconds) for a transition's centered duration:
//   * the window may not exceed either endpoint clip's own span — a
//     wider half would overlap a third clip's contribution window, so
//     duration <= 2 * min(from.duration, to.duration);
//   * `from`'s tail handle — media source seconds past its out point,
//     converted to timeline seconds by playback_rate — bounds the
//     right half: duration <= 2 * tail_src / from.rate (only when
//     from_bounds.duration_seconds > 0; an unknown media length and
//     generated sources leave that side unbounded);
//   * `to`'s head handle — source seconds before its in point —
//     bounds the left half: duration <= 2 * in_point / to.rate (media
//     clips only; generated sources are time-invariant and unbounded).
// Returns the smallest bound; +infinity when nothing limits the
// duration (e.g. both endpoints are generated sources). Returns 0 when
// the endpoints can't be resolved.
double TransitionDurationLimit(const Project&, const Transition&,
                               const MediaBounds& from_bounds,
                               const MediaBounds& to_bounds);
// All supported transition types in stable menu order — UI pickers
// (junction context menu, inspector combo) iterate this. Keep in sync
// with the serialization name table in project_store.cc.
const std::vector<Transition::Type>& AllTransitionTypes();
// Human-readable menu label ("Wipe Left") — the lowercase wire names
// live in project_store.cc.
const char* TransitionTypeLabel(Transition::Type type);
// Removes every transition that is not video-track, missing an
// endpoint, on different tracks, or non-adjacent (to.start ≈
// from.end). Called by every mutating op and by project_store load.
void DropInvalidTransitions(Project&);
// Same prune for a stored sequence's transition list (sequence-local
// tracks/clips obey identical rules).
void DropInvalidTransitions(Sequence&);

// ---- compound clips / nested sequences ------------------------------

// Deepest supported compound nesting (compound inside compound inside
// ...). Render/mix paths stop recursing here and contribute nothing
// rather than running an unbounded subgraph — project_store rejects
// deeper graphs at load.
inline constexpr int kMaxCompoundDepth = 16;

const Sequence* FindSequence(const Project&,
                             const std::string& sequence_id);
Sequence*       FindSequence(Project&, const std::string& sequence_id);
// A compound clip carries a non-empty sequence reference.
bool IsCompoundClip(const Clip&);
// True when ANY clip references `sequence_id` — the top-level
// project.clips plus every stored sequence's clips are all scanned,
// so a sequence used only inside another compound still counts.
bool SequenceInUse(const Project&, const std::string& sequence_id);
// Materializes `sequence` as a renderable Project: canvas/fps/proxy
// fields come from `owner`, tracks/clips/transitions/markers/work
// area come from the sequence, sequences are shared (nested compound
// clips inside the sequence resolve against the owner's store), and
// bg_color/master_gain_db are neutralized — inner holes render
// transparent and the inner mix carries no master fader (the outer
// project's own bg/fader apply once at its level).
Project SequenceProject(const Project& owner, const Sequence& sequence);
// Detects a compound-reference cycle (seq A contains a clip -> seq B
// -> ... -> seq A) anywhere in the stored sequences. Cycles can only
// enter through hand-edited files — MakeCompound always mints a fresh
// sequence, so a new reference can never close a loop.
bool CompoundGraphHasCycle(const Project&);

// Premiere "Nest…" / Resolve "New Compound Clip": the clips named by
// `clip_ids` (any mix of video/audio tracks) move into a new stored
// Sequence and are replaced by ONE compound clip.
//   * The new sequence's timeline starts at 0: inner clip starts are
//     shifted by -span_start; inner tracks are copied in the order of
//     the distinct source tracks used, and Sequence::source_tracks
//     remembers each copy's outer index for FlattenCompound.
//   * Transitions fully inside the selection move into the sequence;
//     transitions crossing the selection boundary are dropped (a
//     cross-boundary transition has no valid endpoint afterwards).
//   * Markers are NOT moved (they stay at the parent level, matching
//     Premiere); keyframes/link_groups/tags ride the copied clips.
//   * The compound clip lands on the lowest-indexed source track
//     matching the content kind (first video track when any selected
//     clip sits on one, else first audio track), spans the union
//     [min_start, max_end), and gets name/in_point=0/duration=span.
//     Its other fields stay at Clip defaults — the caller edits them
//     like any clip afterwards.
// `name` labels both the clip and the stored sequence (empty -> a
// default is generated). `new_clip_id`/`new_sequence_id` are
// caller-supplied so undo commands can replay the exact result
// (SplitClip precedent) — non-empty, unique in their namespaces.
// Errors: empty selection, unknown/duplicate ids, locked source or
// target track, a non-selected clip overlapping the span on the
// target track, or an id collision. Transactional.
bool MakeCompound(Project&, const std::vector<std::string>& clip_ids,
                  const std::string& name,
                  const std::string& new_clip_id,
                  const std::string& new_sequence_id,
                  std::string* error_out);

// Inverse of MakeCompound ("Decompose" / flatten): the compound clip
// is removed and its sequence's clips are restored inline exactly
// where the compound DISPLAYED them — inner clip c lands at
// clip.timeline_start + (c.start - clip.in_point) / clip.playback_rate
// with duration c.duration / clip.playback_rate and its own rate
// folded by the compound's, so a retimed or in-trimmed compound
// restores WYSIWYG. Content landing before t=0 is trimmed away.
// Tracks are restored via
// Sequence::source_tracks first, then greedy kind-matched assignment,
// creating tracks when needed. Inner clip ids that collide with
// existing project clip ids are reminted (inner transitions follow
// via the remap). The stored sequence is deleted when no other clip
// (top-level or nested) still references it. Errors: unknown clip,
// not a compound, unresolved sequence, locked target track.
// Transactional.
bool FlattenCompound(Project&, const std::string& clip_id,
                     std::string* error_out);

// Handle seconds a clip's contribution window extends past its
// nominal [timeline_start, end) range: {in_handle, out_handle} where
// in_handle = duration/2 of the transition where clip is `to`,
// out_handle = duration/2 where clip is `from` (0 when none). Both
// compositor (video) and audio_mixer (audio) consume this — the
// transition window is centered on the cut and reaches into source
// handles per the spec's handle model. `max` is used per side so a
// degenerate project with two transitions sharing an endpoint (model
// invariants prevent it, but loaded/self-healed projects could
// theoretically contain it) gets the larger handle — the safer
// monotone choice. Unknown clip ids return {0, 0}.
std::pair<double, double> ClipTransitionHandles(
    const Project& project, const std::string& clip_id);

// Centered letterbox fit: scale = min(canvas_w/src_w,
// canvas_h/src_h); offset = (canvas - src*scale)/2 as a fraction of
// canvas w/h. Returns identity Transform{} when any dim <= 0.
Transform FitTransformToCanvas(int src_w, int src_h, int canvas_w,
                               int canvas_h);

// ---- keyframe evaluation (compositor/mixer consume these) ----

// Interpolated value of `prop` at clip-local time u (seconds).
// `fallback` is returned when the clip has no keys for prop — pass the
// corresponding static field (e.g. transform.opacity). Hold keeps the
// previous key's value; EaseIn/Out/InOut use quadratic/smoothstep
// curves on the SEGMENT fraction between surrounding keys.
float EvaluateClipProperty(const Clip&, KeyProp prop, double u,
                           float fallback);
// clip.transform overridden per-animated property at clip-local u.
Transform EffectiveTransformAt(const Clip&, double u);
// clip.gain_db overridden when GainDb is animated.
float EffectiveGainDbAt(const Clip&, double u);

std::string NewClipId();   // uuid v4, "8-4-4-4-12" hex

}  // namespace XER::VideoEditor
