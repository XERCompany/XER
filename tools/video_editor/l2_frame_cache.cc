// l2_frame_cache.cc — see l2_frame_cache.h for the contract.
//
// Writer-thread flow: Store() resolves the per-(clip,state-hash)
// ClipFrameCache and queues {cache*, index, owned pixels}; the writer
// pops items, dedupes on HasFrame (TTL-aware), wraps the vector in a
// Frame via Adopt (no second copy) and WriteFrame()s it — zlib +
// fsync-class I/O stays off the render thread. Accounting is an
// estimate seeded by a directory scan at open; when it crosses the
// cap the writer runs an mtime-ordered sweep down to ~90%.
//
// Crash-safety note: PersistentStorage::WriteBinary isn't atomic, so
// a torn .dxcfz can exist after a crash — ReadFrame's header/size/
// zlib checks turn that into a plain miss (recompute), never bad
// pixels.
#include "l2_frame_cache.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <absl/strings/str_cat.h>

#include "video/clip_frame_cache.h"
#include "video/frame.h"

namespace XER::VideoEditor {
namespace {

// Scope directory under the storage root — matches ClipFrameCache's
// CacheEntry::scope so the sweep can find files with a plain
// recursive walk of one subtree.
constexpr char kL2Scope[] = "video_editor_l2_frames";

// source_time_us is signed; the on-disk frame index is not. A large
// bias keeps even negative (pre-roll/under-run) times addressable.
constexpr std::uint64_t kIndexBias = 1ull << 62;

// Write queue depth in frames (each ≤ ~8 MiB of copied BGRA at
// 1080p) — enough to absorb a burst of autocache stores, small enough
// that a saturated disk doesn't grow memory unbounded.
constexpr std::size_t kMaxQueuedFrames = 8;

// Keep ~90% of the cap after a sweep — hysteresis so the sweep
// doesn't re-trigger on the next store.
constexpr double kSweepTargetFraction = 0.9;

std::string SanitizeDirKey(const std::string& clip_id,
                           std::uint64_t state_hash) {
    std::string out;
    out.reserve(clip_id.size() + 17);
    for (const char c : clip_id) {
        const bool safe =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.';
        out.push_back(safe ? c : '_');
    }
    if (out.size() > 48) out.resize(48);
    if (out.empty()) out = "clip";
    return absl::StrCat(out, "_",
                        absl::Hex(state_hash, absl::kZeroPad16));
}

std::size_t IndexFor(std::int64_t source_time_us) {
    return static_cast<std::size_t>(
        static_cast<std::uint64_t>(source_time_us) + kIndexBias);
}

// Sums bytes of every *.dxcfz under `dir` (recursive). Used once at
// open to seed the estimate and inside the sweep for the true total.
std::uint64_t ScanBytes(const std::filesystem::path& dir) {
    std::error_code ec;
    std::uint64_t total = 0;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) &&
            it->path().extension() == ".dxcfz") {
            total += it->file_size(ec);
        }
    }
    return total;
}

}  // namespace

struct L2FrameCache::Impl {
    struct WriteItem {
        video::ClipFrameCache* cache = nullptr;   // owned by `caches`
        std::size_t index = 0;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;
    };

    explicit Impl(std::filesystem::path r, std::uint64_t cap,
                  std::uint32_t ttl)
        : root(std::move(r)),
          scope_dir(root / kL2Scope),
          capacity_bytes(cap),
          ttl_seconds(ttl) {}

    ~Impl() {
        {
            std::lock_guard<std::mutex> lk(queue_mutex);
            stop = true;
        }
        queue_cv.notify_all();
        if (writer.joinable()) {
            writer.join();
        }
    }

    void StartWriter() {
        writer = std::thread([this] { WriterMain(); });
    }

    // Per-(clip,state) ClipFrameCache — created once, kept for the
    // tier's lifetime so queued WriteItems can hold a raw pointer.
    video::ClipFrameCache* CacheFor(
        const EffectFrameCache::Key& key) {
        const std::string dirkey =
            SanitizeDirKey(key.clip_id, key.state_hash);
        std::lock_guard<std::mutex> lk(caches_mutex);
        auto it = caches.find(dirkey);
        if (it != caches.end()) {
            return it->second.get();
        }
        auto cache = std::make_unique<video::ClipFrameCache>(
            root, dirkey, ttl_seconds, kL2Scope);
        auto* ptr = cache.get();
        caches.emplace(dirkey, std::move(cache));
        return ptr;
    }

    void WriterMain() {
        for (;;) {
            WriteItem item;
            {
                std::unique_lock<std::mutex> lk(queue_mutex);
                queue_cv.wait(lk, [&] {
                    return stop || !queue.empty();
                });
                if (queue.empty()) {
                    return;   // stop with drained queue
                }
                item = std::move(queue.front());
                queue.pop_front();
            }

            std::string werr;
            if (item.cache->HasFrame(item.index)) {
                ++stats_dedup_skipped;
            } else {
                video::Frame frame;
                if (frame.Adopt(item.width, item.height,
                                video::PixelFormat::BGRA8,
                                std::move(item.pixels)) &&
                    item.cache->WriteFrame(item.index, frame, &werr)) {
                    ++stats_stores;
                    std::error_code ec;
                    const std::uint64_t written = std::filesystem::
                        file_size(
                            item.cache->PathForFrame(item.index), ec);
                    if (!ec) {
                        bytes_on_disk.fetch_add(
                            written, std::memory_order_relaxed);
                    }
                } else {
                    ++stats_write_errors;
                }
            }

            // Estimate says we're over cap → take the true total and
            // prune. Sweep also resyncs the estimate afterwards.
            if (bytes_on_disk.load(std::memory_order_relaxed) >
                capacity_bytes) {
                Sweep();
            }

            // Counted only after the write lands so Flush() waits for
            // committed files, not just dequeued items.
            pending.fetch_sub(1, std::memory_order_release);
            pending_cv.notify_all();
        }
    }

    // Removes oldest (by mtime) .dxcfz files under scope_dir until the
    // true total is <= ~90% of the cap, plus their .ttl sidecars. Runs
    // only on the writer thread; readers see a removed file as a
    // plain miss.
    void Sweep() {
        struct File {
            std::filesystem::path path;
            std::filesystem::file_time_type mtime;
            std::uint64_t size;
        };
        std::vector<File> files;
        std::uint64_t total = 0;
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator
                 it(scope_dir, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) ||
                it->path().extension() != ".dxcfz") {
                continue;
            }
            File f;
            f.path = it->path();
            f.size = it->file_size(ec);
            f.mtime = it->last_write_time(ec);
            total += f.size;
            files.push_back(std::move(f));
        }
        const std::uint64_t target = static_cast<std::uint64_t>(
            static_cast<double>(capacity_bytes) *
            kSweepTargetFraction);
        if (total <= target) {
            bytes_on_disk.store(total, std::memory_order_relaxed);
            return;
        }
        std::sort(files.begin(), files.end(),
                  [](const File& a, const File& b) {
                      return a.mtime < b.mtime;
                  });
        for (const File& f : files) {
            if (total <= target) {
                break;
            }
            std::error_code rec;
            if (std::filesystem::remove(f.path, rec) && !rec) {
                total -= f.size;
                ++stats_swept_files;
            }
            // The .ttl sidecar travels with its data file.
            const std::filesystem::path sidecar(f.path.string() +
                                                ".ttl");
            std::filesystem::remove(sidecar, rec);
        }
        bytes_on_disk.store(total, std::memory_order_relaxed);
    }

    const std::filesystem::path root;
    const std::filesystem::path scope_dir;
    const std::uint64_t capacity_bytes;
    const std::uint32_t ttl_seconds;

    std::mutex caches_mutex;
    std::unordered_map<std::string,
                       std::unique_ptr<video::ClipFrameCache>>
        caches;

    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<WriteItem> queue;
    bool stop = false;
    std::thread writer;
    std::atomic<int> pending{0};
    std::condition_variable pending_cv;
    std::mutex pending_mutex;

    std::atomic<std::uint64_t> bytes_on_disk{0};

    std::atomic<std::uint64_t> stats_hits{0};
    std::atomic<std::uint64_t> stats_misses{0};
    std::atomic<std::uint64_t> stats_stores{0};
    std::atomic<std::uint64_t> stats_enqueued{0};
    std::atomic<std::uint64_t> stats_queue_dropped{0};
    std::atomic<std::uint64_t> stats_dedup_skipped{0};
    std::atomic<std::uint64_t> stats_read_errors{0};
    std::atomic<std::uint64_t> stats_write_errors{0};
    std::atomic<std::uint64_t> stats_swept_files{0};
};

bool L2FrameCache::EnabledByEnv() {
    const char* env = std::getenv("XER_VE_L2");
    return env == nullptr || *env == '\0' || *env != '0';
}

std::filesystem::path L2FrameCache::DefaultRoot() {
    if (const char* env = std::getenv("XER_VE_L2_DIR");
        env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    std::error_code ec;
    std::filesystem::path base =
        std::filesystem::temp_directory_path(ec);
    if (ec) {
        base = ".";
    }
    return base / "xer_ve_l2_cache";
}

std::shared_ptr<L2FrameCache> L2FrameCache::Create(
    std::string* error_out) {
    if (!EnabledByEnv()) {
        if (error_out != nullptr) {
            *error_out = "L2 frame cache disabled (XER_VE_L2=0)";
        }
        return nullptr;
    }
    std::uint64_t cap = kDefaultCapacityBytes;
    if (const char* env = std::getenv("XER_VE_L2_MB");
        env != nullptr && *env != '\0') {
        const double mb = std::strtod(env, nullptr);
        if (mb > 0.0) {
            cap = static_cast<std::uint64_t>(mb * 1024.0 * 1024.0);
        }
    }
    auto cache = std::make_shared<L2FrameCache>(DefaultRoot(), cap);
    // The ctor already created the scope dir, seeded the byte
    // estimate and started the writer; if the root wasn't writable
    // the scope dir simply won't exist — report instead of serving a
    // silently-broken tier.
    std::error_code ec;
    if (!std::filesystem::is_directory(cache->impl_->scope_dir, ec) ||
        ec) {
        if (error_out != nullptr) {
            *error_out = absl::StrCat(
                "L2 frame cache root not writable: ",
                cache->impl_->scope_dir.string());
        }
        return nullptr;
    }
    return cache;
}

L2FrameCache::L2FrameCache(std::filesystem::path root,
                           std::uint64_t capacity_bytes,
                           std::uint32_t ttl_seconds)
    : impl_(std::make_unique<Impl>(std::move(root), capacity_bytes,
                                 ttl_seconds)) {
    std::error_code ec;
    std::filesystem::create_directories(impl_->scope_dir, ec);
    impl_->bytes_on_disk.store(ScanBytes(impl_->scope_dir),
                               std::memory_order_relaxed);
    impl_->StartWriter();
}

L2FrameCache::~L2FrameCache() = default;

bool L2FrameCache::Lookup(const EffectFrameCache::Key& key,
                          video::Frame* out) {
    if (out == nullptr) {
        return false;
    }
    video::ClipFrameCache* cache = impl_->CacheFor(key);
    std::shared_ptr<video::Frame> frame;
    std::string err;
    if (cache->ReadFrame(IndexFor(key.source_time_us), &frame,
                         &err) &&
        frame != nullptr && frame->IsValid()) {
        ++impl_->stats_hits;
        *out = std::move(*frame);
        return true;
    }
    ++impl_->stats_misses;
    // Distinguish plain-miss from read/decode error for the stats:
    // HasFrame is TTL-aware, so an expired file reports absent too.
    if (!err.empty() && cache->HasFrame(IndexFor(key.source_time_us))) {
        ++impl_->stats_read_errors;
    }
    return false;
}

void L2FrameCache::Store(const EffectFrameCache::Key& key,
                         const video::Frame& frame) {
    if (!frame.IsValid() ||
        frame.Format() != video::PixelFormat::BGRA8 ||
        frame.DataSize() == 0) {
        return;
    }
    video::ClipFrameCache* cache = impl_->CacheFor(key);
    Impl::WriteItem item;
    item.cache = cache;
    item.index = IndexFor(key.source_time_us);
    item.width = frame.Width();
    item.height = frame.Height();
    item.pixels.assign(frame.Data(), frame.Data() + frame.DataSize());
    {
        std::lock_guard<std::mutex> lk(impl_->queue_mutex);
        if (impl_->queue.size() >= kMaxQueuedFrames) {
            ++impl_->stats_queue_dropped;
            return;
        }
        impl_->pending.fetch_add(1, std::memory_order_relaxed);
        impl_->queue.push_back(std::move(item));
    }
    ++impl_->stats_enqueued;
    impl_->queue_cv.notify_one();
}

void L2FrameCache::Flush() {
    std::unique_lock<std::mutex> lk(impl_->pending_mutex);
    impl_->pending_cv.wait(lk, [&] {
        return impl_->pending.load(std::memory_order_acquire) == 0;
    });
}

std::uint64_t L2FrameCache::CapacityBytes() const {
    return impl_->capacity_bytes;
}

L2FrameCache::Stats L2FrameCache::GetStats() const {
    Stats s;
    s.hits = impl_->stats_hits.load();
    s.misses = impl_->stats_misses.load();
    s.stores = impl_->stats_stores.load();
    s.enqueued = impl_->stats_enqueued.load();
    s.queue_dropped = impl_->stats_queue_dropped.load();
    s.dedup_skipped = impl_->stats_dedup_skipped.load();
    s.read_errors = impl_->stats_read_errors.load();
    s.write_errors = impl_->stats_write_errors.load();
    s.swept_files = impl_->stats_swept_files.load();
    s.bytes_on_disk = impl_->bytes_on_disk.load();
    return s;
}

}  // namespace XER::VideoEditor
