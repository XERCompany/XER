// Typed record store for the VideoEditor's "recent projects" start
// screen (XER Studio). UI-free like media_catalog.h: one SQLite
// connection behind a mutex, all paths UTF-8 std::strings, every API
// returns false + error_out on failure — never throws.
//
// Placement: this lives at the app level next to project_store /
// media_catalog because the data is video_editor-specific (recent
// .xerproj rows). Under XER_YA_DLLS the TU is packaged as its own
// branded module — YA.Records.dll (see tools/video_editor/
// CMakeLists.txt) — matching the YA.* module convention; non-YA
// builds compile it straight into XER_VideoEditor_Core.
//
// Concurrency contract mirrors MediaCatalog: a single connection
// serialized internally by db_mutex_; the UI thread owns the one
// instance (no global singleton — the app widget holds it).
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace XER::SQL::Storage {
class SQLiteDatabase;
}

namespace XER::VideoEditor::Records {

// One row of the recent_projects table: everything a start-screen
// tile needs without touching the project file again.
struct RecentProject {
    std::string path;                  // canonical UTF-8 absolute; PK
    std::string display_name;          // tile title (file stem fallback)
    std::int64_t last_opened_unix = 0; // last AddOrTouch time
    std::int64_t last_modified_unix = 0;  // file mtime at last touch
    double duration_seconds = 0.0;     // timeline duration snapshot
    std::string poster_path;           // UTF-8 poster PNG, "" when none
};

// SQLite-backed store. `utf8_db_path` empty -> DefaultDbPath()
// (<user_data>/XER/VideoEditor/records.db — same env chain the media
// catalog uses, so every piece of per-user editor state lands in one
// directory).
class RecentProjectsStore {
public:
    // Bumped whenever the schema changes; EnsureSchema migrates
    // forward via PRAGMA user_version. v1 is the initial table.
    static constexpr int kSchemaVersion = 1;
    static constexpr int kDefaultLimit = 24;

    explicit RecentProjectsStore(std::string utf8_db_path = {});
    ~RecentProjectsStore();
    RecentProjectsStore(const RecentProjectsStore&) = delete;
    RecentProjectsStore& operator=(const RecentProjectsStore&) = delete;

    bool Open(std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const;
    const std::string& DbPath() const { return db_path_; }

    // LOCALAPPDATA -> XDG_DATA_HOME -> HOME/.local/share -> temp,
    // then "/XER/VideoEditor/records.db" (identical chain to
    // MediaCatalog::DefaultDbPath — keep them in sync).
    static std::string DefaultDbPath();

    // Insert-or-touch keyed on the canonical project path:
    // last_opened := now, last_modified := the file's mtime when
    // statable. display_name falls back to the file stem. Sentinel
    // fields keep the stored value on update — duration_seconds < 0
    // and an empty poster_path are "no change" (a fresh insert
    // normalizes them to 0/""). Afterwards the table is trimmed to
    // MaxRows() in recency order.
    bool AddOrTouch(const std::string& utf8_path,
                    const std::string& display_name = {},
                    double duration_seconds = -1.0,
                    const std::string& poster_path = {},
                    std::string* error_out = nullptr);

    // Most-recently-touched first. limit <= 0 returns every row;
    // values above kHardListCap clamp.
    std::vector<RecentProject> List(int limit = kDefaultLimit,
                                    std::string* error_out = nullptr) const;

    // Deletes the row for `utf8_path` (canonicalized the same way).
    // Succeeds silently when nothing matched.
    bool Remove(const std::string& utf8_path,
                std::string* error_out = nullptr);

    // Partial metadata update: `patch.path` addresses the row, the
    // sentinel fields behave like AddOrTouch (empty display_name /
    // duration < 0 / empty poster_path keep stored values), and
    // last_opened/last_modified are always carried over from the
    // existing row — UpdateMeta never perturbs recency order. Upserts
    // when the path has no row yet.
    bool UpdateMeta(const RecentProject& patch,
                    std::string* error_out = nullptr);

    // Row cap enforced by AddOrTouch (oldest first). <= 0 disables
    // trimming. Default kDefaultLimit.
    void SetMaxRows(int max_rows) { max_rows_ = max_rows; }
    int MaxRows() const { return max_rows_; }

    static constexpr int kHardListCap = 1000;

private:
    // db_mutex_ held (called from Open only).
    bool EnsureSchema(std::string* error_out);
    // db_mutex_ held — reads one row; false when absent/unreadable.
    bool ReadRowLocked(const std::string& canon_path,
                       RecentProject* out) const;
    // db_mutex_ held — drops rows past max_rows_ in recency order.
    bool TrimLocked(std::string* error_out);

    std::string db_path_;
    int max_rows_ = kDefaultLimit;
    mutable std::mutex db_mutex_;
    std::unique_ptr<XER::SQL::Storage::SQLiteDatabase> db_;
};

// ---- headless display helpers (shared by the UI + --smoke) ---------

// "x minutes ago"-style recency label: sub-minute "just now",
// minutes to 45, hours to 36 ("yesterday" inside the day band),
// days to a week, then an absolute "YYYY-MM-DD" date. Deterministic —
// the caller passes `now_unix` explicitly (tests inject fixed times).
std::string FormatTimeAgo(std::int64_t unix_seconds,
                          std::int64_t now_unix);

// Tile view-model the ProjectManagerScreen grid renders per recent
// row — kept in the core so --smoke verifies the label math without a
// window. `meta_line` is "<time ago> · <duration>" (duration part
// omitted when the snapshot carries none).
struct RecentTile {
    std::string path;
    std::string title;
    std::string meta_line;
    std::string poster_path;
};
RecentTile MakeRecentTile(const RecentProject& rec, std::int64_t now_unix);

}  // namespace XER::VideoEditor::Records
