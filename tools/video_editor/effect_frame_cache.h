// Hash-keyed per-clip effect-chain result cache (Olive-style
// FrameHashCache analog — see
// docs/wiki/research/2026-11-21-nle-architectures.md §4). The
// compositor's per-clip pipeline is
//   decode -> ClipEffect chain -> masks -> transform warp
// and every stage up to the warp is a PURE function of
//   (media identity, media time, ordered effect/param list, mask list,
//    source spec, canvas size, preview-quality tier)
// for the built-in kernels — so the post-mask clip-space frame can be
// memoized under a hash of exactly those inputs. A hit skips the whole
// chain (decode positioning AND every effect kernel); the warp still
// runs so transform changes never invalidate the cached frame.
//
// The cache object is SHARED and injected via CompositorOptions —
// PreviewEngine keeps one across its compositor session reopens (every
// backward seek / loop wrap re-binds the compositor), which is what
// makes scrubbing-back over already-rendered frames a hit instead of a
// re-decode + re-render. Export never injects one: it walks each frame
// exactly once, so caching would only burn memory.
//
// Invalidation correctness: the key is a pure function of the render
// inputs — there is no explicit invalidation. A param tweak changes
// the state hash (miss, recompute, store under the new key); a seek to
// a different media time changes source_time_us; stale entries are
// only ever reclaimed by the byte-budgeted LRU. Stores are gated on an
// "honest serve" check so a decoder's stale backward-scrub slot can
// never poison a key with the wrong frame.
//
// Eligibility (enforced by the compositor, which computes keys):
// adjustment layers are never cached (their input is the composited
// canvas below, not keyable), and chains containing script:/ocio:/
// non-builtin plugin: effects are skipped — JS fx receive clip-local
// time and may keep ctx state, OCIO profiles can rescan mid-session,
// and third-party plugin DLLs are allowed to be stateful. builtin:/
// plugin: resolving to a host-compiled builtin ARE cached (verified
// pure per-pixel kernels).
//
// Threading: every method is internally synchronized — safe to share
// one instance across compositors on different threads (preview worker,
// monitor UI thread) even though today each owner keeps its own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "frame.h"   // video::Frame — XER_Video

namespace XER::VideoEditor {

class EffectFrameCache {
public:
    // Full identity of a clip-space post-effect post-mask frame. The
    // compositor fills every field; equality covers all of them (a
    // hash alone is never trusted — a collision would serve the wrong
    // pixels, the worst bug class here).
    struct Key {
        // Clip id (uuid) — binds the entry to one clip record. Media
        // path/source spec are folded into state_hash.
        std::string clip_id;
        // Media clips: the REQUESTED source time (in_point +
        // (t - timeline_start) * rate, clamped) quantized to
        // microseconds. Generated clips: clip-local time u. The
        // requested — not served — time is used so a hit also skips
        // decoder positioning; stores are gated on the served frame
        // actually being near this time (honest-serve rule).
        std::int64_t source_time_us = 0;
        // FNV-1a over: source_path + source kind/param + the ordered
        // (effect type, raw param bits) list + the ordered mask field
        // bits + canvas dims + preview-quality bits. Any edit touching
        // the chain changes the hash; reorder/add/remove changes the
        // byte stream.
        std::uint64_t state_hash = 0;
        bool operator==(const Key& other) const = default;
    };

    struct Stats {
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t stores = 0;
        std::uint64_t evictions = 0;
        std::uint64_t lookups_saved_frames = 0;  // hits (alias — HUD)
        std::size_t bytes = 0;
        std::size_t entries = 0;
    };

    // Default memory budget — ~60 1080p BGRA frames. XER_VE_FX_CACHE_MB
    // overrides at construction (0 disables without removing the
    // object).
    static constexpr std::size_t kDefaultCapacityBytes =
        512ull * 1024ull * 1024ull;

    EffectFrameCache();
    explicit EffectFrameCache(std::size_t capacity_bytes);
    ~EffectFrameCache() = default;
    EffectFrameCache(const EffectFrameCache&) = delete;
    EffectFrameCache& operator=(const EffectFrameCache&) = delete;

    // Hit: copies the cached pixels into `out` (allocating if needed),
    // refreshes LRU, returns true. Miss: returns false, `out` untouched.
    // Only packed BGRA8 frames are ever stored/returned.
    bool Lookup(const Key& key, video::Frame* out);
    // Copies `frame` under `key` and evicts least-recently-used entries
    // until the byte budget holds. Oversized frames are silently not
    // stored. Invalid/empty frames are ignored.
    void Store(const Key& key, const video::Frame& frame);

    void Clear();
    std::size_t CapacityBytes() const;
    void SetCapacityBytes(std::size_t bytes);

    Stats GetStats() const;
    // Convenience: hits as a fraction of lookups in [0,1] (0 when no
    // lookups yet) — for a HUD/quality readout.
    double HitRate() const;
    // Entries currently held for one clip_id — the timeline
    // render-status bar's green "cached" coverage check (any cached
    // frame for the clip => it plays from cache). O(1).
    std::size_t ClipEntryCount(const std::string& clip_id) const;

private:
    struct KeyHash {
        std::size_t operator()(const Key& k) const;
    };
    struct Entry {
        std::vector<std::uint8_t> pixels;
        int width = 0;
        int height = 0;
        // Position in lru_ — front is most-recently-used.
        std::list<Key>::iterator lru_it;
    };

    void TouchLocked(std::unordered_map<Key, Entry, KeyHash>::iterator it);
    void EvictLocked(std::size_t need_bytes);

    static std::size_t EnvCapacityBytes();

    mutable std::mutex mutex_;
    std::size_t capacity_bytes_;
    std::size_t bytes_ = 0;
    std::list<Key> lru_;   // front = MRU
    std::unordered_map<Key, Entry, KeyHash> map_;
    // Per-clip_id live entry counts (ClipEntryCount) — maintained
    // alongside map_ inserts/erases.
    std::unordered_map<std::string, std::size_t> clip_counts_;

    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t stores_ = 0;
    std::uint64_t evictions_ = 0;
};

}  // namespace XER::VideoEditor
