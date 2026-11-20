// Bounded worker pool for cold media decode (XER Studio Phase 2).
// The timeline's waveform/poster hooks and the import flow all need
// FFmpeg work that media_catalog.cc explicitly documents as "call
// from a worker, not the UI thread" — MediaPrep is that worker: a
// small pool consuming deduplicated requests, publishing decoded
// posters into a revived PosterCache and waveform peaks into a
// bounded in-memory store.
//
//   MediaPrep prep(&catalog);
//   // UI thread, per frame / per slot:
//   video::Frame frame;
//   switch (prep.LookupPoster(path, seconds, &frame)) {
//       case MediaPrep::Lookup::kReady:   // draw it
//       case MediaPrep::Lookup::kPending: // placeholder; ask next frame
//       case MediaPrep::Lookup::kFailed:  // permanent miss; do not retry
//   }
//   // Tick(): if (prep.ConsumeRenderDirty()) widget->Invalidate();
//
// Threading: every public method is internally synchronized — lookups
// are cheap mutex-guarded map reads safe to call per frame; the decode
// itself runs on the pool threads and never touches the UI. The
// borrowed MediaCatalog serializes its own connection (db_mutex_), so
// worker GetAudioPeaks/ExtractPoster calls coexist with UI queries.
// The catalog must outlive the MediaPrep (the destructor joins the
// workers — declare the MediaPrep AFTER the catalog it borrows).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "media_catalog.h"   // AudioPeaks + MediaCatalog
#include "poster_cache.h"    // PosterCache (poster byte-LRU)

namespace video {
class Frame;  // src/video/frame.h — lookup out-param only
}

namespace XER::VideoEditor {

class MediaPrep {
public:
    enum class Kind : std::uint8_t {
        kPosterAt,      // ClipPoster::GetPosterAt at a bucketed source time
        kPeaks,         // MediaCatalog::GetAudioPeaks (whole-file min/max)
        kImportPoster,  // MediaCatalog::ExtractPoster (bin poster PNG)
    };

    // Tri-state answer of the Lookup* calls — the timeline widget maps
    // this 1:1 onto its MediaLookup hook contract.
    enum class Lookup : std::uint8_t {
        kReady,     // out-param filled — data is usable now
        kPending,   // decode queued or running — retry next frame
        kFailed,    // permanent failure for this request — do not retry
    };

    // Queue/dedup identity of one request. `aux` carries the poster
    // bucket in milliseconds for kPosterAt and is 0 otherwise.
    struct Key {
        Kind kind = Kind::kPosterAt;
        std::string path;
        std::int64_t aux = 0;
        bool operator==(const Key& other) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            const std::size_t h1 = std::hash<std::string>{}(k.path);
            const std::size_t h2 =
                std::hash<std::int64_t>{}(k.aux) ^
                (static_cast<std::size_t>(k.kind) << 1);
            // boost::hash_combine-style mix (64-bit golden constant).
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) +
                         (h1 >> 2));
        }
    };

    // Out slots a JobRunner fills — exactly the field matching
    // Key::kind is dereferenced.
    struct JobOut {
        video::Frame* frame = nullptr;   // kPosterAt
        AudioPeaks* peaks = nullptr;     // kPeaks
    };
    // Returns true when the job produced a usable result (JobOut
    // filled for poster/peaks kinds). Injectable seam for tests — the
    // default runner performs the real ClipPoster/MediaCatalog decode.
    using JobRunner = std::function<bool(const Key&, JobOut*)>;

    struct Options {
        int threads = 2;                            // clamped 1..4
        std::size_t max_pending = 512;              // queued-request cap
        std::size_t max_peaks_entries = 64;         // in-memory peaks LRU
        std::size_t max_failed_keys = 2048;         // negative-cache cap
        std::size_t poster_capacity_bytes =
            PosterCache::kDefaultCapacityBytes;
        // Poster quantization — must match the timeline widget's
        // kPosterQuantize so a widget bucket maps 1:1 onto a decode.
        double poster_bucket_seconds = 0.25;
    };

    // `catalog` is borrowed, never owned — may be nullptr (poster/peaks
    // lookups then fail fast). `runner` overrides the built-in decode
    // path for tests; pass {} for production.
    explicit MediaPrep(MediaCatalog* catalog);
    MediaPrep(MediaCatalog* catalog, Options options,
              JobRunner runner);
    ~MediaPrep();  // stops + joins the pool — never destroys mid-job
    MediaPrep(const MediaPrep&) = delete;
    MediaPrep& operator=(const MediaPrep&) = delete;

    // Lookup-or-enqueue: a cache hit fills the out-param and returns
    // kReady; a miss enqueues the decode once (deduplicated) and
    // returns kPending; a previously-failed request returns kFailed.
    Lookup LookupPoster(const std::string& path, double seconds,
                        video::Frame* frame_out);
    Lookup LookupPeaks(const std::string& path, AudioPeaks* out);

    // Enqueue-only request for the media-bin poster PNG (dedup by
    // path). Success sets the catalog-dirty flag — the app refreshes
    // the bin so the new poster_path column shows up.
    void RequestImportPoster(const std::string& path);

    // Drops queued (not yet running) requests for `path` / all paths.
    // A same-path lookup afterwards re-enqueues, so cancel is safe to
    // issue unconditionally when a clip is deleted — still-needed
    // media simply re-queues on the next render. In-flight decodes
    // finish and publish; their results stay valid cache entries.
    void CancelQueued(const std::string& path);
    void CancelAllQueued();
    // CancelQueued + every stored result and failure record for
    // `path` — for media-bin catalog removal where the media itself
    // went away. Also bumps the path's generation so a worker already
    // mid-decode for `path` drops its result instead of republishing
    // stale peaks/posters that were just purged.
    void PurgePath(const std::string& path);

    // Flag drains for the UI marshal (call from Tick): true once after
    // poster/peaks results landed (repaint needed) / an import poster
    // wrote poster_path (bin refresh needed).
    bool ConsumeRenderDirty();
    bool ConsumeCatalogDirty();
    // Number of RequestImportPoster calls dropped since the last drain
    // because the queue was full — the request is NOT retried
    // automatically, so surface this (once, coalesced) instead of
    // letting posters silently never arrive.
    std::uint64_t ConsumeDroppedPosterRequests();

    // Introspection — primarily for tests.
    std::size_t PendingCount() const;   // queued + running requests
    bool WaitIdle(int timeout_ms = 5000);
    PosterCache::Stats PosterStats() const;

private:
    enum class EnqueueResult {
        kEnqueued,
        kAlreadyPending,
        kQueueFull,  // at max_pending — request dropped, not queued
        kFailed
    };

    void WorkerMain();
    bool RunJob(const Key& key, JobOut* out);
    // `generation` is the path's generation captured when the job was
    // dequeued — a PurgePath during the decode invalidates the result.
    void FinishJob(const Key& key, std::uint64_t generation, bool ok,
                   JobOut* out);
    EnqueueResult Enqueue(const Key& key);
    void StorePeaksLocked(const std::string& path,
                          std::shared_ptr<const AudioPeaks> peaks);
    void MarkFailedLocked(const Key& key);

    MediaCatalog* catalog_ = nullptr;   // borrowed — see header doc
    Options options_;
    JobRunner runner_override_;

    PosterCache posters_;               // has its own internal mutex

    mutable std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable idle_cv_;   // WaitIdle: queue+running empty
    std::deque<Key> queue_;
    // Queued + running requests — the dedup domain.
    std::unordered_set<Key, KeyHash> pending_;
    // Permanent misses — lookups answer kFailed without re-decoding.
    std::unordered_set<Key, KeyHash> failed_;
    std::unordered_map<std::string, std::shared_ptr<const AudioPeaks>>
        peaks_;
    std::deque<std::string> peaks_lru_;   // FIFO eviction, bounded
    // Per-path generation bumped by PurgePath — a job captures it at
    // dequeue and FinishJob drops results whose generation moved, so
    // an in-flight decode can never republish a purged path.
    std::unordered_map<std::string, std::uint64_t> generations_;
    int running_ = 0;
    bool stop_ = false;
    std::vector<std::thread> workers_;

    // Set by FinishJob on the worker; drained by Consume* on the UI.
    std::atomic<std::uint64_t> render_dirty_{0};
    std::atomic<bool> catalog_dirty_{false};
    // RequestImportPoster drops on a full queue — counted so the UI can
    // warn coalesced instead of one status ping per dropped request.
    std::atomic<std::uint64_t> dropped_poster_requests_{0};
};

}  // namespace XER::VideoEditor
