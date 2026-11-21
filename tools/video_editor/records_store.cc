// records_store.cc — see records_store.h for the contract. Follows
// media_catalog.cc patterns: anonymous-namespace Value helpers, one
// Execute() per schema statement (SQLiteDatabase::Execute prepares a
// single statement only), all SQL values bound — never interpolated.
#include "records_store.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "helper/env_utils.h"
#include "helper/path_utils.h"
#include "sql/content/value.h"
#include "sql/storage/sqlite_database.h"

namespace XER::VideoEditor::Records {
namespace {

namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
using XER::SQL::Content::Value;
using XER::SQL::Storage::Row;
using XER::SQL::Storage::SQLiteDatabase;

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

Value VStr(const std::string& s) { return Value{s}; }
Value VInt(std::int64_t v) { return Value{v}; }
Value VReal(double v) { return Value{v}; }

std::string AsString(const Value& v) {
    return std::holds_alternative<std::string>(v) ? std::get<std::string>(v)
                                                  : std::string{};
}
std::int64_t AsInt(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) {
        return std::get<std::int64_t>(v);
    }
    if (std::holds_alternative<double>(v)) {
        return static_cast<std::int64_t>(std::get<double>(v));
    }
    return 0;
}
double AsReal(const Value& v) {
    if (std::holds_alternative<double>(v)) {
        return std::get<double>(v);
    }
    if (std::holds_alternative<std::int64_t>(v)) {
        return static_cast<double>(std::get<std::int64_t>(v));
    }
    return 0.0;
}

std::int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Canonical absolute UTF-8 form of a project path — identical rules to
// media_catalog.cc's CanonicalizeMediaPath (NormalizeUtf8Path ->
// weakly_canonical; nonexistent tails stay normalized so a recent
// whose project file was deleted still lists and reports not-found
// instead of disappearing).
bool CanonicalizeProjectPath(const std::string& utf8_path,
                             std::string* canon_out,
                             std::string* error_out) {
    const std::string normalized = Path::NormalizeUtf8Path(utf8_path);
    if (normalized.empty()) {
        return Fail(error_out, "empty project path");
    }
    std::error_code ec;
    std::filesystem::path canon =
        std::filesystem::weakly_canonical(Path::FromUtf8(normalized), ec);
    if (ec || canon.empty()) {
        canon = Path::FromUtf8(normalized);
        std::error_code aec;
        if (canon.is_relative()) {
            canon = std::filesystem::absolute(canon, aec);
        }
        if (aec) {
            return Fail(error_out,
                        "cannot resolve project path: " + utf8_path);
        }
        canon = canon.lexically_normal();
    }
    *canon_out = Path::ToUtf8(canon);
    return true;
}

// File mtime as unix seconds; 0 when the file is missing/unstattable
// (same file_clock -> system_clock conversion as media_catalog.cc —
// std::chrono::clock_cast is not implemented by libc++).
std::int64_t FileMtimeUnix(const std::filesystem::path& fs_path) {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(fs_path, ec);
    if (ec) {
        return 0;
    }
    const auto sys =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(
               sys.time_since_epoch())
        .count();
}

// Compact "H:MM:SS"/"M:SS" duration for the tile meta line — local so
// this module keeps zero dependencies on the editor core (under
// XER_YA_DLLS, YA.Records must not pull YA.VideoEditor-Core or the two
// modules would import each other).
std::string FormatDurationCompact(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return "0:00";
    }
    const long long total =
        static_cast<long long>(seconds + 0.5);
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;
    char buf[16];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    }
    return buf;
}

// Tile title when the row carries no display_name: the file stem
// ("promo_v2.xerproj" -> "promo_v2"), or the raw path for inputs that
// resolve to a bare root/trailing separator.
std::string DisplayNameFallback(const std::string& canon_path) {
    const std::string stem =
        Path::ToUtf8(Path::FromUtf8(canon_path).stem());
    return stem.empty() ? canon_path : stem;
}

// NOTE: SQLiteDatabase::Execute prepares a single statement — a multi-
// statement script would silently run only the first CREATE TABLE. Keep
// every schema statement separate (same rule as MediaCatalog).
//
// path is the canonical UTF-8 project path — PRIMARY KEY so
// INSERT OR REPLACE naturally upserts. NOCASE collation matches the
// Windows filesystem ("D:/a.xerproj" == "d:/a.xerproj"); non-ASCII
// case edges are accepted (NOCASE folds ASCII only).
constexpr const char* kSchemaStatements[] = {
    R"sql(CREATE TABLE IF NOT EXISTS recent_projects (
    path TEXT PRIMARY KEY COLLATE NOCASE,
    display_name TEXT NOT NULL DEFAULT '',
    last_opened INTEGER NOT NULL DEFAULT 0,
    last_modified INTEGER NOT NULL DEFAULT 0,
    duration_seconds REAL NOT NULL DEFAULT 0,
    poster_path TEXT NOT NULL DEFAULT ''
))sql",
};

constexpr const char* kSelectColumns =
    "path, display_name, last_opened, last_modified,"
    " duration_seconds, poster_path";

RecentProject FromRow(const Row& row) {
    RecentProject r;
    if (row.columns.size() < 6) {
        return r;
    }
    r.path = AsString(row.columns[0]);
    r.display_name = AsString(row.columns[1]);
    r.last_opened_unix = AsInt(row.columns[2]);
    r.last_modified_unix = AsInt(row.columns[3]);
    // A negative stored duration is the "unknown" sentinel (written by
    // an AddOrTouch that carried no duration) — report 0.
    r.duration_seconds = std::max(0.0, AsReal(row.columns[4]));
    r.poster_path = AsString(row.columns[5]);
    return r;
}

// Sentinel merge shared by AddOrTouch and UpdateMeta: `incoming` wins
// for display_name (non-empty), duration_seconds (>= 0) and
// poster_path (non-empty); `existing` keeps everything else.
void MergeMeta(RecentProject* into, const RecentProject& incoming) {
    if (!incoming.display_name.empty()) {
        into->display_name = incoming.display_name;
    }
    if (incoming.duration_seconds >= 0.0) {
        into->duration_seconds = incoming.duration_seconds;
    }
    if (!incoming.poster_path.empty()) {
        into->poster_path = incoming.poster_path;
    }
}

}  // namespace

RecentProjectsStore::RecentProjectsStore(std::string utf8_db_path)
    : db_path_(utf8_db_path.empty() ? DefaultDbPath()
                                    : std::move(utf8_db_path)) {}

RecentProjectsStore::~RecentProjectsStore() { Close(); }

bool RecentProjectsStore::IsOpen() const {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return db_ != nullptr;
}

void RecentProjectsStore::Close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    db_.reset();
}

std::string RecentProjectsStore::DefaultDbPath() {
    // Identical env chain to MediaCatalog::DefaultDbPath
    // (LOCALAPPDATA -> XDG_DATA_HOME -> HOME/.local/share -> temp) so
    // every piece of per-user editor state lands beside catalog.db.
    std::string base =
        Path::NormalizeUtf8Path(XER::Helper::GetEnv("LOCALAPPDATA"));
    if (base.empty()) {
        base = Path::NormalizeUtf8Path(XER::Helper::GetEnv("XDG_DATA_HOME"));
    }
    if (base.empty()) {
        const std::string home =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("HOME"));
        if (!home.empty()) {
            base = home + "/.local/share";
        }
    }
    if (base.empty()) {
        std::error_code ec;
        base = Path::ToUtf8(std::filesystem::temp_directory_path(ec));
    }
    if (base.empty()) {
        base = ".";
    }
    return base + "/XER/VideoEditor/records.db";
}

bool RecentProjectsStore::Open(std::string* error_out) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    db_.reset();
    if (db_path_.empty()) {
        db_path_ = DefaultDbPath();
    }
    if (db_path_.empty()) {
        return Fail(error_out, "empty database path");
    }
    const std::filesystem::path parent =
        Path::FromUtf8(db_path_).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return Fail(error_out,
                        "cannot create records directory " +
                            Path::ToUtf8(parent) + ": " + ec.message());
        }
    }
    db_ = SQLiteDatabase::Open(db_path_, error_out);
    if (db_ == nullptr) {
        return false;
    }
    if (!EnsureSchema(error_out)) {
        db_.reset();
        return false;
    }
    return true;
}

// db_mutex_ held (called from Open only). Migration contract:
// PRAGMA user_version holds the schema version; v0 (fresh db) runs the
// v1 statements then stamps the version. A database stamped NEWER than
// this build refuses to open — silently reading it could drop columns
// a future build owns.
bool RecentProjectsStore::EnsureSchema(std::string* error_out) {
    std::vector<Row> rows;
    if (!db_->Query("PRAGMA user_version", {}, rows, error_out) ||
        rows.empty() || rows[0].columns.empty()) {
        return Fail(error_out, "cannot read records schema version");
    }
    const std::int64_t version = AsInt(rows[0].columns[0]);
    if (version > kSchemaVersion) {
        return Fail(error_out,
                    "records database schema v" + std::to_string(version) +
                        " is newer than this build supports (v" +
                        std::to_string(kSchemaVersion) + ")");
    }
    for (const char* sql : kSchemaStatements) {
        if (!db_->Execute(sql, {}, error_out)) {
            return false;
        }
    }
    // PRAGMA can't take bound parameters — the version is a fixed
    // literal, never user data.
    if (version < kSchemaVersion &&
        !db_->Execute(
            "PRAGMA user_version = " + std::to_string(kSchemaVersion),
            {}, error_out)) {
        return false;
    }
    return true;
}

// db_mutex_ held.
bool RecentProjectsStore::ReadRowLocked(const std::string& canon_path,
                                        RecentProject* out) const {
    std::vector<Row> rows;
    if (!db_->Query(std::string("SELECT ") + kSelectColumns +
                        " FROM recent_projects WHERE path = ?",
                    {VStr(canon_path)}, rows, nullptr)) {
        return false;
    }
    if (rows.empty()) {
        return false;
    }
    *out = FromRow(rows.front());
    return true;
}

// db_mutex_ held. Recency order is (last_opened DESC, rowid DESC) —
// the rowid tiebreak keeps exact touch order when two touches land in
// the same wall-clock second (every write is a fresh rowid).
bool RecentProjectsStore::TrimLocked(std::string* error_out) {
    if (max_rows_ <= 0) {
        return true;
    }
    return db_->Execute(
        "DELETE FROM recent_projects WHERE rowid NOT IN"
        " (SELECT rowid FROM recent_projects"
        "  ORDER BY last_opened DESC, rowid DESC LIMIT ?)",
        {VInt(max_rows_)}, error_out);
}

bool RecentProjectsStore::AddOrTouch(const std::string& utf8_path,
                                     const std::string& display_name,
                                     double duration_seconds,
                                     const std::string& poster_path,
                                     std::string* error_out) {
    // Canonicalize + stat run outside the DB lock — only the SQL below
    // serializes on db_mutex_ (same split as MediaCatalog::Import).
    std::string canon;
    if (!CanonicalizeProjectPath(utf8_path, &canon, error_out)) {
        return false;
    }
    const std::int64_t mtime =
        FileMtimeUnix(Path::FromUtf8(canon));

    RecentProject row;
    row.path = canon;
    row.display_name =
        display_name.empty() ? DisplayNameFallback(canon) : display_name;
    row.duration_seconds = duration_seconds;
    row.poster_path = poster_path;

    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return Fail(error_out, "records store is not open");
    }
    RecentProject merged;
    merged.path = canon;
    if (ReadRowLocked(canon, &merged)) {
        // Existing row: the touch refreshes last_opened (and the file
        // mtime when statable); display/duration/poster merge
        // sentinel-aware so a bare touch never erases metadata.
        if (mtime > 0) {
            merged.last_modified_unix = mtime;
        }
    } else {
        merged.last_modified_unix = mtime;
    }
    merged.last_opened_unix = NowUnixSeconds();
    MergeMeta(&merged, row);
    // INSERT OR REPLACE so a touch gets a fresh rowid — the List()
    // tiebreak then orders strictly by touch, even within one second.
    if (!db_->Execute(
            "INSERT OR REPLACE INTO recent_projects"
            " (path, display_name, last_opened, last_modified,"
            "  duration_seconds, poster_path)"
            " VALUES (?, ?, ?, ?, ?, ?)",
            {VStr(merged.path), VStr(merged.display_name),
             VInt(merged.last_opened_unix),
             VInt(merged.last_modified_unix),
             VReal(merged.duration_seconds),
             VStr(merged.poster_path)},
            error_out)) {
        return false;
    }
    return TrimLocked(error_out);
}

std::vector<RecentProject> RecentProjectsStore::List(
    int limit, std::string* error_out) const {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        Fail(error_out, "records store is not open");
        return {};
    }
    int cap = limit;
    if (cap <= 0 || cap > kHardListCap) {
        cap = kHardListCap;
    }
    std::vector<Row> rows;
    if (!db_->Query(std::string("SELECT ") + kSelectColumns +
                        " FROM recent_projects"
                        " ORDER BY last_opened DESC, rowid DESC LIMIT ?",
                    {VInt(cap)}, rows, error_out)) {
        return {};
    }
    std::vector<RecentProject> out;
    out.reserve(rows.size());
    for (const Row& row : rows) {
        RecentProject rec = FromRow(row);
        if (!rec.path.empty()) {
            out.push_back(std::move(rec));
        }
    }
    return out;
}

bool RecentProjectsStore::Remove(const std::string& utf8_path,
                                 std::string* error_out) {
    std::string canon;
    if (!CanonicalizeProjectPath(utf8_path, &canon, error_out)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return Fail(error_out, "records store is not open");
    }
    return db_->Execute("DELETE FROM recent_projects WHERE path = ?",
                        {VStr(canon)}, error_out);
}

bool RecentProjectsStore::UpdateMeta(const RecentProject& patch,
                                     std::string* error_out) {
    std::string canon;
    if (!CanonicalizeProjectPath(patch.path, &canon, error_out)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return Fail(error_out, "records store is not open");
    }
    RecentProject merged;
    if (ReadRowLocked(canon, &merged)) {
        // Existing row: a plain UPDATE so the rowid — the recency
        // tiebreak — survives untouched. patch.last_opened /
        // last_modified are deliberately ignored (documented on the
        // header): a metadata write must never rewrite ordering.
        RecentProject incoming = patch;
        incoming.path = canon;
        MergeMeta(&merged, incoming);
        return db_->Execute(
            "UPDATE recent_projects"
            " SET display_name = ?, duration_seconds = ?,"
            "     poster_path = ?"
            " WHERE path = ?",
            {VStr(merged.display_name),
             VReal(std::max(0.0, merged.duration_seconds)),
             VStr(merged.poster_path), VStr(canon)},
            error_out);
    }
    // Upsert for a path with no row yet: seed the touch defaults
    // UpdateMeta can't infer (recency fields — last_opened now, file
    // mtime when statable), then merge the patch.
    merged.path = canon;
    merged.last_opened_unix = NowUnixSeconds();
    merged.last_modified_unix = FileMtimeUnix(Path::FromUtf8(canon));
    RecentProject incoming = patch;
    incoming.path = canon;
    MergeMeta(&merged, incoming);
    if (merged.display_name.empty()) {
        merged.display_name = DisplayNameFallback(canon);
    }
    if (merged.duration_seconds < 0.0) {
        merged.duration_seconds = 0.0;
    }
    return db_->Execute(
        "INSERT OR REPLACE INTO recent_projects"
        " (path, display_name, last_opened, last_modified,"
        "  duration_seconds, poster_path)"
        " VALUES (?, ?, ?, ?, ?, ?)",
        {VStr(merged.path), VStr(merged.display_name),
         VInt(merged.last_opened_unix),
         VInt(merged.last_modified_unix),
         VReal(merged.duration_seconds),
         VStr(merged.poster_path)},
        error_out);
}

// ------------------------------------------------------------------
// Display helpers
// ------------------------------------------------------------------

std::string FormatTimeAgo(std::int64_t unix_seconds,
                          std::int64_t now_unix) {
    if (unix_seconds <= 0) {
        return {};
    }
    // Clock skew across machines (file server, manual edits) clamps to
    // "just now" rather than printing a negative interval.
    const std::int64_t diff = now_unix - unix_seconds;
    if (diff < 60) {
        return "just now";
    }
    const std::int64_t minutes = diff / 60;
    if (minutes < 60) {
        return std::to_string(minutes) +
               (minutes == 1 ? " minute ago" : " minutes ago");
    }
    const std::int64_t hours = diff / 3600;
    if (hours < 24) {
        return std::to_string(hours) +
               (hours == 1 ? " hour ago" : " hours ago");
    }
    const std::int64_t days = diff / 86400;
    if (days < 2) {
        return "yesterday";
    }
    if (days < 7) {
        return std::to_string(days) + " days ago";
    }
    // Older than a week -> absolute date (local time). The platform
    // thread-safe variants are chosen at compile time; tm copy is
    // guarded for the rare null return on out-of-range input.
    std::tm tm{};
    const std::time_t when = static_cast<std::time_t>(unix_seconds);
#if defined(_WIN32)
    const bool ok = localtime_s(&tm, &when) == 0;
#else
    const bool ok = localtime_r(&when, &tm) != nullptr;
#endif
    if (!ok) {
        return "a while ago";
    }
    char buf[16];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm) == 0) {
        return "a while ago";
    }
    return std::string(buf);
}

RecentTile MakeRecentTile(const RecentProject& rec,
                          std::int64_t now_unix) {
    RecentTile tile;
    tile.path = rec.path;
    tile.title = rec.display_name.empty()
                     ? DisplayNameFallback(rec.path)
                     : rec.display_name;
    tile.poster_path = rec.poster_path;
    tile.meta_line = FormatTimeAgo(rec.last_opened_unix, now_unix);
    if (rec.duration_seconds > 0.0) {
        const std::string duration =
            FormatDurationCompact(rec.duration_seconds);
        tile.meta_line += tile.meta_line.empty() ? duration
                                                 : " \xc2\xb7 " + duration;
    }
    return tile;
}

}  // namespace XER::VideoEditor::Records
