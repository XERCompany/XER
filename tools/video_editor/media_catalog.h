// SQLite-backed media catalog for the VideoEditor core (XER Studio
// Phase 1B). Maps probed media files (XER::Content::Video::ProbeVideo)
// to stable row ids and manages poster PNGs next to the database.
// Static paths are UTF-8 std::strings; the DB file itself is opened
// with the UTF-8 path directly (SQLite treats filenames as UTF-8).
//
// Concurrency contract: one SQLite connection serialized internally
// by db_mutex_ — worker threads (media_prep) may call GetAudioPeaks /
// ExtractPoster / Import while the UI thread queries. The expensive
// parts (probe, peaks decode, poster extraction) run OUTSIDE the
// lock; only the individual SQL statements are serialized, so a
// long-running decode never blocks the UI.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace video {
class Frame;  // src/video/frame.h — out-param only, forward-declared
}

namespace XER::SQL::Storage {
class SQLiteDatabase;
}

namespace XER::VideoEditor {

// Still-image duration conventions, shared by the catalog import path
// and the UI's probe fallback: FFmpeg's image2 demuxer reports an
// imported still (PNG/JPEG/WebP/...) as a "video" holding one frame at
// a nominal ~40 ms duration, and the OIIO still fallback has no
// intrinsic duration at all. Either way a still gets the
// Premiere-style 5 s editor default; any probed audio-free "video"
// at or under kProbedStillMaxSeconds is treated as a still — a real
// clip always reports a longer duration.
inline constexpr double kStillDefaultSeconds = 5.0;
inline constexpr double kProbedStillMaxSeconds = 0.25;

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
    // Timeline classification rule — any imported file lands on
    // exactly one track kind:
    //   HasVideo() == true   -> TrackKind::Video. Includes stills:
    //                          OIIO-decoded rasters and FFmpeg's
    //                          single-frame image containers both
    //                          store w/h > 0.
    //   HasVideo() == false  -> TrackKind::Audio. Import guarantees
    //                          has_audio on such rows (zero-stream
    //                          probes are rejected), and cover art
    //                          never reaches this field — ProbeVideo
    //                          skips attached-picture streams, so a
    //                          tagged mp3 stays audio.
    bool HasVideo() const { return width > 0 && height > 0; }
    bool HasAudio() const { return has_audio; }
};

// Decoded waveform summary for one media file (Phase 2 spec §5):
// per-channel min/max float pairs bucketed at a fixed
// samples-per-peak resolution — the UI draws clip waveforms straight
// from `data` without touching the decoder.
struct AudioPeaks {
    int channels = 0;
    int samples_per_peak = 256;
    // Channel-major layout: channel c, bucket b occupies
    // data[c * buckets * 2 + b * 2 + {0=min, 1=max}]; every channel
    // carries exactly buckets * 2 floats (total channels*buckets*2).
    std::vector<float> data;
    std::size_t buckets = 0;
};

// Whole-file audio decode → peaks, no catalog needed (the
// --media-worker subprocess path; GetAudioPeaks wraps it with the
// on-disk cache).
bool DecodeAudioPeaks(const std::string& canon_path, AudioPeaks* out,
                      std::string* error_out = nullptr);

class MediaCatalog {
public:
    // utf8_db_path empty → DefaultDbPath(). Single connection —
    // internally serialized by db_mutex_ (safe to call from the
    // media-prep workers while the UI thread queries).
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

    // Waveform peaks for one media file (Phase 2 spec §5), cached in
    // the `peaks` table keyed by canonical path + file mtime. On a
    // cache miss — or when the file's mtime changed since the cached
    // row was written — the audio stream is decoded through the same
    // VideoDecoder audio-tap pipeline the mixer uses and the row is
    // upserted. SYNCHRONOUS and expensive on a miss: decoding a full
    // stream costs roughly realtime-length I/O + decode, so the decode
    // is capped at the first 10 minutes of audio to bound the cost;
    // call from a worker, not the UI thread (media_prep does). The DB
    // lock is released while decoding — only the row read/upsert
    // serialize. Returns false (nothing cached, `out` untouched) for a
    // missing file, a file with no audio stream, or a decode failure.
    bool GetAudioPeaks(const std::string& utf8_path, AudioPeaks* out,
                       std::string* error_out = nullptr);

    // Poster (re)extraction for an ALREADY-IMPORTED row — the async
    // half of Import(): resolves the row by path, writes
    // posters/<id>.png at duration*0.5 (OIIO for still images,
    // ClipPoster::Extract otherwise) and updates poster_path.
    // Best-effort like Import's poster step: returns false and leaves
    // the row untouched when the media has no video or decode fails.
    // The expensive extraction runs outside the DB lock; safe to call
    // from the media-prep worker pool.
    bool ExtractPoster(const std::string& utf8_path,
                       std::string* error_out = nullptr);

    // Poster frame at an arbitrary source time (Phase 2 spec §5) —
    // canonicalizes the path then forwards to ClipPoster::GetPosterAt:
    // the decoded BGRA frame nearest `seconds` (thumbnail-sized —
    // ClipPoster caps the long edge at 320 px so the byte-bounded
    // PosterCache holds hundreds), or the mid-stream frame when
    // `seconds` is out of range. Reads no tables and writes no
    // poster files; requires no open catalog (static).
    static bool GetPosterAt(const std::string& utf8_path,
                            double seconds, video::Frame* frame_out,
                            std::string* error_out = nullptr);

private:
    // db_mutex_ held (called from Open only).
    bool EnsureSchema(std::string* error_out);
    bool IsOpenLocked() const { return db_ != nullptr; }
    std::string PosterDir() const;
    std::string db_path_;
    // Serializes every statement on the single connection — see the
    // file-top concurrency contract.
    mutable std::mutex db_mutex_;
    std::unique_ptr<XER::SQL::Storage::SQLiteDatabase> db_;
};

}  // namespace XER::VideoEditor
