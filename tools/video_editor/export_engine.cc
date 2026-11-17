// export_engine.cc — see export_engine.h for the contract. The worker
// owns all Compositor/AudioMixer calls (their single-consumer-thread
// rule); Cancel() only flips atomics on this object and on the two
// engines so an in-flight RenderFrameAt/GetChunk unwinds through the
// normal provider-false path.
#include "export_engine.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <system_error>
#include <utility>

#include "cache/cache_utils.h"
#include "content/video/video_info.h"
#include "generated_source.h"
#include "helper/path_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::VideoEditor {
namespace {

namespace FFmpeg = XER::Bridge::FFmpeg;
namespace Path = XER::Helper::Path;

bool SetStartError(std::string* error_out, const std::string& error) {
    if (error_out != nullptr) {
        *error_out = error;
    }
    return false;
}

}  // namespace

ExportEngine::ExportEngine() = default;

ExportEngine::~ExportEngine() {
    Cancel();
    Wait();
}

bool ExportEngine::FailStart(std::string* error_out,
                             const std::string& error) {
    return SetStartError(error_out, error);
}

bool ExportEngine::Start(
    const Project& project, const ExportOptions& options,
    std::function<void(const ExportProgress&)> progress,
    std::string* error_out) {
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    if (status_.load(std::memory_order_acquire) == ExportStatus::kRunning) {
        return FailStart(error_out, "export already running");
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    const std::string output_path =
        Path::NormalizeUtf8Path(options.output_path);
    if (output_path.empty()) {
        return FailStart(error_out, "export output path is empty");
    }
    if (project.duration() <= 0.0) {
        return FailStart(error_out, "project has no exportable duration");
    }
    if (project.fps_num <= 0 || project.fps_den <= 0) {
        return FailStart(error_out, "invalid project fps");
    }
    if (!std::isfinite(options.resolution_scale) ||
        options.resolution_scale <= 0.0 ||
        project.width * options.resolution_scale < 2.0 ||
        project.height * options.resolution_scale < 2.0) {
        return FailStart(error_out, "invalid resolution_scale");
    }
    if (!FFmpeg::IsAvailable()) {
        return FailStart(error_out, "FFmpeg bridge unavailable");
    }

    // Probe each unique source once. Missing media is a Start-time
    // error so a broken project cannot leave a half-written target.
    std::map<std::string, MediaProbe> probes;
    for (const auto& clip : project.clips) {
        if (IsGeneratedSource(clip.source) || clip.is_adjustment) {
            // Procedural and adjustment clips own no media file —
            // nothing to probe.
            continue;
        }
        const std::string source =
            Path::NormalizeUtf8Path(clip.source_path);
        if (source.empty()) {
            return FailStart(error_out, "clip '" + clip.id +
                                        "' has an empty source path");
        }
        if (probes.find(source) != probes.end()) {
            continue;
        }
        XER::Content::Video::VideoInfo info;
        std::string probe_error;
        if (!XER::Content::Video::ProbeVideo(source, &info,
                                             &probe_error) ||
            !info.valid) {
            return FailStart(error_out,
                             "cannot probe '" + source + "': " +
                                 (probe_error.empty() ? "probe failed"
                                                      : probe_error));
        }
        MediaProbe probe;
        probe.has_video = info.width > 0 && info.height > 0;
        probe.has_audio = info.has_audio;
        probes.emplace(source, probe);
    }

    bool has_video = false;
    bool has_audio = false;
    for (const auto& clip : project.clips) {
        const Track* track = FindTrack(project, clip.track);
        if (track == nullptr) {
            continue;
        }
        MediaProbe generated_probe;
        const MediaProbe* probe = &generated_probe;
        if (IsGeneratedSource(clip.source)) {
            generated_probe.has_video = true;
        } else {
            const auto probe_it =
                probes.find(Path::NormalizeUtf8Path(clip.source_path));
            if (probe_it == probes.end()) {
                continue;
            }
            probe = &probe_it->second;
        }
        if (track->kind == TrackKind::Video && !track->hidden &&
            probe->has_video) {
            has_video = true;
        }
        const bool track_allows_audio =
            (track->kind == TrackKind::Audio && !track->muted) ||
            (track->kind == TrackKind::Video && !track->hidden);
        if (!clip.muted && track_allows_audio && probe->has_audio) {
            has_audio = true;
        }
    }
    // Audio-only convention (export_preset.cc "Audio only AAC"): an
    // empty video_codec means "mux audio only" — the clip probes still
    // describe the source media, but the export must not init the
    // compositor or feed the video encoder.
    if (options.video_codec.empty()) {
        has_video = false;
    }
    if (!has_video && !has_audio) {
        return FailStart(error_out, "project has no exportable media");
    }

    project_ = project;
    // resolution_scale resizes the output canvas. Transform offsets
    // and masks are canvas-relative, so only the canvas dims and the
    // pixel-absolute transform.scale (plus Scale keyframes, which
    // multiply it) need adjusting — the snapshot makes it export-local.
    if (options.resolution_scale != 1.0) {
        const double s = options.resolution_scale;
        const auto even_dim = [s](int dim) {
            return std::max(2, static_cast<int>(std::lround(dim * s)) &
                                   ~1);
        };
        project_.width = even_dim(project.width);
        project_.height = even_dim(project.height);
        for (auto& clip : project_.clips) {
            clip.transform.scale =
                static_cast<float>(clip.transform.scale * s);
            const auto keys = clip.keyframes.find(KeyProp::Scale);
            if (keys != clip.keyframes.end()) {
                for (auto& key : keys->second) {
                    key.value = static_cast<float>(key.value * s);
                }
            }
        }
    }
    options_ = options;
    options_.output_path = output_path;
    progress_cb_ = std::move(progress);
    has_video_ = has_video;
    has_audio_ = has_audio;
    cancel_requested_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        error_.clear();
        progress_ = ExportProgress{};
    }
    status_.store(ExportStatus::kRunning, std::memory_order_release);

    try {
        worker_ = std::thread(&ExportEngine::Run, this);
    } catch (const std::exception& e) {
        status_.store(ExportStatus::kError, std::memory_order_release);
        const std::string error =
            std::string("export worker failed to start: ") + e.what();
        FinishError(error);
        return FailStart(error_out, error);
    } catch (...) {
        status_.store(ExportStatus::kError, std::memory_order_release);
        const std::string error = "export worker failed to start";
        FinishError(error);
        return FailStart(error_out, error);
    }
    return true;
}

void ExportEngine::Cancel() {
    cancel_requested_.store(true, std::memory_order_release);
    compositor_.Cancel();
    mixer_.Cancel();
}

bool ExportEngine::Wait() {
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    if (worker_.joinable()) {
        worker_.join();
    }
    return status_.load(std::memory_order_acquire) !=
           ExportStatus::kRunning;
}

bool ExportEngine::IsRunning() const {
    return status_.load(std::memory_order_acquire) ==
           ExportStatus::kRunning;
}

ExportStatus ExportEngine::Status() const {
    return status_.load(std::memory_order_acquire);
}

ExportProgress ExportEngine::Progress() const {
    std::lock_guard<std::mutex> progress_lock(progress_mutex_);
    return progress_;
}

std::string ExportEngine::Error() const {
    std::lock_guard<std::mutex> progress_lock(progress_mutex_);
    return error_;
}

void ExportEngine::Run() {
    try {
        RunImpl();
    } catch (const std::exception& e) {
        FinishError(std::string("export worker exception: ") + e.what());
    } catch (...) {
        FinishError("export worker exception");
    }
    ShutdownEngines();
}

void ExportEngine::RunImpl() {
    Report("init");
    if (cancel_requested_.load(std::memory_order_acquire)) {
        FinishCancelled();
        return;
    }

    const std::filesystem::path target_path =
        Path::FromUtf8(options_.output_path);
    std::filesystem::path temp_path =
        XER::Cache::TempPathForTarget(target_path, "xerexp");
    temp_path += target_path.extension();
    const std::string temp_utf8 = Path::ToUtf8(temp_path);

    std::error_code fs_error;
    std::filesystem::remove(temp_path, fs_error);
    fs_error.clear();
    if (temp_path.has_parent_path()) {
        // Best-effort only: an uncreatable parent is reported by the
        // encoder/commit path below with the real OS error.
        std::filesystem::create_directories(temp_path.parent_path(),
                                            fs_error);
    }

    std::string error;
    if (has_video_ &&
        !compositor_.Init(project_, CompositorOptions{}, &error)) {
        FinishError(error.empty() ? "compositor init failed" : error);
        return;
    }
    AudioMixerOptions mixer_options;
    mixer_options.master_gain_db = options_.master_gain_db;
    if (has_audio_ &&
        !mixer_.Init(project_, mixer_options, &error)) {
        FinishError(error.empty() ? "audio mixer init failed" : error);
        return;
    }
    if (cancel_requested_.load(std::memory_order_acquire)) {
        FinishCancelled();
        return;
    }

    // Resolve the export range: an explicit options range wins; else
    // the project work area (work_out > work_in); else the whole
    // project. Non-finite/empty ranges fall through to the next
    // source; the clamped result collapsing to empty falls back to
    // the whole project too.
    const double project_dur = project_.duration();
    double range_in = 0.0;
    double range_out = project_dur;
    {
        double ri = options_.range_in;
        double ro = options_.range_out;
        if (!std::isfinite(ri) || !std::isfinite(ro) || !(ro > ri)) {
            ri = project_.work_in;
            ro = project_.work_out;
        }
        if (std::isfinite(ri) && std::isfinite(ro) && ro > ri) {
            range_in = std::clamp(ri, 0.0, project_dur);
            range_out = std::clamp(ro, 0.0, project_dur);
        }
        if (!(range_out > range_in)) {
            range_in = 0.0;
            range_out = project_dur;
        }
    }
    const double range_dur = range_out - range_in;
    // "ranged" = a strict sub-range of the project. Audio chunks then
    // need sample-exact slicing (range edges rarely align to the
    // absolute chunk grid); the whole-project case keeps the direct
    // GetChunk(index) path with identical chunk counts.
    const bool ranged = range_in > 0.0 || range_out < project_dur;

    const std::uint64_t video_total =
        has_video_ ? static_cast<std::uint64_t>(std::max<std::int64_t>(
                         1,
                         std::llround(
                             range_dur *
                             static_cast<double>(project_.fps_num) /
                             static_cast<double>(project_.fps_den))))
                   : 0;
    // Absolute output-rate sample index where the range begins, and
    // how many samples the range spans (>= 1 — a positive-duration
    // range always exports at least one sample).
    const std::size_t audio_first = static_cast<std::size_t>(std::max(
        std::int64_t{0},
        std::llround(range_in * mixer_options.sample_rate)));
    const std::size_t audio_span = static_cast<std::size_t>(std::max(
        std::int64_t{1},
        std::llround(range_dur * mixer_options.sample_rate)));
    const std::uint64_t audio_total =
        has_audio_
            ? static_cast<std::uint64_t>(
                  ranged ? (audio_span +
                            static_cast<std::size_t>(
                                mixer_options.chunk_samples) -
                            1) /
                               static_cast<std::size_t>(
                                   mixer_options.chunk_samples)
                         : mixer_.ChunkCount())
            : 0;

    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        progress_.video_frames_total = video_total;
        progress_.audio_chunks_total = audio_total;
    }
    if (has_video_ && video_total == 0) {
        FinishError("project produced zero video frames");
        return;
    }
    if (has_audio_ && audio_total == 0) {
        FinishError("project produced zero audio chunks");
        return;
    }

    const bool mux = has_video_ && has_audio_;
    const char* video_stage = mux ? "mux" : "video";
    const char* audio_stage = mux ? "mux" : "audio";

    auto video_provider = [&](std::size_t frame_index,
                              FFmpeg::VideoFrameInfo* out_frame,
                              std::string* provider_error) -> bool {
        if (cancel_requested_.load(std::memory_order_acquire)) {
            if (provider_error != nullptr) {
                *provider_error = "export cancelled";
            }
            return false;
        }
        if (out_frame == nullptr) {
            if (provider_error != nullptr) {
                *provider_error = "video provider received null output";
            }
            return false;
        }
        // Frame i renders timeline t = range_in + i/fps (range_in is
        // 0 for whole-project exports — identical to before).
        const double t = range_in +
                         static_cast<double>(frame_index) *
                             static_cast<double>(project_.fps_den) /
                             static_cast<double>(project_.fps_num);
        std::vector<std::uint8_t> bgra;
        if (!compositor_.RenderFrameAt(t, &bgra, provider_error)) {
            return false;
        }
        out_frame->width = compositor_.CanvasWidth();
        out_frame->height = compositor_.CanvasHeight();
        out_frame->pixel_format = "bgra";
        out_frame->pts = static_cast<std::int64_t>(frame_index);
        out_frame->best_effort_timestamp =
            static_cast<std::int64_t>(frame_index);
        out_frame->line_sizes = {out_frame->width * 4, 0, 0, 0};
        out_frame->data = std::move(bgra);
        UpdateDone(static_cast<std::uint64_t>(frame_index) + 1,
                   Progress().audio_chunks_done, video_stage);
        return true;
    };

    // Ranged-audio slicing state: the mixer produces chunks on the
    // ABSOLUTE chunk grid ([m*cs, m*cs+count)) while a range can start
    // mid-chunk, so ranged exports consume the absolute stream through
    // a one-chunk lookahead that copies sample-exact slices. Positions
    // are absolute output-rate sample indices on the timeline. Chunks
    // are fetched in strictly non-decreasing order — the mixer's
    // sequential-production contract.
    FFmpeg::AudioFrameInfo slice_src;
    std::size_t slice_src_index = 0;
    bool slice_src_have = false;

    auto audio_provider = [&](std::size_t chunk_index,
                              FFmpeg::AudioFrameInfo* out_frame,
                              std::string* provider_error) -> bool {
        if (cancel_requested_.load(std::memory_order_acquire)) {
            if (provider_error != nullptr) {
                *provider_error = "export cancelled";
            }
            return false;
        }
        if (out_frame == nullptr) {
            if (provider_error != nullptr) {
                *provider_error = "audio provider received null output";
            }
            return false;
        }
        if (!ranged) {
            if (!mixer_.GetChunk(chunk_index, out_frame,
                                 provider_error)) {
                return false;
            }
        } else {
            const std::size_t cs = static_cast<std::size_t>(
                mixer_options.chunk_samples);
            // Exported sample span [base, base+n) in absolute indices
            // — base < audio_first + audio_span by construction of
            // audio_total, so n >= 1.
            const std::size_t base = audio_first + chunk_index * cs;
            const std::size_t end =
                std::min(base + cs, audio_first + audio_span);
            const std::size_t n = end - base;
            out_frame->stream_index = -1;
            out_frame->sample_rate = mixer_options.sample_rate;
            out_frame->channels = mixer_options.channels;
            out_frame->sample_count = static_cast<int>(n);
            out_frame->planar = false;
            out_frame->sample_format = "s16";
            out_frame->pts = static_cast<std::int64_t>(chunk_index) *
                             static_cast<std::int64_t>(cs);
            out_frame->best_effort_timestamp = out_frame->pts;
            out_frame->plane_sizes.clear();
            out_frame->data.assign(n * 4u, 0);
            std::size_t done = 0;
            while (done < n) {
                const std::size_t pos = base + done;
                const std::size_t m = pos / cs;
                if (!slice_src_have || slice_src_index != m) {
                    if (!mixer_.GetChunk(m, &slice_src,
                                         provider_error)) {
                        return false;
                    }
                    slice_src_index = m;
                    slice_src_have = true;
                }
                const std::size_t off = pos - m * cs;
                std::size_t avail = 0;
                if (off < static_cast<std::size_t>(
                              slice_src.sample_count)) {
                    avail = static_cast<std::size_t>(
                                slice_src.sample_count) -
                            off;
                }
                if (avail == 0) {
                    // pos ran past a short (final) mixer chunk —
                    // unreachable while the range stays inside
                    // [0, duration]; the tail stays zero-filled.
                    break;
                }
                const std::size_t take = std::min(avail, n - done);
                std::memcpy(out_frame->data.data() + done * 4u,
                            slice_src.data.data() + off * 4u,
                            take * 4u);
                done += take;
            }
        }
        UpdateDone(Progress().video_frames_done,
                   static_cast<std::uint64_t>(chunk_index) + 1,
                   audio_stage);
        return true;
    };

    FFmpeg::EncodeVideoParams video_params;
    video_params.output_path = temp_utf8;
    video_params.codec_name = options_.video_codec;
    video_params.width = project_.width;
    video_params.height = project_.height;
    video_params.pixel_format = "bgra";
    video_params.fps_num = project_.fps_num;
    video_params.fps_den = project_.fps_den;
    video_params.bit_rate = options_.video_bit_rate;

    FFmpeg::EncodeAudioParams audio_params;
    audio_params.output_path = temp_utf8;
    audio_params.codec_name = options_.audio_codec;
    audio_params.sample_rate = mixer_options.sample_rate;
    audio_params.channels = mixer_options.channels;
    audio_params.sample_format = "s16";
    audio_params.bit_rate = options_.audio_bit_rate;

    bool encoded = false;
    if (mux) {
        Report("mux");
        FFmpeg::EncodeAudioVideoParams params;
        params.video = video_params;
        params.audio = audio_params;
        encoded = FFmpeg::EncodeAudioVideoStream(
            params, static_cast<std::size_t>(video_total), video_provider,
            static_cast<std::size_t>(audio_total), audio_provider, &error);
    } else if (has_video_) {
        Report("video");
        encoded = FFmpeg::EncodeVideoFrameStream(
            video_params, static_cast<std::size_t>(video_total),
            video_provider, &error);
    } else {
        Report("audio");
        encoded = FFmpeg::EncodeAudioStream(
            audio_params, static_cast<std::size_t>(audio_total),
            audio_provider, &error);
    }

    if (!encoded) {
        std::filesystem::remove(temp_path, fs_error);
        if (cancel_requested_.load(std::memory_order_acquire)) {
            FinishCancelled();
        } else {
            FinishError(error.empty() ? "export encode failed" : error);
        }
        return;
    }
    if (cancel_requested_.load(std::memory_order_acquire)) {
        std::filesystem::remove(temp_path, fs_error);
        FinishCancelled();
        return;
    }

    Report("commit");
    if (cancel_requested_.load(std::memory_order_acquire)) {
        std::filesystem::remove(temp_path, fs_error);
        FinishCancelled();
        return;
    }
    if (!XER::Cache::CommitTempFile(temp_path, target_path, &error)) {
        std::filesystem::remove(temp_path, fs_error);
        FinishError(error.empty() ? "failed to commit export" : error);
        return;
    }

    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        progress_.video_frames_done = progress_.video_frames_total;
        progress_.audio_chunks_done = progress_.audio_chunks_total;
        progress_.fraction = 1.0;
        progress_.stage = "commit";
    }
    if (progress_cb_) {
        progress_cb_(Progress());
    }
    status_.store(ExportStatus::kDone, std::memory_order_release);
}

void ExportEngine::Report(const char* stage) {
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        progress_.stage = stage != nullptr ? stage : "init";
    }
    if (progress_cb_) {
        progress_cb_(Progress());
    }
}

void ExportEngine::UpdateDone(std::uint64_t video_done,
                              std::uint64_t audio_done,
                              const char* stage) {
    ExportProgress snapshot;
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        progress_.video_frames_done = std::max(
            progress_.video_frames_done, video_done);
        progress_.audio_chunks_done = std::max(
            progress_.audio_chunks_done, audio_done);
        progress_.stage = stage != nullptr ? stage : progress_.stage;

        const bool both = progress_.video_frames_total > 0 &&
                          progress_.audio_chunks_total > 0;
        double fraction = 0.0;
        if (both) {
            fraction =
                0.5 * (static_cast<double>(progress_.video_frames_done) /
                       static_cast<double>(progress_.video_frames_total)) +
                0.5 * (static_cast<double>(progress_.audio_chunks_done) /
                       static_cast<double>(progress_.audio_chunks_total));
        } else if (progress_.video_frames_total > 0) {
            fraction = static_cast<double>(progress_.video_frames_done) /
                       static_cast<double>(progress_.video_frames_total);
        } else if (progress_.audio_chunks_total > 0) {
            fraction = static_cast<double>(progress_.audio_chunks_done) /
                       static_cast<double>(progress_.audio_chunks_total);
        }
        progress_.fraction =
            std::clamp(fraction, progress_.fraction, 1.0);
        snapshot = progress_;
    }
    if (progress_cb_) {
        progress_cb_(snapshot);
    }
}

void ExportEngine::ShutdownEngines() {
    compositor_.Shutdown();
    mixer_.Shutdown();
}

void ExportEngine::FinishCancelled() {
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        error_ = "export cancelled";
    }
    status_.store(ExportStatus::kCancelled, std::memory_order_release);
}

void ExportEngine::FinishError(const std::string& error) {
    {
        std::lock_guard<std::mutex> progress_lock(progress_mutex_);
        error_ = error.empty() ? "export failed" : error;
    }
    status_.store(ExportStatus::kError, std::memory_order_release);
}

}  // namespace XER::VideoEditor
