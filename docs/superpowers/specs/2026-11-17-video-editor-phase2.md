# XER Studio Video Editor — Phase 2 design

Goal: bring the editor to Premiere/AE/Photoshop-level feature parity on top
of the Phase 1 headless core. All new model data is UI-free and serializes
into `.xerproj` (schema v2, backward compatible: v1 files load with
defaults; saving always writes v2).

## 1. Model additions (tools/video_editor/timeline_model.h)

```cpp
enum class BlendMode { Normal, Add, Multiply, Screen, Overlay, Darken, Lighten };

enum class MaskShape { Rect, Ellipse };
struct ClipMask {
    MaskShape shape = MaskShape::Ellipse;
    float cx = 0.5f, cy = 0.5f;    // center, fraction of clip frame w/h
    float w  = 0.5f, h  = 0.5f;    // size, fraction
    float feather = 0.05f;         // fraction of min(w,h)
    bool inverted = false;
};

enum class KeyProp { PosX, PosY, Scale, Rotation, Opacity, GainDb };
enum class KeyEase { Linear, EaseIn, EaseOut, EaseInOut, Hold };
struct Keyframe {
    double time = 0.0;             // clip-local seconds, sorted, unique
    float  value = 0.0f;
    KeyEase easing = KeyEase::Linear;
};

struct Marker {
    double time = 0.0;
    std::string name;
    std::array<std::uint8_t,4> color{255, 214, 0, 255};
};
```

Clip gains:
`double playback_rate = 1.0` (source t = in_point + (tau-start)*rate),
`BlendMode blend_mode = Normal`, `float pan = 0.0f` (-1..1),
`std::string link_group` (non-empty = linked),
`std::vector<ClipMask> masks`,
`std::map<KeyProp, std::vector<Keyframe>> keyframes`.

Track gains `bool locked = false` — every clip-affecting mutating op on a
locked track fails with "track is locked".

Project gains `std::vector<Marker> markers`,
`double work_in = 0, work_out = 0` (work_out <= work_in ⇒ whole project).

New transition types appended to `Transition::Type` (keep existing ordinals!):
`DipToBlack`, `DipToWhite`, `AdditiveDissolve`.

## 2. New model ops (validate-then-mutate, transactional)

```cpp
bool RippleDeleteClip(Project&, const std::string& clip_id,
                      std::string* error_out);            // remove + close gap on its track
bool LiftRange(Project&, double t0, double t1, bool close_gap,
               std::string* error_out);                  // all tracks; close_gap=false = lift
bool RollingEdit(Project&, const std::string& from_id, const std::string& to_id,
                 double delta_seconds, const MediaBounds& a_bounds,
                 const MediaBounds& b_bounds, std::string* error_out);
bool SlipClip(Project&, const std::string& clip_id, double delta_seconds,
              const MediaBounds&, std::string* error_out);   // in_point += delta
bool SlideClip(Project&, const std::string& clip_id, double delta_seconds,
               const MediaBounds&, std::string* error_out);  // move, neighbours stretch/shrink
bool SetClipRate(Project&, const std::string& clip_id, double rate,
                 const MediaBounds&, std::string* error_out); // Premiere Speed: keeps in_point+consumed source, duration = consumed/rate
bool InsertClipAt(Project&, const Clip& clip, double at_seconds,
                  const MediaBounds&, std::string* error_out); // shift right then add
bool OverwriteClipAt(Project&, const Clip& clip,
                     const MediaBounds&, std::string* error_out); // clear range then add
bool AddMarker(Project&, const Marker&, std::string* error_out);
bool RemoveMarker(Project&, double time);
bool SetWorkArea(Project&, double in_s, double out_s, std::string* error_out);
bool SetTrackLocked(Project&, int track_index, bool locked,
                    std::string* error_out);
bool SetClipLinkGroup(Project&, const std::string& clip_id,
                      const std::string& group);         // "" = unlink
bool SetClipKeyframes(Project&, const std::string& clip_id, KeyProp prop,
                      std::vector<Keyframe> keys, std::string* error_out);
```

Snap points additionally include marker times and work-area edges.

## 3. Rendering (compositor + audio_mixer)

- Keyframes evaluated per rendered frame at clip-local time
  `u = (tau - start)` (rate does NOT stretch keyframe time — keys are
  timeline-local like Premiere). Interpolation by easing; Hold keeps
  last value. Evaluated transform = keyframe value where animated,
  else clip.transform field.
- `playback_rate` changes the decode source-time map; also in mixer.
- `blend_mode` applied when compositing the clip's layer over the
  accumulated track/canvas: Normal=alpha-over; Add=additive; Multiply,
  Screen, Overlay, Darken, Lighten per standard formulas (straight
  alpha: blend applies to rgb where over-alpha>0, then alpha-over).
- `masks` applied to the layer AFTER warp (mask coords are fractions of
  the layer's clip rect in canvas space? NO — simpler and correct v2:
  mask is defined in clip-frame fractions, applied to `s.frame` BEFORE
  warp, multiplying alpha by the soft mask).
- New transitions: DipToBlack/White (through solid), AdditiveDissolve
  (additive lerp). `ApplyTransitionPixels` extended; signature unchanged.
- Mixer: `pan` -> stereo balance (equal-power), `track.gain_db` applied.
- Mixer exposes `LastChunkPeakL/PeakR` or `PeakLevels()` accessor for UI
  meters; export engine forwards them optionally.

## 4. Export

`ExportOptions` gains `double range_in = 0, range_out = 0`
(out <= in ⇒ whole project). Export renders the work area when set.
`--export` CLI gets `--range <in> <out>`? Keep simple: honor project
work area only; no new CLI flags.

## 5. Catalog

- `AudioPeaks`: per-channel min/max pairs at fixed samples-per-peak
  (~256). Stored in a `peaks` SQLite table keyed by path+hash.
  API: `bool GetAudioPeaks(const std::string& path, AudioPeaks* out)`.
- `GetPoster(path, seconds)` — poster at arbitrary source time (falls
  back to middle when seconds < 0).

## 6. UI

- Timeline: waveform on audio clips (peaks), poster thumbnails on video
  clips, marker lane on the ruler, work-area bar with drag handles,
  track header lock/mute/hide toggles, ripple/roll/slip/slide edit modes
  via modifier keys (Alt=ripple delete-on-delete, Shift-drag edge=roll,
  Ctrl-drag body=slip, Alt-drag body=slide... see app note), snapping to
  markers/work edges.
- Tools/keys (Premiere): V selection, C razor, B ripple-edit,
  N rolling-edit, Y slip, U slide, R rate-stretch, M marker,
  I/O set work in/out, Q/W ripple trim to playhead,
  ,/. nudge selected ±1 frame, Ctrl+D default transition at cut.
- Inspector: blend-mode combo, speed field, pan field, per-property
  keyframe buttons + mini key list, mask add/remove, link group display.
- Effects panel (list of all ClipEffect types → apply to selection).
- Monitor: audio meters (mixer peaks), safe margins overlay toggle,
  marker indicator on playhead.
- Export dialog: range = work area / entire sequence.

## 7. File ownership (parallel implementation)

| Area | Files |
|---|---|
| model+history+ops | timeline_model.*, timeline_history.*, model/history tests |
| render | compositor.*, audio_mixer.*, export_engine.*, their tests |
| catalog | media_catalog.*, clip_poster.*, catalog tests |
| timeline UI | ui/timeline_widget.* |
| inspector+panels UI | ui/inspector_widget.*, new ui/effects_panel.*, ui/markers_panel.* |
| monitor+export UI | ui/monitor_widget.*, ui/export_dialog.* |
| app glue | ui/video_editor_app.* (LAST — after all widgets land) |
