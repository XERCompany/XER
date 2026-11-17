// poster_cache.cc — see poster_cache.h for the contract. No I/O, no
// platform ifdefs: a mutex-guarded unordered_map + intrusive LRU list.
#include "poster_cache.h"

#include <cmath>
#include <utility>

namespace XER::VideoEditor {

PosterCache::PosterCache(std::size_t capacity_bytes)
    : capacity_bytes_(capacity_bytes) {}

std::int64_t PosterCache::BucketFor(double seconds,
                                    double bucket_seconds) {
    if (!(bucket_seconds > 0.0) || !std::isfinite(seconds)) {
        // Degenerate bucket -> per-millisecond keys (no sharing).
        return static_cast<std::int64_t>(std::llround(seconds * 1000.0));
    }
    // Round-to-nearest bucket multiple — same shape as the timeline
    // widget's kPosterQuantize bucketing (llround(s / q) * q).
    const double bucket = std::llround(seconds / bucket_seconds);
    return static_cast<std::int64_t>(
        std::llround(bucket * bucket_seconds * 1000.0));
}

std::shared_ptr<const video::Frame> PosterCache::Get(
    const std::string& media_path, std::int64_t bucket_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Key key{media_path, bucket_ms};
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        ++misses_;
        return nullptr;
    }
    // Promote to MRU. splice keeps the iterator valid — it now refers
    // to the front position, so entry.lru_it needs no update.
    lru_.splice(lru_.begin(), lru_, it->second.lru_it);
    ++hits_;
    return it->second.frame;
}

void PosterCache::Put(const std::string& media_path,
                      std::int64_t bucket_ms, video::Frame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    Key key{media_path, bucket_ms};
    auto stored = std::make_shared<const video::Frame>(std::move(frame));
    const std::size_t bytes = stored->DataSize();

    const auto it = entries_.find(key);
    if (it != entries_.end()) {
        // Replace in place and promote — no duplicate entry.
        bytes_ -= it->second.bytes;
        it->second.frame = std::move(stored);
        it->second.bytes = bytes;
        bytes_ += bytes;
        lru_.splice(lru_.begin(), lru_, it->second.lru_it);
    } else {
        lru_.push_front(key);
        Entry entry;
        entry.frame = std::move(stored);
        entry.bytes = bytes;
        entry.lru_it = lru_.begin();
        entries_.emplace(std::move(key), std::move(entry));
        bytes_ += bytes;
    }
    EvictToFitLocked();
}

void PosterCache::SetCapacityBytes(std::size_t capacity_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_bytes_ = capacity_bytes;
    EvictToFitLocked();
}

std::size_t PosterCache::CapacityBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_bytes_;
}

std::size_t PosterCache::BytesUsed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

std::size_t PosterCache::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void PosterCache::Remove(const std::string& media_path,
                         std::int64_t bucket_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const Key key{media_path, bucket_ms};
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        return;
    }
    bytes_ -= it->second.bytes;
    lru_.erase(it->second.lru_it);
    entries_.erase(it);
}

void PosterCache::RemoveMedia(const std::string& media_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.media_path == media_path) {
            bytes_ -= it->second.bytes;
            lru_.erase(it->second.lru_it);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void PosterCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    lru_.clear();
    bytes_ = 0;
    // hits_/misses_/evictions_ are lifetime counters — left intact.
}

PosterCache::Stats PosterCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    stats.entries = entries_.size();
    stats.bytes = bytes_;
    stats.capacity_bytes = capacity_bytes_;
    stats.hits = hits_;
    stats.misses = misses_;
    stats.evictions = evictions_;
    return stats;
}

void PosterCache::EvictToFitLocked() {
    while (bytes_ > capacity_bytes_ && !lru_.empty()) {
        const Key& oldest = lru_.back();
        const auto it = entries_.find(oldest);
        if (it != entries_.end()) {
            bytes_ -= it->second.bytes;
            entries_.erase(it);
        }
        lru_.pop_back();
        ++evictions_;
    }
}

}  // namespace XER::VideoEditor
