// In-memory LRU thumbnail cache for timeline poster frames (XER
// Studio Phase 2). UI-free; pure stdlib + video::Frame. Keyed by
// (media_path, bucket_ms) where bucket_ms is a caller-quantized source
// time in milliseconds — see BucketFor() for the same round-to-nearest
// quantization the timeline widget applies before asking for a poster.
//
// Posters are decoded via ClipPoster::GetPosterAt (packed BGRA8); the
// cache stores shared_ptr<const video::Frame> so a UI-thread Get()
// stays valid even if a decode thread's Put() evicts the entry a
// moment later. All public methods are mutex-guarded — the timeline
// queries on the UI thread while decode may run elsewhere.
// Byte-accounted capacity: entries are evicted least-recently-used
// first until BytesUsed() <= CapacityBytes(); a single frame larger
// than the capacity is never retained. Never throws (except the usual
// std::bad_alloc a caller cannot observe anyway).
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "frame.h"   // src/video — video::Frame (BGRA poster pixels)

namespace XER::VideoEditor {

class PosterCache {
public:
    // Cache key. `bucket_ms` is whatever quantization the caller chose
    // (BucketFor() recommended) — two requests in the same bucket share
    // one decoded poster.
    struct Key {
        std::string media_path;   // UTF-8 source path
        std::int64_t bucket_ms;   // quantized source time, milliseconds
        bool operator==(const Key& other) const = default;
    };

    struct Stats {
        std::size_t entries = 0;         // live entries
        std::size_t bytes = 0;           // sum of Frame::DataSize()
        std::size_t capacity_bytes = 0;  // configured budget
        std::uint64_t hits = 0;          // lifetime Get() hits
        std::uint64_t misses = 0;        // lifetime Get() misses
        std::uint64_t evictions = 0;     // lifetime capacity evictions
    };

    static constexpr std::size_t kDefaultCapacityBytes =
        64u * 1024u * 1024u;                     // 64 MiB
    static constexpr double kDefaultBucketSeconds = 0.5;

    explicit PosterCache(
        std::size_t capacity_bytes = kDefaultCapacityBytes);
    ~PosterCache() = default;
    PosterCache(const PosterCache&) = delete;
    PosterCache& operator=(const PosterCache&) = delete;

    // Quantizes `seconds` to the nearest `bucket_seconds` multiple,
    // returned in milliseconds (the key's bucket_ms). A non-positive
    // bucket collapses to per-millisecond keys (no sharing).
    static std::int64_t BucketFor(
        double seconds, double bucket_seconds = kDefaultBucketSeconds);

    // The cached frame for (path, bucket), or nullptr on a miss.
    // A hit promotes the entry to most-recently-used. The returned
    // shared_ptr keeps the pixels alive across later evictions.
    std::shared_ptr<const video::Frame> Get(const std::string& media_path,
                                            std::int64_t bucket_ms);

    // Caches `frame` under (path, bucket). Re-putting an existing key
    // replaces the frame and refreshes its LRU position. Triggers
    // capacity eviction afterwards. `frame` is moved — pass a copy to
    // keep the original.
    void Put(const std::string& media_path, std::int64_t bucket_ms,
             video::Frame frame);

    // New byte budget; evicts LRU-first until BytesUsed() <= capacity
    // (capacity 0 empties the cache). Takes effect immediately.
    void SetCapacityBytes(std::size_t capacity_bytes);
    std::size_t CapacityBytes() const;
    std::size_t BytesUsed() const;
    std::size_t Size() const;   // live entry count

    // Drops one entry / every bucket of one media path / everything.
    // These are explicit removals — they do NOT count as evictions.
    void Remove(const std::string& media_path, std::int64_t bucket_ms);
    void RemoveMedia(const std::string& media_path);
    void Clear();

    Stats GetStats() const;

private:
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            const std::size_t h1 = std::hash<std::string>{}(k.media_path);
            const std::size_t h2 =
                std::hash<std::int64_t>{}(k.bucket_ms);
            // boost::hash_combine-style mix (64-bit golden constant).
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) +
                         (h1 >> 2));
        }
    };
    struct Entry {
        std::shared_ptr<const video::Frame> frame;
        std::size_t bytes = 0;                 // frame->DataSize()
        std::list<Key>::iterator lru_it;       // position inside lru_
    };

    // mutex_ held — evicts from the LRU tail until bytes_ <=
    // capacity_bytes_ (or the cache is empty).
    void EvictToFitLocked();

    mutable std::mutex mutex_;
    std::list<Key> lru_;   // front = most recently used
    std::unordered_map<Key, Entry, KeyHash> entries_;
    std::size_t capacity_bytes_;
    std::size_t bytes_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
};

}  // namespace XER::VideoEditor
