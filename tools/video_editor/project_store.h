// .xerproj JSON v1 persistence for the VideoEditor timeline model
// (XER Studio Phase 1B). Static and UI-free; all paths are UTF-8
// std::strings. Save is crash-safe: the JSON is written to
// <path>.tmp and then renamed over the target, so a crash mid-write
// never leaves a truncated .xerproj.
#pragma once

#include <string>

#include "timeline_model.h"

namespace XER::VideoEditor {

// Loads/saves Project as .xerproj JSON (format version 1 — key layout
// per docs/superpowers/specs/2026-11-15-video-editor-design.md).
// Load is lenient about absent optional fields and self-heals
// transitions that reference missing/non-adjacent clips via
// DropInvalidTransitions; it errors on absent required fields, wrong
// JSON types, duplicate clip ids, out-of-range track indices, and
// negative times/durations. Never throws.
class ProjectStore {
public:
    static constexpr int kVersion = 1;

    // UTF-8 paths. Save writes <path>.tmp then renames (crash-safe);
    // on Windows rename-over-existing may need remove+retry — handled
    // internally with std::error_code, never throws.
    static bool Save(const Project& project, const std::string& utf8_path,
                     std::string* error_out = nullptr);
    static bool Load(const std::string& utf8_path, Project* project_out,
                     std::string* error_out = nullptr);
};

}  // namespace XER::VideoEditor
