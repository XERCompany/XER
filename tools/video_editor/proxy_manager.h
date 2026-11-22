// Background proxy-media pipeline for the VideoEditor core
// (Premiere/Resolve-style proxy editing). A single worker thread
// transcodes media files to lightweight edit proxies — capped
// long-edge h264 in mp4, video-only — into a managed directory next
// to the media catalog (<db_dir>/proxies/<hash>.mp4), recording
// state per source in the catalog's `proxies` table
// (pending -> ready | failed).
//
// Decode-side usage is one seam: ProxyManager::ResolveDecodePath()
// maps a media path to its READY proxy (file exists + the source
// fingerprint — size + mtime captured at generation — still
// matches). Callers that may decode through a proxy install it as a
// path resolver:
//   * CompositorOptions::media_resolver — preview + autocache +
//     source monitor (PreviewOptions::media_resolver forwards it);
//   * MediaPrep::Options::path_resolver — poster decodes.
// The AudioMixer and peaks pipeline never resolve — proxies carry
// no audio, and audio decode is cheap enough to keep on the source.
// EXPORT NEVER RESOLVES: ExportEngine builds CompositorOptions{}
// with no resolver, so an export can only ever open original media.
// Keep it that way — a proxy leaking into export is a
// data-integrity bug.
//
//   ProxyManager proxies(&catalog);
//   proxies.Request("D:/media/a.mov", {.max_dim = 960});  // queued
//   // decode paths (worker/UI threads alike — internally locked):
//   const std::string decode = proxies.ResolveDecodePath(path);
//   // Tick(): if (proxies.ConsumeCatalogDirty()) RefreshBin();
//
// Threading: like MediaPrep, every public method is internally
// synchronized — lookups are cheap mutex/SQL reads safe from any
// thread; the transcode runs on the worker and only writes the
// catalog (its own db_mutex_ serializes the connection). The
// catalog must outlive the manager — declare it BEFORE the manager
// (the destructor joins the worker).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include "media_catalog.h"   // MediaCatalog + ProxyRecord

namespace XER::VideoEditor {

// Per-job transcode settings — copied at Request time so a settings
// change only affects later jobs.
struct ProxySettings {
    int max_dim = 960;            // long-edge cap, px (0 = keep size)
    std::string codec = "libx264";// EncodeVideoParams::codec_name
    int crf = 28;                 // -1 = bit_rate path instead
    int bit_rate = 0;             // 0 = bridge default (4 Mbps)
};

class ProxyManager {
public:
    // `catalog` is borrowed, never owned — may be nullptr (requests
    // then fail fast; ResolveDecodePath returns paths unchanged).
    explicit ProxyManager(MediaCatalog* catalog);
    ~ProxyManager();  // cancels + joins the worker — never destroys mid-job
    ProxyManager(const ProxyManager&) = delete;
    ProxyManager& operator=(const ProxyManager&) = delete;

    // Queue a proxy build for `utf8_path` (canonicalized inside).
    // Deduplicated: an already-queued/running request is a no-op.
    // Audio-only and still files are rejected up front. The row goes
    // to pending immediately so the bin badge flips before the worker
    // picks the job up. Returns false when the catalog is closed/null
    // or the path cannot canonicalize.
    bool Request(const std::string& utf8_path, const ProxySettings& s);
    // Cancel a queued or running job: queued entries drop, the
    // in-flight encode aborts at the next frame, the temp file is
    // deleted and the catalog row is REMOVED (state back to none).
    void Cancel(const std::string& utf8_path);
    void CancelAll();

    // Decode-path resolution — THE proxy seam. Returns the managed
    // proxy path when a `ready` row exists for `utf8_path`, the proxy
    // file is still on disk, AND the source file's size+mtime still
    // match the fingerprint stored at generation time; returns
    // `utf8_path` unchanged otherwise (never empty for non-empty
    // input). Internally canonicalizes, so timeline paths and bin
    // paths hit the same row. Safe from any thread.
    // Side effect: a successful resolve refreshes the proxy file's
    // mtime — that timestamp is the LRU marker EnforceStorageCap
    // evicts on (see the storage-cap block below).
    std::string ResolveDecodePath(const std::string& utf8_path) const;

    // Managed proxy filename for a canonical path — FNV-1a hex of the
    // path, stable across sessions so a re-imported file reuses its
    // proxy. Exposed for tests and the smoke check.
    static std::string ProxyFileName(const std::string& canon_path);

    // ---- storage cap / LRU eviction ----------------------------------
    // Byte cap on the managed proxies dir (0 = unbounded). The app
    // feeds Project::proxy_cache_max_mb (MiB) * 2^20. The worker
    // re-enforces after every finished job; EnforceStorageCap() below
    // runs it on demand (project load, settings apply).
    void SetStorageCapBytes(std::int64_t bytes);
    // Media paths (canonicalized inside — raw source paths are fine)
    // whose proxies are NEVER evicted: the open project's sources.
    // Replace-all semantics; an empty vector protects nothing.
    void SetProtectedSources(std::vector<std::string> utf8_paths);
    // Evicts least-recently-used ready proxies — managed file +
    // catalog row together — until the proxies dir fits the cap.
    // LRU stamp choice (documented contract): the proxy FILE's mtime
    // is its last-used marker; ResolveDecodePath rewrites it (~1 min
    // granularity) on every successful resolve, so recency survives
    // across sessions without a schema change and costs at most one
    // timestamp write per resolve. Rows skipped: protected sources,
    // queued/running jobs, non-ready states, missing files, and any
    // proxy_path outside the managed dir (a hand-edited row must not
    // become a deletion primitive). Returns the evicted count; the
    // dirty flag flips so the bin re-reads badges. Safe from any
    // thread; runs its disk I/O off the queue mutex.
    int EnforceStorageCap();

    // ---- UI drains / introspection (Tick thread) --------------------
    // True once after any proxies-row write (pending/ready/failed/
    // removed) — the bin refreshes so badges track state.
    bool ConsumeCatalogDirty();
    // Queued + running jobs (Request dedup domain).
    std::size_t PendingCount() const;
    int RunningCount() const;    // 0 or 1 (single worker)
    // Progress of the running job for `utf8_path` in [0,1]; -1 when
    // that path is not the one currently encoding.
    double ProgressFor(const std::string& utf8_path) const;
    // One-line status-strip text: "" when idle, else e.g.
    // "Proxy: clip.mov 42% (+2 queued)".
    std::string DescribeActivity() const;
    bool WaitIdle(int timeout_ms = 5000);   // tests/smoke

private:
    struct Job {
        std::string canon;        // canonical source path
        ProxySettings settings;
    };

    void WorkerMain();
    // Runs one job end-to-end on the worker: stat source, transcode
    // to a temp file next to the target, commit on success. The
    // proxies row transitions pending -> ready|failed inside. Cancel
    // aborts cooperatively via IsCancelled(canon).
    void RunJob(const Job& job);
    // `skipped_out` is set true when the source needed no proxy
    // (already under the size cap) — the row was removed and no file
    // was written; RunJob then writes NO state at all.
    bool Transcode(const Job& job, const std::string& proxy_path,
                   std::string* error_out, bool* skipped_out);
    bool IsCancelled(const std::string& canon) const;
    void WriteState(const std::string& canon, ProxyStatus status,
                    const std::string& proxy_path,
                    std::int64_t src_size, std::int64_t src_mtime,
                    int max_dim, const std::string& error);

    MediaCatalog* catalog_ = nullptr;   // borrowed — see header doc

    mutable std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable idle_cv_;   // WaitIdle: queue+running empty
    std::deque<Job> queue_;
    std::unordered_set<std::string> pending_;   // dedup domain
    std::unordered_set<std::string> cancelled_; // cancel marks
    bool running_ = false;
    std::string running_path_;        // canon of the in-flight job
    bool stop_ = false;
    std::thread worker_;

    // Set by the worker; drained by ConsumeCatalogDirty on the UI.
    std::atomic<bool> catalog_dirty_{false};
    // Progress of the running job — [0,1] fraction of source
    // duration decoded (progress only; the row owns truth).
    std::atomic<double> progress_{0.0};

    // Storage cap: byte limit (0 = unbounded) read by
    // EnforceStorageCap; protected_sources_ holds canonical paths the
    // sweep skips (mutex_-guarded like the job sets).
    std::atomic<std::int64_t> cap_bytes_{0};
    std::unordered_set<std::string> protected_sources_;
};

}  // namespace XER::VideoEditor
