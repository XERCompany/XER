// Disk tier (L2) behind the RAM EffectFrameCache — wires the dormant
// video::ClipFrameCache ".dxcfz" store as a spill target (Adobe-style
// two-tier preview-cache; the T7(a) item in
// docs/wiki/research/2026-11-24-adobe-render-pipeline.md).
//
// Model: EffectFrameCache stays the only lookup surface. On a RAM
// miss it consults this tier; a disk hit promotes the decoded frame
// back into the RAM LRU. On every Store the frame is additionally
// enqueued here (write-through) — the enqueue copies the pixels so
// the render thread only pays a memcpy; zlib + filesystem I/O happen
// on a private writer thread with a bounded queue (drops are counted,
// never block the render path).
//
// Key mapping: the EffectFrameCache::Key splits into
//   directory = <sanitized clip_id>_<state_hash hex>   (one clip-state)
//   frame     = frame_<source_time_us + bias>.dxcfz
// under <root>/video_editor_l2_frames/. The .dxcfz container already
// matches the tier contract exactly — packed BGRA8, zlib
// Z_BEST_SPEED, TTL sidecar — so no format change is needed.
//
// Bounds: XER_VE_L2_MB caps bytes on disk (default 2048; an
// mtime-ordered sweep prunes to ~90% when the estimate exceeds the
// cap — entries have no individual LRU record, file mtime is the
// recency). XER_VE_L2_DIR overrides the root (default
// <temp>/xer_ve_l2_cache). XER_VE_L2=0 disables the tier entirely.
// TTL follows ClipFrameCache's 6 h default; expired entries read as
// misses and get evicted on Exists.
//
// Threading: Lookup may run on any compositor thread (preview worker,
// AutoCacher's private compositor); Store/Flush/GetStats are likewise
// safe concurrently. One writer thread owns all writes + sweeps.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "effect_frame_cache.h"

namespace video {
class Frame;
class ClipFrameCache;
}

namespace XER::VideoEditor {

class L2FrameCache {
public:
    struct Stats {
        std::uint64_t hits = 0;            // disk reads that decoded
        std::uint64_t misses = 0;          // lookup found nothing
        std::uint64_t stores = 0;          // frames written to disk
        std::uint64_t enqueued = 0;        // Store() calls accepted
        std::uint64_t queue_dropped = 0;   // writes shed under pressure
        std::uint64_t dedup_skipped = 0;   // already on disk
        std::uint64_t read_errors = 0;
        std::uint64_t write_errors = 0;
        std::uint64_t swept_files = 0;     // capacity-sweep removals
        std::uint64_t bytes_on_disk = 0;   // estimate (seeded by scan)
    };

    static constexpr std::uint64_t kDefaultCapacityBytes =
        2048ull * 1024ull * 1024ull;

    // XER_VE_L2=0 disables; anything else (incl. unset) enables.
    static bool EnabledByEnv();
    static std::filesystem::path DefaultRoot();

    // Creates the tier, ensures the root dir, seeds the byte estimate
    // with a one-time scan. Returns nullptr when disabled by env or
    // when the root can't be created (error_out describes why).
    static std::shared_ptr<L2FrameCache> Create(
        std::string* error_out = nullptr);
    // Explicit-construction variant for tests (no env gate).
    explicit L2FrameCache(std::filesystem::path root,
                          std::uint64_t capacity_bytes =
                              kDefaultCapacityBytes,
                          std::uint32_t ttl_seconds = 21600);
    ~L2FrameCache();
    L2FrameCache(const L2FrameCache&) = delete;
    L2FrameCache& operator=(const L2FrameCache&) = delete;

    // Disk read; on success `out` holds the decoded frame (allocated
    // inside — caller takes pixels or the shared_ptr ownership).
    bool Lookup(const EffectFrameCache::Key& key, video::Frame* out);

    // Write-through enqueue: copies the pixels, returns immediately.
    // Bounded queue — a full queue drops the write (counted), never
    // blocks the render thread.
    void Store(const EffectFrameCache::Key& key,
               const video::Frame& frame);

    // Drains the write queue (tests/smoke: determinism before stats
    // or shutdown assertions).
    void Flush();

    Stats GetStats() const;
    std::uint64_t CapacityBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::VideoEditor
