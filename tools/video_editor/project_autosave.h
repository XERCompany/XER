// Premiere-style project autosave/recovery for the VideoEditor core
// (XER Studio Phase 2). UI-free; all paths are UTF-8 std::strings.
//
// The manager is bound to a project path + autosave directory by
// Init(). MaybeAutosave() writes a rotating set of up to
// kMaxAutosaves copies named <basename>.autosave-N.xerproj inside the
// autosave directory — N=1 is always the newest; on every rotation the
// previous slots shift up (1->2, 2->3, ...) and the oldest slot is
// dropped — but only when BOTH conditions hold:
//   * the dirty flag is set (MarkDirty()), and
//   * at least IntervalMs() has elapsed since the last successful
//     autosave (the first-ever autosave has no previous timestamp and
//     is always due once dirty).
// A gated call is a success no-op (returns true); false is reserved
// for real failures (not initialized, rotation/save I/O errors).
//
// Recovery enumerates the slots newest-first via ListAutosaves() and
// reloads through ProjectStore (crash-safe v2 .xerproj). LoadLatest
// falls back to older slots when the newest copy is unreadable.
// Never throws: every I/O failure returns false with error_out set.
// Single-threaded like the timeline model — call from the UI thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "timeline_model.h"

namespace XER::VideoEditor {

class AutosaveManager {
public:
    static constexpr int kMaxAutosaves = 5;
    static constexpr std::int64_t kDefaultIntervalMs = 120 * 1000;

    AutosaveManager() = default;

    // Binds the manager to `utf8_project_path` (only its filename stem
    // is used for slot naming) and `utf8_autosave_dir` (created if
    // missing). Resets the dirty flag and the interval timer; the
    // configured interval is kept across re-Init. Fails on an empty
    // path or when the directory cannot be created.
    bool Init(const std::string& utf8_project_path,
              const std::string& utf8_autosave_dir,
              std::string* error_out = nullptr);
    bool IsInitialized() const { return initialized_; }
    const std::string& ProjectPath() const { return project_path_; }
    const std::string& AutosaveDir() const { return autosave_dir_; }

    // Interval between autosaves. <= 0 means "always due".
    void SetIntervalMs(std::int64_t interval_ms) { interval_ms_ = interval_ms; }
    std::int64_t IntervalMs() const { return interval_ms_; }

    // Call after every project mutation; cleared by a successful
    // autosave (or a successful Load of the primary file, which the
    // caller can mirror with ClearDirty()).
    void MarkDirty() { dirty_ = true; }
    void ClearDirty() { dirty_ = false; }
    bool IsDirty() const { return dirty_; }

    // Writes <autosave_dir>/<basename>.autosave-1.xerproj after
    // rotating existing slots (oldest dropped) when dirty AND due —
    // see header comment. Returns true when nothing was written
    // because the write was gated; false + error_out only on real
    // failures. A failed save keeps the dirty flag so the next due
    // call retries. `now_ms` is caller-supplied (any monotonic clock).
    bool MaybeAutosave(const Project& project, std::int64_t now_ms,
                       std::string* error_out = nullptr);

    // Existing autosave slots for the bound project, newest first
    // (autosave-1 first). Empty when uninitialized or none exist.
    std::vector<std::string> ListAutosaves() const;
    // Static form for callers without a bound manager (e.g. a
    // recovery prompt before a project is opened).
    static std::vector<std::string> ListAutosaves(
        const std::string& utf8_project_path,
        const std::string& utf8_autosave_dir);

    // Loads the newest readable autosave into `project_out` via
    // ProjectStore::Load — falls back to older slots when the newest
    // copy is corrupt, returning the first slot that loads. Fails with
    // error_out when no slot exists or none loads. Replaces
    // `project_out`'s ENTIRE contents (same history caveat as
    // ProjectStore::Load: clear/rebuild TimelineHistory around it).
    bool LoadLatest(Project* project_out,
                    std::string* error_out = nullptr) const;
    static bool LoadLatest(const std::string& utf8_project_path,
                           const std::string& utf8_autosave_dir,
                           Project* project_out,
                           std::string* error_out = nullptr);

    // Full UTF-8 path of slot `slot` (1..kMaxAutosaves) for a project —
    // exposed for tests, diagnostics, and the recovery picker UI.
    static std::string AutosavePathFor(
        const std::string& utf8_project_path,
        const std::string& utf8_autosave_dir, int slot);

private:
    std::string project_path_;
    std::string autosave_dir_;
    std::int64_t interval_ms_ = kDefaultIntervalMs;
    std::int64_t last_save_ms_ = 0;
    bool has_saved_ = false;   // no previous autosave -> always due
    bool dirty_ = false;
    bool initialized_ = false;
};

}  // namespace XER::VideoEditor
