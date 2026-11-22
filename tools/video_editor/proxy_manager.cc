// proxy_manager.cc — see proxy_manager.h for the contract. One
// worker thread runs jobs end-to-end: stat the source, stream
// VideoDecoder frames through an area-average downscale into
// EncodeVideoFrameStream (libx264, capped long edge, video-only),
// then atomically commit the temp file over the managed target and
// flip the catalog row pending -> ready. Cancellations abort inside
// the frame provider and drop the row entirely.
//
// Frame-count wrinkle: EncodeVideoFrameStream needs the exact frame
// count up front, which only exists after a full decode for VFR
// sources. The job therefore estimates llround(duration*fps) and —
// only when the source comes up SHORT — reruns the pass with the now
// exact count. CFR media (the overwhelming common case) completes in
// one pass; the worst case costs a second decode, never a corrupt
// proxy.
#include "proxy_manager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

#include "cache/cache_utils.h"
#include "helper/path_utils.h"
#include "media_catalog.h"   // kProbedStillMaxSeconds
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;
namespace FFmpeg = XER::Bridge::FFmpeg;

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

// Canonical absolute UTF-8 form — the same shape media_catalog.cc
// keys every row on (weakly_canonical with a lexical fallback).
bool CanonicalizeMediaPath(const std::string& utf8_path,
                           std::string* canon_out) {
    const std::string normalized = Path::NormalizeUtf8Path(utf8_path);
    if (normalized.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path canon =
        std::filesystem::weakly_canonical(Path::FromUtf8(normalized), ec);
    if (ec || canon.empty()) {
        canon = Path::FromUtf8(normalized);
        std::error_code aec;
        if (canon.is_relative()) {
            canon = std::filesystem::absolute(canon, aec);
        }
        if (aec) {
            return false;
        }
        canon = canon.lexically_normal();
    }
    *canon_out = Path::ToUtf8(canon);
    return true;
}

// File mtime as unix seconds; 0 when unstattable — same convention
// as media_catalog.cc's FileMtimeUnix.
std::int64_t FileMtimeUnix(const std::filesystem::path& fs_path) {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(fs_path, ec);
    if (ec) {
        return 0;
    }
    const auto sys =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(
               sys.time_since_epoch())
        .count();
}

std::int64_t FileSizeBytes(const std::filesystem::path& fs_path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(fs_path, ec);
    return ec ? 0 : static_cast<std::int64_t>(size);
}

// Aspect-preserving BGRA area-average downscale on raw buffers —
// the same minification math clip_poster.cc uses for poster thumbs,
// kept local because the poster helper is file-static.
void DownscaleBgra(const std::uint8_t* src, int src_w, int src_h,
                   std::uint8_t* dst, int dst_w, int dst_h) {
    for (int dy = 0; dy < dst_h; ++dy) {
        const int sy0 = dy * src_h / dst_h;
        const int sy1 = (dy + 1) * src_h / dst_h;
        for (int dx = 0; dx < dst_w; ++dx) {
            const int sx0 = dx * src_w / dst_w;
            const int sx1 = (dx + 1) * src_w / dst_w;
            std::uint32_t b = 0, g = 0, r = 0, a = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const std::uint8_t* row =
                    src + (static_cast<std::size_t>(sy) * src_w + sx0) *
                              4u;
                for (int sx = sx0; sx < sx1; ++sx) {
                    b += row[0];
                    g += row[1];
                    r += row[2];
                    a += row[3];
                    row += 4;
                }
            }
            const std::uint32_t n =
                static_cast<std::uint32_t>((sy1 - sy0) * (sx1 - sx0));
            std::uint8_t* out =
                dst + (static_cast<std::size_t>(dy) * dst_w + dx) * 4u;
            out[0] = static_cast<std::uint8_t>(b / n);
            out[1] = static_cast<std::uint8_t>(g / n);
            out[2] = static_cast<std::uint8_t>(r / n);
            out[3] = static_cast<std::uint8_t>(a / n);
        }
    }
}

std::string BasenameUtf8(const std::string& utf8_path) {
    return Path::ToUtf8(Path::FromUtf8(utf8_path).filename());
}

}  // namespace

ProxyManager::ProxyManager(MediaCatalog* catalog)
    : catalog_(catalog) {
    worker_ = std::thread(&ProxyManager::WorkerMain, this);
}

ProxyManager::~ProxyManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        // Mark everything cancelled — including the in-flight job —
        // so the frame provider aborts at the next decoded frame and
        // app shutdown never waits out a full 4K transcode.
        for (const Job& job : queue_) {
            cancelled_.insert(job.canon);
        }
        queue_.clear();
        if (!running_path_.empty()) {
            cancelled_.insert(running_path_);
        }
    }
    work_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string ProxyManager::ProxyFileName(const std::string& canon_path) {
    // FNV-1a 64 — stable across sessions, same-media-keyed (a
    // re-imported file hashes to the same proxy). Path case stays
    // as canonicalized: the proxies table joins under NOCASE but
    // filenames are case-stable on every supported filesystem.
    std::uint64_t h = 14695981039346656037ull;
    for (const char c : canon_path) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.mp4",
                  static_cast<unsigned long long>(h));
    return name;
}

bool ProxyManager::Request(const std::string& utf8_path,
                           const ProxySettings& settings) {
    if (catalog_ == nullptr || !catalog_->IsOpen()) {
        return false;
    }
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon)) {
        return false;
    }
    // Front-gates: only real video media gets a proxy. Audio-only
    // rows and stills (image2 probes — duration under the still
    // threshold, or codec "image") gain nothing from an h264 copy.
    CatalogEntry entry;
    if (catalog_->FindByPath(canon, &entry, nullptr)) {
        if (!entry.HasVideo() ||
            entry.duration_seconds <= kProbedStillMaxSeconds ||
            entry.video_codec == "image") {
            return false;
        }
    }
    const std::filesystem::path fs_path = Path::FromUtf8(canon);
    const std::int64_t src_size = FileSizeBytes(fs_path);
    if (src_size <= 0) {
        return false;   // source unreadable — not a proxy problem
    }
    // A ready+fresh proxy already covers this media — nothing to do.
    {
        ProxyRecord existing;
        if (catalog_->GetProxy(canon, &existing, nullptr) &&
            existing.status == ProxyStatus::kReady &&
            existing.src_size == src_size &&
            existing.src_mtime == FileMtimeUnix(fs_path) &&
            !existing.proxy_path.empty()) {
            std::error_code ec;
            if (std::filesystem::exists(
                    Path::FromUtf8(existing.proxy_path), ec)) {
                return true;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.count(canon) != 0u) {
            return true;   // already queued or encoding
        }
        pending_.insert(canon);
        Job job;
        job.canon = canon;
        job.settings = settings;
        queue_.push_back(std::move(job));
    }
    // Pending row first — the bin badge flips before the worker even
    // wakes (state writes are cheap SQL, safe off the worker).
    WriteState(canon, ProxyStatus::kPending,
               catalog_->ProxyDir() + "/" + ProxyFileName(canon),
               src_size, FileMtimeUnix(fs_path), settings.max_dim, "");
    work_cv_.notify_one();
    return true;
}

void ProxyManager::Cancel(const std::string& utf8_path) {
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon)) {
        return;
    }
    bool dropped_queued = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Only mark a canon that is actually queued or in-flight —
        // pending_ covers both. A stray mark on a finished/absent
        // job would otherwise sit in cancelled_ forever and silently
        // kill the NEXT Request for the same path (worker pops it,
        // sees "cancelled", skips — leaving a permanent pending row).
        if (pending_.count(canon) != 0u) {
            cancelled_.insert(canon);
        }
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->canon == canon) {
                it = queue_.erase(it);
                dropped_queued = true;
            } else {
                ++it;
            }
        }
        if (dropped_queued) {
            pending_.erase(canon);
            cancelled_.erase(canon);
        }
    }
    if (dropped_queued && catalog_ != nullptr) {
        // Queued job cancelled before it ran — the row goes away
        // entirely (back to "no proxy"), never to a half state.
        catalog_->RemoveProxy(canon, nullptr);
        catalog_dirty_.store(true, std::memory_order_release);
    }
    work_cv_.notify_all();
}

void ProxyManager::CancelAll() {
    std::deque<std::string> dropped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Job& job : queue_) {
            cancelled_.insert(job.canon);
            pending_.erase(job.canon);
            cancelled_.erase(job.canon);
            dropped.push_back(job.canon);
        }
        queue_.clear();
    }
    if (catalog_ != nullptr) {
        for (const std::string& canon : dropped) {
            catalog_->RemoveProxy(canon, nullptr);
        }
        if (!dropped.empty()) {
            catalog_dirty_.store(true, std::memory_order_release);
        }
    }
}

std::string ProxyManager::ResolveDecodePath(
    const std::string& utf8_path) const {
    if (catalog_ == nullptr || utf8_path.empty()) {
        return utf8_path;
    }
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon)) {
        return utf8_path;
    }
    ProxyRecord record;
    if (!catalog_->GetProxy(canon, &record, nullptr) ||
        record.status != ProxyStatus::kReady ||
        record.proxy_path.empty()) {
        return utf8_path;
    }
    // Fingerprint check: a source rewritten since generation must
    // silently fall back to the original — a stale proxy is a
    // wrong-pixel bug, not a perf miss.
    const std::filesystem::path fs_path = Path::FromUtf8(canon);
    if (FileSizeBytes(fs_path) != record.src_size ||
        FileMtimeUnix(fs_path) != record.src_mtime) {
        return utf8_path;
    }
    std::error_code ec;
    if (!std::filesystem::exists(Path::FromUtf8(record.proxy_path),
                                 ec)) {
        return utf8_path;   // proxy file deleted behind our back
    }
    return record.proxy_path;
}

bool ProxyManager::ConsumeCatalogDirty() {
    return catalog_dirty_.exchange(false, std::memory_order_acquire);
}

std::size_t ProxyManager::PendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

int ProxyManager::RunningCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_ ? 1 : 0;
}

double ProxyManager::ProgressFor(const std::string& utf8_path) const {
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon)) {
        return -1.0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || running_path_ != canon) {
        return -1.0;
    }
    return progress_.load(std::memory_order_relaxed);
}

std::string ProxyManager::DescribeActivity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return {};
    }
    const int pct = static_cast<int>(
        progress_.load(std::memory_order_relaxed) * 100.0);
    std::string text = "Proxy: " + BasenameUtf8(running_path_) + " " +
                       std::to_string(pct) + "%";
    if (!queue_.empty()) {
        text += " (+" + std::to_string(queue_.size()) + " queued)";
    }
    return text;
}

bool ProxyManager::WaitIdle(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return idle_cv_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms), [this] {
            return queue_.empty() && !running_;
        });
}

void ProxyManager::WriteState(const std::string& canon,
                              ProxyStatus status,
                              const std::string& proxy_path,
                              std::int64_t src_size,
                              std::int64_t src_mtime, int max_dim,
                              const std::string& error) {
    if (catalog_ == nullptr) {
        return;
    }
    ProxyRecord record;
    record.path = canon;
    record.proxy_path = proxy_path;
    record.status = status;
    record.src_size = src_size;
    record.src_mtime = src_mtime;
    record.max_dim = max_dim;
    record.error = error;
    if (catalog_->SetProxyRecord(record, nullptr)) {
        catalog_dirty_.store(true, std::memory_order_release);
    }
}

bool ProxyManager::IsCancelled(const std::string& canon) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cancelled_.count(canon) != 0u;
}

void ProxyManager::WorkerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_cv_.wait(lock, [this] {
                return stop_ || !queue_.empty();
            });
            if (stop_) {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            if (cancelled_.count(job.canon) != 0u) {
                pending_.erase(job.canon);
                cancelled_.erase(job.canon);
                continue;
            }
            running_ = true;
            running_path_ = job.canon;
            progress_.store(0.0, std::memory_order_relaxed);
        }
        RunJob(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            running_path_.clear();
            pending_.erase(job.canon);
            cancelled_.erase(job.canon);
        }
        idle_cv_.notify_all();
    }
}

void ProxyManager::RunJob(const Job& job) {
    const std::filesystem::path fs_path = Path::FromUtf8(job.canon);
    const std::int64_t src_size = FileSizeBytes(fs_path);
    const std::int64_t src_mtime = FileMtimeUnix(fs_path);
    const std::string proxy_path =
        catalog_->ProxyDir() + "/" + ProxyFileName(job.canon);
    if (IsCancelled(job.canon)) {
        catalog_->RemoveProxy(job.canon, nullptr);
        catalog_dirty_.store(true, std::memory_order_release);
        return;
    }
    std::string error;
    bool skipped = false;
    if (src_size <= 0) {
        error = "source unreadable";
    } else if (!Transcode(job, proxy_path, &error, &skipped)) {
        // error filled
    }
    if (skipped) {
        return;   // no proxy needed — row already removed in Transcode
    }
    if (IsCancelled(job.canon)) {
        // A cancel beats a success/failure write — the row dies and
        // any committed file goes with it.
        catalog_->RemoveProxy(job.canon, nullptr);
        std::error_code ec;
        std::filesystem::remove(Path::FromUtf8(proxy_path), ec);
        catalog_dirty_.store(true, std::memory_order_release);
        return;
    }
    if (error.empty()) {
        WriteState(job.canon, ProxyStatus::kReady, proxy_path, src_size,
                   src_mtime, job.settings.max_dim, "");
    } else {
        WriteState(job.canon, ProxyStatus::kFailed, proxy_path, src_size,
                   src_mtime, job.settings.max_dim, error);
    }
}

bool ProxyManager::Transcode(const Job& job,
                             const std::string& proxy_path,
                             std::string* error_out, bool* skipped_out) {
    if (skipped_out != nullptr) {
        *skipped_out = false;
    }
    if (!FFmpeg::IsAvailable()) {
        return Fail(error_out, "FFmpeg bridge unavailable");
    }
    const std::filesystem::path target = Path::FromUtf8(proxy_path);
    {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    // Temp-then-commit like export_engine: readers only ever see a
    // complete proxy or no proxy. The extension rides back on because
    // the encoder sniffs it for the container format.
    std::filesystem::path temp =
        XER::Cache::TempPathForTarget(target, "xerpxy");
    temp += target.extension();
    const std::string temp_utf8 = Path::ToUtf8(temp);
    {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
    }

    bool encoded = false;
    // Two attempts bound the retry: pass 1 estimates the frame count;
    // an under-run (VFR / optimistic duration) reruns once with the
    // exact count it just discovered. Anything else fails outright.
    std::size_t target_frames = 0;
    for (int pass = 0; pass < 2 && !encoded; ++pass) {
        FFmpeg::VideoDecoder decoder;
        std::string open_error;
        if (!decoder.Open(job.canon, &open_error)) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return Fail(error_out, "cannot open: " + open_error);
        }
        const int src_w = decoder.GetWidth();
        const int src_h = decoder.GetHeight();
        if (src_w <= 0 || src_h <= 0) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return Fail(error_out, "no video stream");
        }
        if (pass == 0) {
            // Skip-media gate: a source already under the cap gains
            // nothing — leave the row absent so ResolveDecodePath
            // simply falls through to the original.
            if (job.settings.max_dim > 0 &&
                std::max(src_w, src_h) <= job.settings.max_dim) {
                decoder.Close();
                std::error_code ec;
                std::filesystem::remove(temp, ec);
                if (catalog_ != nullptr) {
                    catalog_->RemoveProxy(job.canon, nullptr);
                    catalog_dirty_.store(true,
                                         std::memory_order_release);
                }
                if (skipped_out != nullptr) {
                    *skipped_out = true;
                }
                return true;   // job done — nothing to encode
            }
            const double duration = decoder.GetDurationSeconds();
            const double fps = decoder.GetFps();
            target_frames =
                (duration > 0.0 && fps > 0.0)
                    ? static_cast<std::size_t>(std::max<std::int64_t>(
                          1, std::llround(duration * fps)))
                    // Unknown container timing: SIZE_MAX drives the
                    // provider to natural EOF; the under-run retry
                    // then encodes the now-exact count cleanly.
                    : (std::numeric_limits<std::size_t>::max)();
        }
        const double duration = decoder.GetDurationSeconds();
        double fps = decoder.GetFps();
        if (!(fps > 0.0) || !std::isfinite(fps)) {
            fps = 30.0;
        }
        // Long-edge cap, aspect preserved, dims snapped even (yuv420).
        int dst_w = src_w;
        int dst_h = src_h;
        if (job.settings.max_dim > 0 &&
            std::max(src_w, src_h) > job.settings.max_dim) {
            const double scale =
                static_cast<double>(job.settings.max_dim) /
                std::max(src_w, src_h);
            dst_w = std::max(2, static_cast<int>(src_w * scale));
            dst_h = std::max(2, static_cast<int>(src_h * scale));
        }
        dst_w &= ~1;
        dst_h &= ~1;

        FFmpeg::EncodeVideoParams params;
        params.output_path = temp_utf8;
        params.codec_name =
            job.settings.codec.empty() ? "libx264" : job.settings.codec;
        params.width = dst_w;
        params.height = dst_h;
        params.pixel_format = "bgra";
        params.fps_num =
            static_cast<int>(std::llround(fps * 1001.0));
        params.fps_den = 1001;
        params.bit_rate = job.settings.bit_rate;
        params.crf = job.settings.crf;

        bool eof_hit = false;
        bool cancelled = false;
        std::size_t produced = 0;
        std::vector<std::uint8_t> bgra;
        std::vector<std::uint8_t> scaled;
        const auto provider = [&](std::size_t index,
                                  FFmpeg::VideoFrameInfo* frame,
                                  std::string* provider_error) -> bool {
            if (IsCancelled(job.canon)) {
                cancelled = true;
                if (provider_error != nullptr) {
                    *provider_error = "proxy cancelled";
                }
                return false;
            }
            int w = 0;
            int h = 0;
            if (!decoder.NextFrame(bgra, w, h) || w <= 0 || h <= 0 ||
                bgra.size() != static_cast<std::size_t>(w) *
                                   static_cast<std::size_t>(h) * 4u) {
                eof_hit = true;
                if (provider_error != nullptr) {
                    *provider_error = "source exhausted";
                }
                return false;
            }
            frame->width = dst_w;
            frame->height = dst_h;
            frame->pixel_format = "bgra";
            frame->pts = frame->best_effort_timestamp =
                static_cast<std::int64_t>(index);
            frame->line_sizes = {dst_w * 4, 0, 0, 0};
            if (dst_w == w && dst_h == h) {
                frame->data = bgra;
            } else {
                scaled.assign(
                    static_cast<std::size_t>(dst_w) *
                        static_cast<std::size_t>(dst_h) * 4u,
                    0);
                DownscaleBgra(bgra.data(), w, h, scaled.data(), dst_w,
                              dst_h);
                frame->data = scaled;
            }
            produced = index + 1;
            if (duration > 0.0) {
                const double pos = decoder.GetPositionSeconds();
                progress_.store(
                    std::clamp(pos / duration, 0.0, 1.0),
                    std::memory_order_relaxed);
            }
            return true;
        };

        std::string encode_error;
        encoded = FFmpeg::EncodeVideoFrameStream(
            params, target_frames, provider, &encode_error);
        decoder.Close();
        if (encoded) {
            break;
        }
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        if (cancelled) {
            return Fail(error_out, "cancelled");
        }
        // Under-run: the source produced FEWER frames than the
        // estimate — the aborted file is discarded and one exact-count
        // retry encodes it properly. produced==0 means the source
        // yields nothing decodable — no retry can fix that.
        if (eof_hit && produced > 0 &&
            produced < target_frames && pass == 0) {
            target_frames = produced;
            continue;
        }
        return Fail(error_out, encode_error.empty()
                                 ? "proxy encode failed"
                                 : encode_error);
    }

    if (!encoded) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return Fail(error_out, "proxy encode produced no output");
    }
    if (!XER::Cache::CommitTempFile(temp, target, error_out)) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return false;
    }
    progress_.store(1.0, std::memory_order_relaxed);
    return true;
}

}  // namespace XER::VideoEditor
