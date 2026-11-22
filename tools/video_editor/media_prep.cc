// media_prep.cc — see media_prep.h for the contract. A mutex+cv
// bounded FIFO feeding 1..4 decode workers; every request dedups by
// (kind, path, aux) across both the queue and the currently-running
// set, so per-frame lookup calls can re-ask freely without ever
// doubling decode work. Results land in the byte-bounded PosterCache
// (posters) or the count-bounded peaks map; failures are recorded so
// a known-bad request answers kFailed instead of re-decoding every
// frame. No exceptions, no UI dependencies — stays in the core lib.
#include "media_prep.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

#include "frame.h"   // src/video — video::Frame
#include "media_worker_client.h"

namespace XER::VideoEditor {
namespace {

constexpr int kMinThreads = 1;
constexpr int kMaxThreads = 4;

}  // namespace

MediaPrep::MediaPrep(MediaCatalog* catalog)
    : MediaPrep(catalog, Options{}, {}) {}

MediaPrep::MediaPrep(MediaCatalog* catalog, Options options,
                     JobRunner runner)
    : catalog_(catalog),
      options_(options),
      runner_override_(std::move(runner)),
      posters_(options.poster_capacity_bytes) {
    const int threads = std::clamp(options_.threads, kMinThreads,
                                   kMaxThreads);
    workers_.reserve(static_cast<std::size_t>(threads));
    for (int i = 0; i < threads; ++i) {
        workers_.emplace_back([this] { WorkerMain(); });
    }
}

MediaPrep::~MediaPrep() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
        pending_.clear();
    }
    work_cv_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

MediaPrep::Lookup MediaPrep::LookupPoster(const std::string& path,
                                          double seconds,
                                          video::Frame* frame_out) {
    if (path.empty() || frame_out == nullptr) return Lookup::kFailed;
    const std::int64_t bucket_ms =
        PosterCache::BucketFor(seconds, options_.poster_bucket_seconds);
    if (const auto frame = posters_.Get(path, bucket_ms)) {
        *frame_out = *frame;
        return Lookup::kReady;
    }
    const Key key{Kind::kPosterAt, path, bucket_ms};
    switch (Enqueue(key)) {
        case EnqueueResult::kFailed:
            return Lookup::kFailed;
        case EnqueueResult::kEnqueued:
        case EnqueueResult::kAlreadyPending:
        case EnqueueResult::kQueueFull:
            return Lookup::kPending;
    }
    return Lookup::kPending;
}

MediaPrep::Lookup MediaPrep::LookupPeaks(const std::string& path,
                                         AudioPeaks* out) {
    if (path.empty() || out == nullptr) return Lookup::kFailed;
    if (catalog_ == nullptr || !catalog_->IsOpen()) {
        return Lookup::kFailed;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = peaks_.find(path);
        if (it != peaks_.end()) {
            *out = *it->second;
            return Lookup::kReady;
        }
    }
    const Key key{Kind::kPeaks, path, 0};
    switch (Enqueue(key)) {
        case EnqueueResult::kFailed:
            return Lookup::kFailed;
        case EnqueueResult::kEnqueued:
        case EnqueueResult::kAlreadyPending:
        case EnqueueResult::kQueueFull:
            return Lookup::kPending;
    }
    return Lookup::kPending;
}

void MediaPrep::RequestImportPoster(const std::string& path) {
    if (path.empty() || catalog_ == nullptr) return;
    const Key key{Kind::kImportPoster, path, 0};
    {
        // An explicit user action always re-attempts — clear any
        // recorded failure from an earlier import of this path.
        std::lock_guard<std::mutex> lock(mutex_);
        failed_.erase(key);
    }
    // Unlike the Lookup* calls, a dropped import-poster request is
    // never re-asked (nothing polls for it) — count the drop so the
    // caller can warn instead of the poster silently never arriving.
    if (Enqueue(key) == EnqueueResult::kQueueFull) {
        dropped_poster_requests_.fetch_add(1, std::memory_order_relaxed);
    }
}

void MediaPrep::CancelQueued(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->path == path) {
            pending_.erase(*it);   // re-requestable immediately
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    // Idle waiters observe the shrunken queue via WaitIdle's predicate.
    idle_cv_.notify_all();
}

void MediaPrep::CancelAllQueued() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Key& key : queue_) {
        pending_.erase(key);
    }
    queue_.clear();
    idle_cv_.notify_all();
}

void MediaPrep::PurgePath(const std::string& path) {
    CancelQueued(path);
    // mutex_ → posters_ ordering (same as FinishJob): removal and the
    // generation bump share one critical section so a finishing job
    // can never publish between the cache wipe and the bump.
    std::lock_guard<std::mutex> lock(mutex_);
    posters_.RemoveMedia(path);
    // Bump the generation only while a job for this path is actually
    // in flight — queued ones were just cancelled above, so a pending_
    // entry here means a worker is mid-decode and its FinishJob must
    // drop the result instead of republishing the entries we removed.
    // Tombstoning unconditionally would grow generations_ on every
    // purge with nothing left to catch.
    const bool in_flight = std::any_of(
        pending_.begin(), pending_.end(),
        [&path](const Key& k) { return k.path == path; });
    if (in_flight) {
        ++generations_[path];
    }
    peaks_.erase(path);
    peaks_lru_.erase(
        std::remove(peaks_lru_.begin(), peaks_lru_.end(), path),
        peaks_lru_.end());
    for (auto it = failed_.begin(); it != failed_.end();) {
        it = it->path == path ? failed_.erase(it) : std::next(it);
    }
}

bool MediaPrep::ConsumeRenderDirty() {
    return render_dirty_.exchange(0, std::memory_order_acq_rel) > 0;
}

bool MediaPrep::ConsumeCatalogDirty() {
    return catalog_dirty_.exchange(false, std::memory_order_acq_rel);
}

std::uint64_t MediaPrep::ConsumeDroppedPosterRequests() {
    return dropped_poster_requests_.exchange(0,
                                             std::memory_order_acq_rel);
}

std::size_t MediaPrep::PendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

bool MediaPrep::WaitIdle(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_cv_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms),
        [this] { return queue_.empty() && running_ == 0; });
}

PosterCache::Stats MediaPrep::PosterStats() const {
    return posters_.GetStats();
}

// ---- internals ----

MediaPrep::EnqueueResult MediaPrep::Enqueue(const Key& key) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failed_.count(key) != 0u) {
            return EnqueueResult::kFailed;
        }
        if (pending_.count(key) != 0u) {
            return EnqueueResult::kAlreadyPending;
        }
        if (queue_.size() >= options_.max_pending) {
            // Back-pressure: lookups still report kPending — the
            // caller re-asks next frame and the enqueue retries once
            // the queue drains. Enqueue-only requests see kQueueFull.
            return EnqueueResult::kQueueFull;
        }
        pending_.insert(key);
        queue_.push_back(key);
    }
    work_cv_.notify_one();
    return EnqueueResult::kEnqueued;
}

void MediaPrep::WorkerMain() {
    for (;;) {
        Key key;
        std::uint64_t generation;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_cv_.wait(lock,
                          [this] { return stop_ || !queue_.empty(); });
            if (stop_ || queue_.empty()) {
                return;
            }
            key = queue_.front();
            queue_.pop_front();
            ++running_;
            // The key stays in pending_ while running — still dedups.
            // Capture the path's purge generation for FinishJob.
            const auto git = generations_.find(key.path);
            generation = git != generations_.end() ? git->second : 0;
        }

        video::Frame frame;
        AudioPeaks peaks;
        JobOut out;
        out.frame = &frame;
        out.peaks = &peaks;
        const bool ok = RunJob(key, &out);
        FinishJob(key, generation, ok, &out);
    }
}

bool MediaPrep::RunJob(const Key& key, JobOut* out) {
    if (runner_override_) {
        return runner_override_(key, out);
    }
    // Proxy seam: only the kPosterAt VIDEO decode goes through the
    // optional resolver (a ready proxy decodes a poster a lot cheaper
    // than the 4K source). kPeaks stays on the original — proxies
    // carry no audio — and kImportPoster must too: ExtractPoster keys
    // the poster row update on the media path itself, so feeding it a
    // managed proxy path would write to a row that does not exist.
    const std::string decode_path =
        (options_.path_resolver && key.kind == Kind::kPosterAt)
            ? options_.path_resolver(key.path)
            : key.path;
    switch (key.kind) {
        case Kind::kPosterAt:
            // Out-of-process decode first: a decoder crash kills the
            // --media-worker subprocess, not the editor. The client
            // returns false on spawn fail/timeout/death/suspect-path —
            // the in-process decode below is the seamless fallback.
            // The resolver only applies to the in-process fallback:
            // the worker resolves its own path policy.
#if defined(_WIN32)
            if (MediaWorkerClient::Shared().PosterAt(
                    key.path,
                    static_cast<double>(key.aux) / 1000.0,
                    out->frame)) {
                return true;
            }
#endif
            return MediaCatalog::GetPosterAt(
                decode_path, static_cast<double>(key.aux) / 1000.0,
                out->frame, nullptr);
        case Kind::kPeaks:
#if defined(_WIN32)
            if (MediaWorkerClient::Shared().Peaks(key.path,
                                                out->peaks)) {
                return true;
            }
#endif
            return catalog_ != nullptr &&
                   catalog_->GetAudioPeaks(key.path, out->peaks);
        case Kind::kImportPoster:
            // Stays in-process: the catalog row update lives here —
            // the worker cannot write the parent's SQLite.
            return catalog_ != nullptr &&
                   catalog_->ExtractPoster(key.path);
    }
    return false;
}

void MediaPrep::FinishJob(const Key& key, std::uint64_t generation,
                          bool ok, JobOut* out) {
    // The stale check and the publish run inside ONE critical section:
    // a PurgePath interleaving between the two would resurrect a
    // poster/peaks entry it just removed. Lock order is mutex_ →
    // posters_ — PosterCache locking is leaf-level (it never takes
    // mutex_), so holding mutex_ across posters_.Put cannot deadlock.
    std::lock_guard<std::mutex> lock(mutex_);
    // A job that outlived a PurgePath of its media publishes nothing
    // and records no failure — a re-request must be free to re-decode
    // the current file.
    const auto git = generations_.find(key.path);
    const bool stale =
        git != generations_.end() && git->second != generation;
    // Publish first — running_ only drops after the result is visible,
    // so WaitIdle() can never observe "idle" ahead of the data.
    bool produced = false;
    if (ok && !stale) {
        switch (key.kind) {
            case Kind::kPosterAt:
                if (out->frame != nullptr && out->frame->IsValid()) {
                    // PosterCache owns the pixels from here (moved in).
                    posters_.Put(key.path, key.aux,
                                 std::move(*out->frame));
                    produced = true;
                }
                break;
            case Kind::kPeaks:
                if (out->peaks != nullptr && out->peaks->buckets > 0 &&
                    out->peaks->channels > 0) {
                    auto stored =
                        std::make_shared<const AudioPeaks>(*out->peaks);
                    StorePeaksLocked(key.path, std::move(stored));
                    produced = true;
                }
                break;
            case Kind::kImportPoster:
                catalog_dirty_.store(true, std::memory_order_release);
                produced = true;
                break;
        }
        if (produced && key.kind != Kind::kImportPoster) {
            render_dirty_.fetch_add(1, std::memory_order_release);
        }
    }
    pending_.erase(key);
    --running_;
    if (!produced && !stale) {
        // Decode failure, or ok=true with unusable output — both
        // are permanent misses as far as lookups are concerned.
        // (A stale result is neither published nor cached as a
        // failure — the purge invalidated it, not the decode.)
        MarkFailedLocked(key);
    }
    // Reclaim the purge tombstone once no queued or running job
    // remains for this path — pending_ holds both (keys stay in
    // the set until their FinishJob), so while a same-path job is
    // still in flight the entry must survive to catch its stale
    // result.
    if (generations_.count(key.path) != 0u) {
        const bool any_pending = std::any_of(
            pending_.begin(), pending_.end(),
            [&key](const Key& k) { return k.path == key.path; });
        if (!any_pending) {
            generations_.erase(key.path);
        }
    }
    if (queue_.empty() && running_ == 0) {
        idle_cv_.notify_all();
    }
}

void MediaPrep::StorePeaksLocked(
    const std::string& path, std::shared_ptr<const AudioPeaks> peaks) {
    // mutex_ held. New paths append to the FIFO; hitting paths are
    // refreshed in place (the LRU order is coarse — FIFO eviction is
    // enough for a decode cache).
    if (peaks_.count(path) == 0u) {
        peaks_lru_.push_back(path);
        while (peaks_lru_.size() > options_.max_peaks_entries) {
            peaks_.erase(peaks_lru_.front());
            peaks_lru_.pop_front();
        }
    }
    peaks_[path] = std::move(peaks);
}

void MediaPrep::MarkFailedLocked(const Key& key) {
    // mutex_ held. Bounded negative cache: on overflow the whole set
    // is dropped — a stale failure then costs at most one re-decode.
    failed_.insert(key);
    if (failed_.size() > options_.max_failed_keys) {
        failed_.clear();
        failed_.insert(key);
    }
}

}  // namespace XER::VideoEditor
