// .xerproj JSON v2 persistence for the VideoEditor timeline model
// (XER Studio Phase 1B). Static and UI-free; all paths are UTF-8
// std::strings. Save is crash-safe: the JSON is written to
// <path>.tmp and then renamed over the target, so a crash mid-write
// never leaves a truncated .xerproj.
#pragma once

#include <string>
#include <vector>

#include <json/json.h>

#include "media_catalog.h"
#include "timeline_model.h"

namespace XER::VideoEditor {

// Loads/saves Project as .xerproj JSON. Save always writes the current
// kVersion; Load accepts any version in [1, kVersion] — v2 adds
// optional-only fields (playback_rate, blend_mode, pan, link_group,
// masks, keyframes, track.locked, markers, work area, new transition
// types), so v1 files load cleanly with defaults.
// Load is lenient about absent optional fields and self-heals
// transitions that reference missing/non-adjacent clips via
// DropInvalidTransitions; it errors on absent required fields, wrong
// JSON types, duplicate clip ids, out-of-range track indices, and
// negative times/durations. Never throws.
class ProjectStore {
public:
    static constexpr int kVersion = 2;
    static constexpr int kMinSupportedVersion = 1;

    // UTF-8 paths. Save writes <path>.tmp then renames (crash-safe);
    // on Windows rename-over-existing may need remove+retry — handled
    // internally with std::error_code, never throws.
    static bool Save(const Project& project, const std::string& utf8_path,
                     std::string* error_out = nullptr);
    // Replaces project_out's ENTIRE contents — canvas, tracks, clips,
    // transitions. If that Project is bound to a TimelineHistory, the
    // caller MUST call history.Clear() before the load (or rebuild
    // history afterwards): pending commands capture slices of
    // apply-time state, so Undo() after a load would restore pre-load
    // clips into the new project. Undo history is not migrated
    // across loads.
    static bool Load(const std::string& utf8_path, Project* project_out,
                     std::string* error_out = nullptr);
};

// Transition type <-> stable wire name. The names are the .xerproj
// vocabulary AND the ve.core v3 service-table argument values
// ("crossfade", "wipe_left", ..., "script") — one table serves both.
const char* TransitionTypeToString(Transition::Type type);
bool TransitionTypeFromString(const std::string& text,
                              Transition::Type* out);

// ---- ve.core v3 describe layer --------------------------------------
// UI-free JSON projections of the live editor state behind the
// ve_call service table (sdk/ve.core.h). The app widget's
// DispatchExtHostCall answers read-only services with these and the
// smoke suite consumes the same builders, so the served shapes have
// exactly one implementation. Every function returns a JSON OBJECT
// (list services put their array under a named key) — the dispatcher
// stamps "ok":true on top.
Json::Value VeDescribeProject(const Project& project,
                              const std::string& project_path,
                              bool dirty);
Json::Value VeDescribeTimeline(const Project& project, double playhead,
                               const std::string& selected_clip_id);
Json::Value VeDescribeTracks(const Project& project);
Json::Value VeDescribeClips(const Project& project);
// One clip's describe object ({} when the id is unknown) — the same
// per-clip shape VeDescribeClips emits inside its "clips" array.
Json::Value VeDescribeClip(const Project& project,
                           const std::string& clip_id);
Json::Value VeDescribeTransitions(const Project& project);
Json::Value VeDescribeMarkers(const Project& project);
Json::Value VeDescribeCatalog(const std::vector<CatalogEntry>& entries);

// The complete ve.core v3 service-name table — what "ve.services"
// advertises and what DispatchExtHostCall implements (the dispatcher
// rejects anything outside this list with "unknown_call").
// v2 aliases ("project.clips", "status") are served but NOT listed.
const std::vector<std::string>& VeServiceNames();
bool VeServiceKnown(const std::string& name);

}  // namespace XER::VideoEditor
