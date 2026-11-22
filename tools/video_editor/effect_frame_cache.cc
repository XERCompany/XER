// effect_frame_cache.cc — see effect_frame_cache.h for the contract.
#include "effect_frame_cache.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace XER::VideoEditor {

namespace {

// boost::hash_combine-style mix of four key fields into the bucket
// hash. Equality still decides hits — this only spreads buckets.
std::size_t Combine(std::size_t seed, std::size_t v) {
    return seed ^ (v + 0x9e3779b97f4a7c15ull + (seed << 6) +
                   (seed >> 2));
}

}  // namespace

std::size_t EffectFrameCache::KeyHash::operator()(const Key& k) const {
    std::size_t h = std::hash<std::string>{}(k.clip_id);
    h = Combine(h, std::hash<std::int64_t>{}(k.source_time_us));
    h = Combine(h, std::hash<std::uint64_t>{}(k.state_hash));
    return h;
}

std::size_t EffectFrameCache::EnvCapacityBytes() {
    const char* env = std::getenv("XER_VE_FX_CACHE_MB");
    if (env == nullptr || *env == '\0') {
        return kDefaultCapacityBytes;
    }
    const double mb = std::strtod(env, nullptr);
    if (!(mb >= 0.0) || mb > 65536.0) {
        return kDefaultCapacityBytes;
    }
    return static_cast<std::size_t>(mb * 1024.0 * 1024.0);
}

EffectFrameCache::EffectFrameCache()
    : capacity_bytes_(EnvCapacityBytes()) {}

EffectFrameCache::EffectFrameCache(std::size_t capacity_bytes)
    : capacity_bytes_(capacity_bytes) {}

bool EffectFrameCache::Lookup(const Key& key, video::Frame* out) {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = map_.find(key);
    if (it == map_.end() || out == nullptr) {
        ++misses_;
        return false;
    }
    Entry& entry = it->second;
    const std::size_t need =
        static_cast<std::size_t>(entry.width) *
        static_cast<std::size_t>(entry.height) * 4u;
    if (entry.pixels.size() != need || need == 0) {
        // Corrupt entry — drop it rather than serve a bad frame.
        lru_.erase(entry.lru_it);
        bytes_ -= entry.pixels.size();
        map_.erase(it);
        ++misses_;
        return false;
    }
    if (out->Width() != entry.width || out->Height() != entry.height ||
        out->Format() != video::PixelFormat::BGRA8) {
        out->Release();
        if (!out->Allocate(entry.width, entry.height,
                           video::PixelFormat::BGRA8)) {
            ++misses_;
            return false;   // allocation failure — caller recomputes
        }
    }
    std::memcpy(out->Data(), entry.pixels.data(), need);
    TouchLocked(it);
    ++hits_;
    return true;
}

void EffectFrameCache::Store(const Key& key, const video::Frame& frame) {
    if (!frame.IsValid() ||
        frame.Format() != video::PixelFormat::BGRA8) {
        return;
    }
    const std::size_t size = frame.DataSize();
    const std::size_t need =
        static_cast<std::size_t>(frame.Width()) *
        static_cast<std::size_t>(frame.Height()) * 4u;
    if (size == 0 || size != need) {
        return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    if (size > capacity_bytes_) {
        return;   // never fits — don't evict the world for one frame
    }
    const auto existing = map_.find(key);
    if (existing != map_.end()) {
        // Same key re-stored (shouldn't happen — stores follow misses)
        // — refresh content + recency, keep accounting consistent.
        bytes_ -= existing->second.pixels.size();
        existing->second.pixels.assign(frame.Data(),
                                       frame.Data() + size);
        existing->second.width = frame.Width();
        existing->second.height = frame.Height();
        bytes_ += size;
        TouchLocked(existing);
        return;
    }
    EvictLocked(size);
    lru_.push_front(key);
    Entry entry;
    entry.pixels.assign(frame.Data(), frame.Data() + size);
    entry.width = frame.Width();
    entry.height = frame.Height();
    entry.lru_it = lru_.begin();
    map_.emplace(key, std::move(entry));
    bytes_ += size;
    ++stores_;
}

void EffectFrameCache::TouchLocked(
    std::unordered_map<Key, Entry, KeyHash>::iterator it) {
    if (it->second.lru_it != lru_.begin()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru_it);
    }
}

void EffectFrameCache::EvictLocked(std::size_t need_bytes) {
    while (bytes_ + need_bytes > capacity_bytes_ && !lru_.empty()) {
        const Key& oldest = lru_.back();
        const auto it = map_.find(oldest);
        if (it != map_.end()) {
            bytes_ -= it->second.pixels.size();
            map_.erase(it);
        }
        lru_.pop_back();
        ++evictions_;
    }
}

void EffectFrameCache::Clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    map_.clear();
    lru_.clear();
    bytes_ = 0;
}

std::size_t EffectFrameCache::CapacityBytes() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return capacity_bytes_;
}

void EffectFrameCache::SetCapacityBytes(std::size_t bytes) {
    std::lock_guard<std::mutex> lk(mutex_);
    capacity_bytes_ = bytes;
    EvictLocked(0);
}

EffectFrameCache::Stats EffectFrameCache::GetStats() const {
    std::lock_guard<std::mutex> lk(mutex_);
    Stats s;
    s.hits = hits_;
    s.misses = misses_;
    s.stores = stores_;
    s.evictions = evictions_;
    s.lookups_saved_frames = hits_;
    s.bytes = bytes_;
    s.entries = map_.size();
    return s;
}

double EffectFrameCache::HitRate() const {
    std::lock_guard<std::mutex> lk(mutex_);
    const std::uint64_t total = hits_ + misses_;
    return total == 0
               ? 0.0
               : static_cast<double>(hits_) / static_cast<double>(total);
}

}  // namespace XER::VideoEditor
