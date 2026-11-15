// media_catalog.cc — see media_catalog.h for the contract. Follows
// node_graph_project_store.cc patterns: anonymous-namespace Value
// helpers, one Execute() per schema statement (SQLiteDatabase::Execute
// prepares a single statement only), all SQL values bound — never
// interpolated.
#include "media_catalog.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <json/json.h>

#include "clip_poster.h"
#include "content/video/video_info.h"
#include "helper/env_utils.h"
#include "helper/path_utils.h"
#include "sql/content/value.h"
#include "sql/storage/sqlite_database.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
namespace Video = XER::Content::Video;
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
// SQL::Content::Value has no bool alternative — has_audio is an INTEGER
// column, so booleans bind as 0/1 int64.
Value VBool(bool b) { return Value{static_cast<std::int64_t>(b ? 1 : 0)}; }

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
bool AsBool(const Value& v) { return AsInt(v) != 0; }

std::int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// NOTE: SQLiteDatabase::Execute prepares a single statement — a multi-
// statement script would silently run only the first CREATE TABLE. Keep
// every schema statement separate (same rule as ProjectStore).
constexpr const char* kSchemaStatements[] = {
    R"sql(CREATE TABLE IF NOT EXISTS media (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL COLLATE NOCASE,
    duration REAL NOT NULL DEFAULT 0,
    width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0,
    fps REAL NOT NULL DEFAULT 0,
    video_codec TEXT NOT NULL DEFAULT '',
    audio_codec TEXT NOT NULL DEFAULT '',
    has_audio INTEGER NOT NULL DEFAULT 0,
    file_size INTEGER NOT NULL DEFAULT 0,
    mtime INTEGER NOT NULL DEFAULT 0,
    probe_json TEXT NOT NULL DEFAULT '',
    poster_path TEXT NOT NULL DEFAULT '',
    added_at INTEGER NOT NULL DEFAULT 0
))sql",
    // NOCASE path dedup: the column collation makes both `path = ?`
    // lookups and this unique index ASCII case-insensitive, matching
    // the Windows case-insensitive filesystem ("D:/a.MP4" ==
    // "d:/a.mp4"). Non-ASCII case edges are accepted — NOCASE folds
    // ASCII only.
    "CREATE UNIQUE INDEX IF NOT EXISTS media_path_uq ON media(path)",
};

constexpr const char* kSelectColumns =
    "id, path, duration, width, height, fps, video_codec, audio_codec,"
    " has_audio, file_size, mtime, probe_json, poster_path, added_at";

// Canonical absolute UTF-8 form of a media path: NormalizeUtf8Path
// (legacy ANSI bytes -> UTF-8), FromUtf8 -> weakly_canonical (absolute,
// "." / ".." resolved; nonexistent tails stay normalized so ProbeVideo
// reports not-found), ToUtf8 back. weakly_canonical uses the
// error_code overload — a canonicalize failure falls back to absolute +
// lexical normalization rather than throwing.
bool CanonicalizeMediaPath(const std::string& utf8_path,
                           std::string* canon_out,
                           std::string* error_out) {
    const std::string normalized = Path::NormalizeUtf8Path(utf8_path);
    if (normalized.empty()) {
        return Fail(error_out, "empty media path");
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
                        "cannot resolve media path: " + utf8_path);
        }
        canon = canon.lexically_normal();
    }
    *canon_out = Path::ToUtf8(canon);
    return true;
}

// Serialized VideoInfo snapshot stored in probe_json — compact JSON,
// same writer settings as ProjectStore.
std::string ProbeJsonFor(const std::string& canon,
                         const Video::VideoInfo& info,
                         std::int64_t file_size) {
    Json::Value probe(Json::objectValue);
    probe["path"] = canon;
    probe["width"] = info.width;
    probe["height"] = info.height;
    probe["fps"] = info.fps;
    probe["duration_seconds"] = info.duration_seconds;
    probe["video_codec"] = info.video_codec;
    probe["audio_codec"] = info.audio_codec;
    probe["has_audio"] = info.has_audio;
    probe["file_size_bytes"] = static_cast<Json::Int64>(file_size);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, probe);
}

CatalogEntry EntryFromRow(const Row& row) {
    CatalogEntry e;
    if (row.columns.size() < 14) {
        return e;
    }
    e.id = AsInt(row.columns[0]);
    e.path = AsString(row.columns[1]);
    e.duration_seconds = AsReal(row.columns[2]);
    e.width = static_cast<int>(AsInt(row.columns[3]));
    e.height = static_cast<int>(AsInt(row.columns[4]));
    e.fps = AsReal(row.columns[5]);
    e.video_codec = AsString(row.columns[6]);
    e.audio_codec = AsString(row.columns[7]);
    e.has_audio = AsBool(row.columns[8]);
    e.file_size_bytes = AsInt(row.columns[9]);
    e.mtime_unix = AsInt(row.columns[10]);
    e.probe_json = AsString(row.columns[11]);
    e.poster_path = AsString(row.columns[12]);
    e.added_at_unix = AsInt(row.columns[13]);
    return e;
}

}  // namespace

MediaCatalog::MediaCatalog(std::string utf8_db_path)
    : db_path_(utf8_db_path.empty() ? DefaultDbPath()
                                    : std::move(utf8_db_path)) {}

MediaCatalog::~MediaCatalog() { Close(); }

void MediaCatalog::Close() { db_.reset(); }

bool MediaCatalog::IsOpen() const { return db_ != nullptr; }

std::string MediaCatalog::DefaultDbPath() {
    // UTF-8 safe env chain: LOCALAPPDATA arrives ANSI on non-EN Windows
    // -> NormalizeUtf8Path; XDG_DATA_HOME / HOME/.local/share cover
    // Linux/macOS; the temp dir is the last resort. Generic '/' separators
    // are appended — SQLite and Windows both accept them.
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
    return base + "/XER/VideoEditor/catalog.db";
}

bool MediaCatalog::Open(std::string* error_out) {
    Close();
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
                        "cannot create database directory " +
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

bool MediaCatalog::EnsureSchema(std::string* error_out) {
    for (const char* sql : kSchemaStatements) {
        if (!db_->Execute(sql, {}, error_out)) {
            return false;
        }
    }
    return true;
}

std::string MediaCatalog::PosterDir() const {
    return Path::ToUtf8(
        Path::FromUtf8(db_path_).parent_path() / "posters");
}

bool MediaCatalog::Import(const std::string& utf8_path,
                          std::int64_t* id_out,
                          bool extract_poster,
                          std::string* error_out) {
    if (!IsOpen()) {
        return Fail(error_out, "catalog is not open");
    }
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }

    Video::VideoInfo info;
    if (!Video::ProbeVideo(canon, &info, error_out)) {
        if (error_out != nullptr && error_out->empty()) {
            *error_out = "probe failed: " + canon;
        }
        return false;
    }
    const std::filesystem::path canon_fs = Path::FromUtf8(canon);

    std::int64_t mtime_unix = 0;
    {
        std::error_code ec;
        const auto ftime =
            std::filesystem::last_write_time(canon_fs, ec);
        if (!ec) {
            const auto sys =
                std::chrono::clock_cast<std::chrono::system_clock>(ftime);
            mtime_unix =
                std::chrono::duration_cast<std::chrono::seconds>(
                    sys.time_since_epoch())
                    .count();
        }
    }

    // ProbeVideo already fills file_size_bytes; fall back to a direct
    // stat when it could not (e.g. permission quirks on the probe path).
    std::int64_t file_size = info.file_size_bytes;
    if (file_size <= 0) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(canon_fs, ec);
        if (!ec) {
            file_size = static_cast<std::int64_t>(size);
        }
    }

    const std::string probe_json = ProbeJsonFor(canon, info, file_size);
    const std::int64_t now_unix = NowUnixSeconds();

    // Upsert by canonical path: refresh the probe fields on conflict,
    // keep added_at (and poster_path until re-extraction below).
    if (!db_->Execute(
            "UPDATE media SET duration = ?, width = ?, height = ?,"
            " fps = ?, video_codec = ?, audio_codec = ?, has_audio = ?,"
            " file_size = ?, mtime = ?, probe_json = ? WHERE path = ?",
            {VReal(info.duration_seconds), VInt(info.width),
             VInt(info.height), VReal(info.fps), VStr(info.video_codec),
             VStr(info.audio_codec), VBool(info.has_audio), VInt(file_size),
             VInt(mtime_unix), VStr(probe_json), VStr(canon)},
            error_out)) {
        return false;
    }
    if (db_->Changes() == 0) {
        if (!db_->Execute(
                "INSERT INTO media(path, duration, width, height, fps,"
                " video_codec, audio_codec, has_audio, file_size, mtime,"
                " probe_json, added_at)"
                " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                {VStr(canon), VReal(info.duration_seconds),
                 VInt(info.width), VInt(info.height), VReal(info.fps),
                 VStr(info.video_codec), VStr(info.audio_codec),
                 VBool(info.has_audio), VInt(file_size), VInt(mtime_unix),
                 VStr(probe_json), VInt(now_unix)},
                error_out)) {
            return false;
        }
    }

    std::vector<Row> id_rows;
    if (!db_->Query("SELECT id FROM media WHERE path = ?",
                    {VStr(canon)}, id_rows, error_out) ||
        id_rows.empty() || id_rows.front().columns.empty()) {
        return Fail(error_out, "catalog row missing after upsert: " + canon);
    }
    const std::int64_t id = AsInt(id_rows.front().columns.front());
    if (id <= 0) {
        return Fail(error_out, "catalog row has invalid id: " + canon);
    }

    // Poster extraction is best-effort and non-fatal: the row already
    // upserted, and poster_path keeps its previous/empty value on
    // failure. Only files with video get posters (ClipPoster rejects
    // audio-only anyway; the width>0 gate just skips the attempt).
    if (extract_poster && info.width > 0) {
        const double seek = info.duration_seconds > 0.0
                                ? info.duration_seconds * 0.5
                                : 0.0;
        const std::string poster_dir = PosterDir();
        {
            std::error_code ec;
            std::filesystem::create_directories(
                Path::FromUtf8(poster_dir), ec);
        }
        const std::string poster_path =
            poster_dir + "/" + std::to_string(id) + ".png";
        std::string poster_error;
        if (ClipPoster::Extract(canon, seek, poster_path, &poster_error)) {
            // Failure here is also non-fatal — the poster file exists
            // but the path simply will not be recorded.
            db_->Execute("UPDATE media SET poster_path = ? WHERE id = ?",
                         {VStr(poster_path), VInt(id)}, nullptr);
        }
    }

    if (id_out != nullptr) {
        *id_out = id;
    }
    return true;
}

bool MediaCatalog::FindByPath(const std::string& utf8_path,
                              CatalogEntry* out,
                              std::string* error_out) const {
    if (!IsOpen() || out == nullptr) {
        return Fail(error_out, "catalog is not open");
    }
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }
    std::vector<Row> rows;
    // `path = ?` compares under the column's COLLATE NOCASE.
    if (!db_->Query(
            std::string("SELECT ") + kSelectColumns +
                " FROM media WHERE path = ?",
            {VStr(canon)}, rows, error_out)) {
        return false;
    }
    if (rows.empty()) {
        return Fail(error_out, "media not found: " + canon);
    }
    *out = EntryFromRow(rows.front());
    return true;
}

bool MediaCatalog::FindById(std::int64_t id,
                            CatalogEntry* out,
                            std::string* error_out) const {
    if (!IsOpen() || out == nullptr) {
        return Fail(error_out, "catalog is not open");
    }
    std::vector<Row> rows;
    if (!db_->Query(
            std::string("SELECT ") + kSelectColumns +
                " FROM media WHERE id = ?",
            {VInt(id)}, rows, error_out)) {
        return false;
    }
    if (rows.empty()) {
        return Fail(error_out, "media not found: id " + std::to_string(id));
    }
    *out = EntryFromRow(rows.front());
    return true;
}

std::vector<CatalogEntry> MediaCatalog::List(std::string* error_out) const {
    std::vector<CatalogEntry> out;
    if (!IsOpen()) {
        Fail(error_out, "catalog is not open");
        return out;
    }
    std::vector<Row> rows;
    if (!db_->Query(
            std::string("SELECT ") + kSelectColumns +
                " FROM media ORDER BY id",
            {}, rows, error_out)) {
        return {};
    }
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(EntryFromRow(row));
    }
    return out;
}

bool MediaCatalog::Remove(std::int64_t id,
                          bool remove_poster_file,
                          std::string* error_out) {
    if (!IsOpen()) {
        return Fail(error_out, "catalog is not open");
    }
    // Read poster_path first so the file can be removed after the row
    // is gone (the path is not recoverable once the row is deleted).
    std::vector<Row> rows;
    if (!db_->Query("SELECT poster_path FROM media WHERE id = ?",
                    {VInt(id)}, rows, error_out)) {
        return false;
    }
    std::string poster_path;
    if (!rows.empty() && !rows.front().columns.empty()) {
        poster_path = AsString(rows.front().columns.front());
    }
    if (!db_->Execute("DELETE FROM media WHERE id = ?",
                      {VInt(id)}, error_out)) {
        return false;
    }
    if (remove_poster_file && !poster_path.empty()) {
        // Best-effort: a missing/locked poster file is not an error.
        std::error_code ec;
        std::filesystem::remove(Path::FromUtf8(poster_path), ec);
    }
    return true;
}

}  // namespace XER::VideoEditor
