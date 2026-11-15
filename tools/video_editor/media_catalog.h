// SQLite-backed media catalog for the VideoEditor core (XER Studio
// Phase 1B). Maps probed media files (XER::Content::Video::ProbeVideo)
// to stable row ids and manages poster PNGs next to the database.
// Static paths are UTF-8 std::strings; the DB file itself is opened
// with the UTF-8 path directly (SQLite treats filenames as UTF-8).
//
// Concurrency contract: one SQLite connection, UI-thread-owned — NOT
// internally synchronized (callers serialize, same contract as
// NodeGraph::ProjectStore).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::SQL::Storage {
class SQLiteDatabase;
}

namespace XER::VideoEditor {

struct CatalogEntry {
    std::int64_t id = 0;
    std::string path;                 // canonical UTF-8 absolute
    double duration_seconds = 0;
    int width = 0, height = 0;        // 0/0 = audio-only
    double fps = 0;
    std::string video_codec, audio_codec;
    bool has_audio = false;
    std::int64_t file_size_bytes = 0;
    std::int64_t mtime_unix = 0;
    std::string probe_json;           // serialized VideoInfo fields
    std::string poster_path;          // UTF-8, empty when none
    std::int64_t added_at_unix = 0;
    bool HasVideo() const { return width > 0 && height > 0; }
};

class MediaCatalog {
public:
    // utf8_db_path empty → DefaultDbPath(). Single connection,
    // UI-thread-owned — NOT internally synchronized (callers serialize,
    // same contract as NodeGraph::ProjectStore).
    explicit MediaCatalog(std::string utf8_db_path = {});
    ~MediaCatalog();
    MediaCatalog(const MediaCatalog&) = delete;
    MediaCatalog& operator=(const MediaCatalog&) = delete;

    bool Open(std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const;
    const std::string& DbPath() const { return db_path_; }

    // Posters live in <db_dir>/posters/<id>.png — "next to the DB".
    static std::string DefaultDbPath();     // see env chain in .cc

    // Probe + upsert by canonical path. extract_poster && width>0 →
    // ClipPoster::Extract at duration*0.5 into posters/<id>.png and
    // store poster_path; poster failure is NON-fatal (row still
    // upserts, poster_path keeps its previous/empty value).
    bool Import(const std::string& utf8_path, std::int64_t* id_out = nullptr,
                bool extract_poster = true, std::string* error_out = nullptr);

    bool FindByPath(const std::string& utf8_path, CatalogEntry* out,
                    std::string* error_out = nullptr) const;
    bool FindById(std::int64_t id, CatalogEntry* out,
                  std::string* error_out = nullptr) const;
    std::vector<CatalogEntry> List(std::string* error_out = nullptr) const;
    // Deletes the row; best-effort deletes the poster file when
    // remove_poster_file (default true).
    bool Remove(std::int64_t id, bool remove_poster_file = true,
                std::string* error_out = nullptr);

private:
    bool EnsureSchema(std::string* error_out);
    std::string PosterDir() const;
    std::string db_path_;
    std::unique_ptr<XER::SQL::Storage::SQLiteDatabase> db_;
};

}  // namespace XER::VideoEditor
