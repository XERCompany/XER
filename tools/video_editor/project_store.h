// .xerproj JSON v2 persistence for the VideoEditor timeline model
// (XER Studio Phase 1B). Static and UI-free; all paths are UTF-8
// std::strings. Save is crash-safe: the JSON is written to
// <path>.tmp and then renamed over the target, so a crash mid-write
// never leaves a truncated .xerproj.
#pragma once

#include <string>

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

}  // namespace XER::VideoEditor
