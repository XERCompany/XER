// LUT manager: scans the editor's LUT search dirs for .cube/.3dl/.look
// files, parses the minimal .cube header needed to name/describe each
// look, and publishes the results as named OCIO profiles — the color
// panel's LOOKS section and the compositor's `ocio:<name>` clip effect
// both resolve through Color::SharedOcioProfiles(), so registering here
// keeps ONE source of truth for look application (no second apply path).
//
// UI-free core module (tools/video_editor top level): the smoke checks
// exercise the .cube parser and scan bookkeeping without a window.
//
// Search dirs (highest priority first; first name wins on ties):
//   1. <project_dir>/luts            — set via SetProjectDir ("" when
//                                      the project is unsaved)
//   2. <assets_dir>/luts             — AppRuntime assets (deployed
//                                      color.yaml already points here)
//   3. <exe_dir>/assets/luts and <exe_dir>/luts — fallback when the
//                                      assets layer did not boot
//   4. <user_data>/luts              — per-user dir derived from the
//                                      catalog's DefaultDbPath env chain
//     (LOCALAPPDATA/XER/VideoEditor on Windows)
//
// A scanned .cube contributes TITLE + LUT_3D_SIZE (parsed, cached);
// .3dl/.look files list by filename stem (OCIO parses them at apply
// time). Parsed cubes are memoized by path so a rescan only re-reads
// changed files.
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace XER::VideoEditor::Lut {

// Parsed .cube contents — enough to name (TITLE), describe (size) and
// sanity-check a LUT. The file itself stays on disk; OCIO's
// FileTransform does the real apply.
struct CubeData {
    std::string title;
    int size = 0;               // LUT_3D_SIZE / LUT_1D_SIZE
    bool is_3d = true;
    // Row-major triplets; expected count is size^3 (3D) or size (1D).
    std::vector<float> data;
};

// Minimal .cube parser: TITLE, LUT_3D_SIZE / LUT_1D_SIZE, DOMAIN_*
// (accepted, ignored) and float data rows; '#' starts a comment.
// Returns false (out untouched) when the text declares no LUT size or
// carries zero data rows — everything else is tolerated.
bool ParseCubeText(const std::string& text, CubeData* out,
                   std::string* error = nullptr);
bool ParseCubeFile(const std::filesystem::path& path, CubeData* out,
                   std::string* error = nullptr);

// One discovered LUT file.
struct Entry {
    std::string name;        // TITLE (.cube) or filename stem
    std::string path;        // UTF-8 absolute path to the LUT file
    std::string format;      // "cube" | "3dl" | "look"
    int size = 0;            // 3D size (0 = 1D/unknown)
    bool is_3d = true;
    std::string source_dir;  // UTF-8 dir the file was found in
};

class Manager {
public:
    static Manager& Shared();

    // The directory holding the open .xerproj — its `luts` subdir joins
    // the scan at the highest priority. Empty clears the slot.
    void SetProjectDir(const std::filesystem::path& dir);

    // Every directory the next Rescan() would search (deduped,
    // existing-or-not — callers use this for diagnostics).
    std::vector<std::filesystem::path> SearchDirs() const;

    // Rescans all search dirs and re-syncs SharedOcioProfiles():
    // entries previously registered by the manager are removed, then
    // the fresh list is appended as LUT-backed OcioProfiles (skipping
    // names a profile already owns — color.yaml keeps precedence).
    // Returns the number of LUT files found.
    std::size_t Rescan();

    const std::vector<Entry>& Entries() const { return entries_; }
    // Matches a scanned entry by LUT path — accepts either separator
    // style; nullptr when the path is not a scanned LUT (e.g. a
    // color.yaml-relative one).
    const Entry* FindByPath(const std::string& utf8_path) const;
    // Parsed .cube data for a scanned entry — memoized by path;
    // nullptr for non-cube formats or a failed parse.
    const CubeData* CubeFor(const std::string& utf8_path);

private:
    Manager() = default;

    // Scans one directory non-recursively for .cube/.3dl/.look files,
    // appending entries sorted by filename. `used_names` carries the
    // already-claimed display names across dirs (first dir wins).
    // Appends to `out` without touching members — callers run the scan
    // unlocked and publish afterwards (CubeFor locks internally).
    void ScanDir(const std::filesystem::path& dir,
                 std::unordered_set<std::string>* used_names,
                 std::vector<Entry>* out);

    std::vector<Entry> entries_;
    // lut_path values the manager last pushed into
    // SharedOcioProfiles() — absolute UTF-8 paths; Rescan() erases
    // exactly these before appending the fresh set so yaml-owned and
    // other registrations are never touched.
    std::unordered_set<std::string> registered_paths_;
    std::filesystem::path project_dir_;
    // Parsed-cube cache keyed by UTF-8 path; entries hold nullptr for
    // files that failed to parse (cached negatives — a bad cube does
    // not re-read every rescan).
    std::unordered_map<std::string, std::shared_ptr<CubeData>> cubes_;
    mutable std::mutex mutex_;
};

}  // namespace XER::VideoEditor::Lut
