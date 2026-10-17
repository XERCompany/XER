#include "music_bass_eq_clip.h"

#include "audio/analysis_ai/audio_fft_analyzer.h"
#include "audio/audio_core/audio_source_loader.h"
#include "cache/cache_utils.h"
#include "clip_frame_cache.h"
#include "clip_gpu_accel.h"
#include "frame.h"
#include "frame_exporter.h"
#include "rotating_cube_scene.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <filesystem>
#include <future>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>

namespace video {

namespace {

constexpr double kFallbackClipSeconds = 15.0;
constexpr size_t kFftSize = 2048u;

struct RgbColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct NormalizedClipOptions {
    std::filesystem::path output_path;
    std::filesystem::path output_dir;
    double duration_seconds = kFallbackClipSeconds;
    int width = 1280;
    int height = 720;
    int fps = 30;
    int sample_rate = 44100;
    std::string codec_name = "libx264";
};

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

int ClampEven(int value, int minimum, int maximum) {
    value = std::clamp(value, minimum, maximum);
    return value % 2 == 0 ? value : value - 1;
}

float SmoothStep(float value) {
    value = Clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

unsigned CpuWorkerCount(size_t work_items) {
    if (work_items == 0u) return 1u;
    const unsigned hardware = std::thread::hardware_concurrency();
    return static_cast<unsigned>(std::min<size_t>(work_items, std::max(1u, hardware)));
}

void SetError(std::string* error_out, const std::string& error) {
    if (error_out != nullptr) {
        *error_out = error;
    }
}

NormalizedClipOptions NormalizeOptions(const MusicBassEqClipOptions& options) {
    NormalizedClipOptions normalized;
    normalized.duration_seconds = std::max(0.25, options.duration_seconds);
    normalized.width = ClampEven(options.width, 320, 3840);
    normalized.height = ClampEven(options.height, 180, 2160);
    normalized.fps = std::clamp(options.fps, 1, 60);
    normalized.sample_rate = std::clamp(options.target_sample_rate, 8000, 192000);
    normalized.codec_name = options.codec_name.empty() ? "libx264" : options.codec_name;

    normalized.output_dir = options.output_dir.empty()
        ? std::filesystem::path("out/music_bass_eq_clip")
        : options.output_dir;
    normalized.output_path = options.output_path.empty()
        ? normalized.output_dir / "music_bass_eq_clip.mp4"
        : options.output_path;
    if (normalized.output_dir.empty()) {
        normalized.output_dir = normalized.output_path.parent_path();
    }
    return normalized;
}

std::string BuildFrameCacheKey(
    const MusicBassEqClipOptions& options,
    const NormalizedClipOptions& normalized,
    const Engine::Audio::Core::AudioSourceBuffer& audio_buffer) {
    std::error_code fs_error;
    auto file_size = std::filesystem::file_size(options.input_path, fs_error);
    if (fs_error) {
        file_size = 0;
    }

    std::ostringstream key;
    key << "music_bass_eq_clip_frames_v3\n";
    key << "input=" << options.input_path.lexically_normal().string() << "\n";
    key << "file_size=" << file_size << "\n";
    key << "original_sample_rate=" << audio_buffer.original_sample_rate << "\n";
    key << "original_channels=" << audio_buffer.original_channels << "\n";
    key << "original_frames=" << audio_buffer.original_frame_count << "\n";
    key << "sample_rate=" << audio_buffer.sample_rate << "\n";
    key << "frames=" << audio_buffer.frame_count << "\n";
    key << "width=" << normalized.width << "\n";
    key << "height=" << normalized.height << "\n";
    key << "fps=" << normalized.fps << "\n";
    key << "duration=" << normalized.duration_seconds << "\n";
    return Engine::Cache::HashKey(key.str());
}

void PutPixel(Frame& frame, int x, int y, RgbColor color, uint8_t alpha = 255) {
    if (x < 0 || y < 0 || x >= frame.Width() || y >= frame.Height()) return;
    uint8_t* pixel = frame.Data() + (static_cast<size_t>(y) * frame.Width() + x) * 4u;
    pixel[0] = color.b;
    pixel[1] = color.g;
    pixel[2] = color.r;
    pixel[3] = alpha;
}

RgbColor Mix(RgbColor a, RgbColor b, float amount) {
    amount = Clamp01(amount);
    const float inverse = 1.0f - amount;
    return RgbColor{
        static_cast<uint8_t>(std::clamp(a.r * inverse + b.r * amount, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(a.g * inverse + b.g * amount, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(a.b * inverse + b.b * amount, 0.0f, 255.0f)),
    };
}

void FillRect(Frame& frame, int left, int top, int right, int bottom, RgbColor color) {
    left = std::clamp(left, 0, frame.Width());
    right = std::clamp(right, 0, frame.Width());
    top = std::clamp(top, 0, frame.Height());
    bottom = std::clamp(bottom, 0, frame.Height());
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            PutPixel(frame, x, y, color);
        }
    }
}

void FillCircle(Frame& frame, int center_x, int center_y, int radius, RgbColor color) {
    if (radius <= 0) return;
    const int radius_sq = radius * radius;
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
        for (int x = center_x - radius; x <= center_x + radius; ++x) {
            const int dx = x - center_x;
            const int dy = y - center_y;
            if (dx * dx + dy * dy <= radius_sq) PutPixel(frame, x, y, color);
        }
    }
}

void DrawBackground(Frame& frame, float bass, float high, size_t frame_index) {
    (void)bass;
    (void)high;
    (void)frame_index;
    for (int y = 0; y < frame.Height(); ++y) {
        for (int x = 0; x < frame.Width(); ++x) {
            PutPixel(frame, x, y, RgbColor{0, 0, 0});
        }
    }
}

void RenderEqBars(Frame& frame, const MusicBassEqFramePoint& point) {
    const std::array<float, 4> values{point.bass, point.low_mid, point.mid, point.high};
    const std::array<RgbColor, 4> colors{
        RgbColor{255, 75, 64},
        RgbColor{255, 185, 84},
        RgbColor{75, 210, 185},
        RgbColor{82, 150, 255},
    };
    const int margin = std::max(24, frame.Width() / 24);
    const int gap = std::max(10, frame.Width() / 90);
    const int usable = frame.Width() - margin * 2 - gap * 3;
    const int bar_width = std::max(12, usable / 4);
    const int base_y = frame.Height() - std::max(38, frame.Height() / 16);
    const int max_height = std::max(32, frame.Height() / 3);

    for (int band = 0; band < 4; ++band) {
        const float eased = SmoothStep(values[static_cast<size_t>(band)]);
        const int bar_height = static_cast<int>(max_height * (0.12f + eased * 0.88f));
        const int left = margin + band * (bar_width + gap);
        const int top = base_y - bar_height;
        FillRect(frame, left, top, left + bar_width, base_y, colors[static_cast<size_t>(band)]);
        FillRect(frame, left, base_y + 4, left + bar_width, base_y + 8, Mix(colors[static_cast<size_t>(band)], RgbColor{255, 255, 255}, 0.35f));
    }
}

void DrawTimeline(Frame& frame, float progress) {
    const int margin = std::max(24, frame.Width() / 24);
    const int y = frame.Height() - 18;
    FillRect(frame, margin, y, frame.Width() - margin, y + 4, RgbColor{38, 45, 56});
    const int filled = margin + static_cast<int>((frame.Width() - margin * 2) * Clamp01(progress));
    FillRect(frame, margin, y, filled, y + 4, RgbColor{245, 198, 93});
}

std::shared_ptr<Frame> RenderMusicFrame(
    const NormalizedClipOptions& options,
    const MusicBassEqFramePoint& point,
    size_t frame_index,
    size_t frame_count) {
    auto frame = std::make_shared<Frame>();
    if (!frame->Allocate(options.width, options.height, PixelFormat::BGRA8)) return nullptr;

    DrawBackground(*frame, point.bass, point.high, frame_index);
    RenderRotatingCubeScene(*frame, RotatingCubeSceneOptions{
        point.time_seconds,
        point.bass,
        point.low_mid,
        point.mid,
        point.high,
        point.rms,
    });

    (void)frame_count;
    frame->SetPts(static_cast<int64_t>(frame_index));
    return frame;
}

float BandEnergy(
    const std::vector<float>& magnitude,
    int sample_rate,
    size_t fft_size,
    float min_hz,
    float max_hz) {
    if (magnitude.empty() || sample_rate <= 0 || max_hz <= min_hz) return 0.0f;
    const float bin_hz = static_cast<float>(sample_rate) / static_cast<float>(fft_size);
    const size_t first_bin = std::clamp(static_cast<size_t>(std::floor(min_hz / bin_hz)), size_t{0}, magnitude.size() - 1u);
    const size_t last_bin = std::clamp(static_cast<size_t>(std::ceil(max_hz / bin_hz)), first_bin, magnitude.size() - 1u);
    double sum = 0.0;
    size_t count = 0;
    for (size_t bin = first_bin; bin <= last_bin; ++bin) {
        sum += static_cast<double>(magnitude[bin]) * magnitude[bin];
        ++count;
    }
    return count == 0 ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

bool AnalyzeBassEqTimeline(
    const std::vector<float>& samples,
    int sample_rate,
    const NormalizedClipOptions& options,
    MusicBassEqClipResult* result_out,
    std::string* error_out) {
    const size_t clip_audio_frames = std::min(
        samples.size(),
        static_cast<size_t>(std::ceil(options.duration_seconds * static_cast<double>(sample_rate))));
    if (clip_audio_frames == 0) {
        SetError(error_out, "music clip input has no samples inside the requested duration");
        return false;
    }

    const double duration = std::min(options.duration_seconds, static_cast<double>(clip_audio_frames) / sample_rate);
    const size_t video_frame_count = std::max<size_t>(1u, static_cast<size_t>(std::ceil(duration * options.fps)));
    result_out->eq_points.assign(video_frame_count, {});

    std::atomic_bool analysis_failed{false};
    std::mutex analysis_error_mutex;
    std::string analysis_error;

    auto analyze_range = [&](size_t begin, size_t end) {
        Engine::Audio::AnalysisAI::AudioFFTAnalyzer analyzer(kFftSize);
        std::vector<float> window(kFftSize, 0.0f);
        for (size_t frame_index = begin; frame_index < end && !analysis_failed.load(); ++frame_index) {
            const double time_seconds = static_cast<double>(frame_index) / static_cast<double>(options.fps);
            const size_t center = std::min(
                clip_audio_frames - 1u,
                static_cast<size_t>(std::round(time_seconds * sample_rate)));
            const size_t half = kFftSize / 2u;
            const size_t start = center > half ? center - half : 0u;
            std::fill(window.begin(), window.end(), 0.0f);
            const size_t copy_count = std::min(kFftSize, clip_audio_frames - start);
            std::copy_n(samples.data() + static_cast<std::ptrdiff_t>(start), copy_count, window.data());

            if (!analyzer.AnalyzeSpectrum(window.data(), window.size())) {
                std::lock_guard<std::mutex> lock(analysis_error_mutex);
                analysis_error = "bass EQ FFT analysis failed";
                analysis_failed.store(true);
                return;
            }

            auto& point = result_out->eq_points[frame_index];
            const auto& magnitude = analyzer.GetMagnitudeSpectrum();
            point.time_seconds = time_seconds;
            point.bass = BandEnergy(magnitude, sample_rate, kFftSize, 20.0f, 160.0f);
            point.low_mid = BandEnergy(magnitude, sample_rate, kFftSize, 160.0f, 400.0f);
            point.mid = BandEnergy(magnitude, sample_rate, kFftSize, 400.0f, 2500.0f);
            point.high = BandEnergy(magnitude, sample_rate, kFftSize, 2500.0f, 10000.0f);
            point.rms = analyzer.GetRMSEnergy();
            point.dominant_hz = analyzer.GetDominantFrequency(sample_rate);
        }
    };

    const size_t analysis_worker_count = CpuWorkerCount(video_frame_count);
    std::vector<std::future<void>> analysis_futures;
    analysis_futures.reserve(analysis_worker_count > 0u ? analysis_worker_count - 1u : 0u);
    for (size_t worker = 1u; worker < analysis_worker_count; ++worker) {
        const size_t begin = video_frame_count * worker / analysis_worker_count;
        const size_t end = video_frame_count * (worker + 1u) / analysis_worker_count;
        analysis_futures.push_back(std::async(std::launch::async, analyze_range, begin, end));
    }
    analyze_range(0u, video_frame_count / analysis_worker_count);
    for (auto& future : analysis_futures) {
        future.get();
    }
    if (analysis_failed.load()) {
        SetError(error_out, analysis_error.empty() ? "bass EQ FFT analysis failed" : analysis_error);
        return false;
    }

    result_out->bass_peak = 0.0f;
    result_out->low_mid_peak = 0.0f;
    result_out->mid_peak = 0.0f;
    result_out->high_peak = 0.0f;
    result_out->rms_peak = 0.0f;
    for (const auto& point : result_out->eq_points) {
        result_out->bass_peak = std::max(result_out->bass_peak, point.bass);
        result_out->low_mid_peak = std::max(result_out->low_mid_peak, point.low_mid);
        result_out->mid_peak = std::max(result_out->mid_peak, point.mid);
        result_out->high_peak = std::max(result_out->high_peak, point.high);
        result_out->rms_peak = std::max(result_out->rms_peak, point.rms);
    }

    const float bass_peak = std::max(result_out->bass_peak, 1.0e-6f);
    const float low_mid_peak = std::max(result_out->low_mid_peak, 1.0e-6f);
    const float mid_peak = std::max(result_out->mid_peak, 1.0e-6f);
    const float high_peak = std::max(result_out->high_peak, 1.0e-6f);
    const float rms_peak = std::max(result_out->rms_peak, 1.0e-6f);

    double bass_sum = 0.0;
    for (auto& point : result_out->eq_points) {
        point.bass = Clamp01(point.bass / bass_peak);
        point.low_mid = Clamp01(point.low_mid / low_mid_peak);
        point.mid = Clamp01(point.mid / mid_peak);
        point.high = Clamp01(point.high / high_peak);
        point.rms = Clamp01(point.rms / rms_peak);
        bass_sum += point.bass;
    }
    result_out->bass_average = result_out->eq_points.empty()
        ? 0.0f
        : static_cast<float>(bass_sum / static_cast<double>(result_out->eq_points.size()));
    result_out->duration_seconds = duration;
    result_out->audio_frames_used = clip_audio_frames;
    result_out->video_frames_written = video_frame_count;
    result_out->sample_rate = sample_rate;
    return true;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string* error_out) {
    std::ofstream output(path);
    if (!output.is_open()) {
        SetError(error_out, "failed to open music bass EQ report: " + path.string());
        return false;
    }
    output << text;
    if (!output) {
        SetError(error_out, "failed to write music bass EQ report: " + path.string());
        return false;
    }
    return true;
}

bool WriteEqCsv(const std::filesystem::path& path, const std::vector<MusicBassEqFramePoint>& points, std::string* error_out) {
    std::ofstream output(path);
    if (!output.is_open()) {
        SetError(error_out, "failed to open music bass EQ CSV: " + path.string());
        return false;
    }
    output << "frame,time_seconds,bass,low_mid,mid,high,rms,dominant_hz\n";
    output << std::fixed << std::setprecision(6);
    for (size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        output << index << "," << point.time_seconds << "," << point.bass << "," << point.low_mid << ","
               << point.mid << "," << point.high << "," << point.rms << "," << point.dominant_hz << "\n";
    }
    if (!output) {
        SetError(error_out, "failed to write music bass EQ CSV: " + path.string());
        return false;
    }
    return true;
}

}  // namespace

std::string BuildMusicBassEqClipReport(const MusicBassEqClipOptions& options, const MusicBassEqClipResult& result) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6);
    output << "Music Bass EQ Clip\n";
    output << "input_path=" << options.input_path.string() << "\n";
    output << "output_path=" << result.output_path.string() << "\n";
    output << "duration_seconds=" << result.duration_seconds << "\n";
    output << "width=" << result.width << "\n";
    output << "height=" << result.height << "\n";
    output << "fps=" << result.fps << "\n";
    output << "sample_rate=" << result.sample_rate << "\n";
    output << "audio_frames_used=" << result.audio_frames_used << "\n";
    output << "video_frames_written=" << result.video_frames_written << "\n";
    output << "codec_name=" << result.codec_name << "\n";
    output << "bass_peak=" << result.bass_peak << "\n";
    output << "bass_average_normalized=" << result.bass_average << "\n";
    output << "low_mid_peak=" << result.low_mid_peak << "\n";
    output << "mid_peak=" << result.mid_peak << "\n";
    output << "high_peak=" << result.high_peak << "\n";
    output << "rms_peak=" << result.rms_peak << "\n";
    output << "visual_scene=rotating_3d_cube\n";
    output << "assimp_enabled=" << (RotatingCubeSceneUsesAssimp() ? "true" : "false") << "\n";
    output << "opengl_enabled=" << (result.opengl_enabled ? "true" : "false") << "\n";
    output << "opencl_enabled=" << (result.opencl_enabled ? "true" : "false") << "\n";
    output << "opencl_postprocess_used=" << (result.opencl_postprocess_used ? "true" : "false") << "\n";
    output << "vulkan_enabled=" << (result.vulkan_enabled ? "true" : "false") << "\n";
    output << "vulkan_frame_accel_used=" << (result.vulkan_frame_accel_used ? "true" : "false") << "\n";
    output << "frame_cache_enabled=" << (result.frame_cache_enabled ? "true" : "false") << "\n";
    output << "frame_cache_frames=" << result.frame_cache_frames << "\n";
    if (!result.frame_cache_root.empty()) output << "frame_cache_root=" << result.frame_cache_root.string() << "\n";
    output << "audio_muxed=false\n";
    if (!result.report_path.empty()) output << "report_path=" << result.report_path.string() << "\n";
    if (!result.eq_csv_path.empty()) output << "eq_csv_path=" << result.eq_csv_path.string() << "\n";
    output << "clip_status=pass\n";
    return output.str();
}

bool GenerateMusicBassEqClip(
    const MusicBassEqClipOptions& options,
    MusicBassEqClipResult* result_out,
    std::string* error_out) {
    if (result_out == nullptr) {
        SetError(error_out, "music bass EQ clip result target is null");
        return false;
    }
    *result_out = MusicBassEqClipResult{};
    if (options.input_path.empty()) {
        SetError(error_out, "music bass EQ clip requires an input music file");
        return false;
    }
    if (!engine::bridge::ffmpeg::IsAvailable()) {
        SetError(error_out, "FFmpeg bridge is required for music bass EQ clip generation");
        return false;
    }

    NormalizedClipOptions normalized = NormalizeOptions(options);
    std::error_code fs_error;
    std::filesystem::create_directories(normalized.output_dir, fs_error);
    if (fs_error) {
        SetError(error_out, "failed to create music bass EQ clip output directory");
        return false;
    }
    if (!normalized.output_path.parent_path().empty()) {
        std::filesystem::create_directories(normalized.output_path.parent_path(), fs_error);
        if (fs_error) {
            SetError(error_out, "failed to create music bass EQ clip output parent directory");
            return false;
        }
    }

    Engine::Audio::Core::AudioSourceLoadOptions load_options;
    load_options.input_path = options.input_path;
    load_options.raw_sample_rate = normalized.sample_rate;
    load_options.target_sample_rate = normalized.sample_rate;
    load_options.target_channels = 1;

    Engine::Audio::Core::AudioSourceBuffer audio_buffer;
    if (!Engine::Audio::Core::AudioSourceLoader::Load(load_options, &audio_buffer, error_out)) return false;
    if (audio_buffer.samples.empty() || audio_buffer.sample_rate <= 0) {
        SetError(error_out, "music bass EQ clip normalized audio buffer is empty");
        return false;
    }
    normalized.duration_seconds = std::max(
        0.25,
        static_cast<double>(audio_buffer.frame_count == 0u ? audio_buffer.samples.size() : audio_buffer.frame_count)
            / static_cast<double>(audio_buffer.sample_rate));

    result_out->output_path = normalized.output_path;
    result_out->width = normalized.width;
    result_out->height = normalized.height;
    result_out->fps = normalized.fps;
    result_out->codec_name = normalized.codec_name;
    result_out->opengl_enabled = ClipOpenGlEnabled();
    result_out->opencl_enabled = ClipOpenClEnabled();
    result_out->vulkan_enabled = ClipVulkanEnabled();

    if (!AnalyzeBassEqTimeline(audio_buffer.samples, audio_buffer.sample_rate, normalized, result_out, error_out)) return false;

    const size_t frame_count = result_out->eq_points.size();
    std::mutex result_mutex;

    const std::filesystem::path frame_cache_root = normalized.output_dir / "cache" / "frames";
    ClipFrameCache frame_cache(frame_cache_root, BuildFrameCacheKey(options, normalized, audio_buffer));
    result_out->frame_cache_enabled = true;
    result_out->frame_cache_root = frame_cache.root();

    std::atomic_bool cache_failed{false};
    std::mutex cache_error_mutex;
    std::string cache_error;

    auto cache_range = [&](size_t begin, size_t end) {
        for (size_t index = begin; index < end && !cache_failed.load(); ++index) {
            auto frame = RenderMusicFrame(normalized, result_out->eq_points[index], index, frame_count);
            if (!frame) {
                std::lock_guard<std::mutex> lock(cache_error_mutex);
                cache_error = "failed to render music bass EQ video frame for cache";
                cache_failed.store(true);
                return;
            }
            const ClipGpuPostprocessResult gpu_result = ApplyClipGpuPostprocess(*frame, ClipGpuPostprocessOptions{
                result_out->eq_points[index].bass,
                result_out->eq_points[index].high,
                result_out->eq_points[index].rms,
            });
            {
                std::lock_guard<std::mutex> lock(result_mutex);
                result_out->opengl_enabled = result_out->opengl_enabled || gpu_result.opengl_enabled;
                result_out->opencl_enabled = result_out->opencl_enabled || gpu_result.opencl_enabled;
                result_out->opencl_postprocess_used = result_out->opencl_postprocess_used || gpu_result.opencl_used;
                result_out->vulkan_enabled = result_out->vulkan_enabled || gpu_result.vulkan_enabled;
                result_out->vulkan_frame_accel_used = result_out->vulkan_frame_accel_used || gpu_result.vulkan_used;
            }
            std::string write_error;
            if (!frame_cache.WriteFrame(index, *frame, &write_error)) {
                std::lock_guard<std::mutex> lock(cache_error_mutex);
                cache_error = write_error.empty() ? "failed to write optimized frame cache" : write_error;
                cache_failed.store(true);
                return;
            }
        }
    };

    const size_t cache_worker_count = CpuWorkerCount(frame_count);
    std::vector<std::future<void>> cache_futures;
    cache_futures.reserve(cache_worker_count > 0u ? cache_worker_count - 1u : 0u);
    for (size_t worker = 1u; worker < cache_worker_count; ++worker) {
        const size_t begin = frame_count * worker / cache_worker_count;
        const size_t end = frame_count * (worker + 1u) / cache_worker_count;
        cache_futures.push_back(std::async(std::launch::async, cache_range, begin, end));
    }
    cache_range(0u, frame_count / cache_worker_count);
    for (auto& future : cache_futures) {
        future.get();
    }
    if (cache_failed.load()) {
        SetError(error_out, cache_error.empty() ? "failed to render optimized frame cache" : cache_error);
        return false;
    }
    result_out->frame_cache_frames = frame_count;

    FrameExporter exporter;

    auto frame_provider = [&](size_t index, std::shared_ptr<Frame>* out_frame, std::string* provider_error) {
        if (out_frame == nullptr || index >= frame_count) {
            SetError(provider_error, "invalid streaming music frame request");
            return false;
        }
        return frame_cache.ReadFrame(index, out_frame, provider_error);
    };

    auto export_stream = [&](const std::string& codec_name) {
        std::filesystem::path temp_output = Engine::Cache::TempPathForTarget(normalized.output_path, codec_name);
        temp_output += normalized.output_path.extension().string();
        std::error_code remove_error;
        std::filesystem::remove(temp_output, remove_error);
        if (!exporter.ExportFrameStream(
                temp_output.string(),
                frame_count,
                normalized.width,
                normalized.height,
                codec_name,
                normalized.fps,
                frame_provider)) {
            std::filesystem::remove(temp_output, remove_error);
            return false;
        }
        std::string commit_error;
        if (!Engine::Cache::CommitTempFile(temp_output, normalized.output_path, &commit_error)) {
            SetError(error_out, commit_error.empty() ? "failed to commit streamed music clip output" : commit_error);
            return false;
        }
        return true;
    };

    if (!export_stream(normalized.codec_name)) {
        if (normalized.codec_name == "mpeg4" || !export_stream("mpeg4")) {
            if (error_out != nullptr && error_out->empty()) {
                SetError(error_out, "failed to encode streamed music bass EQ clip video frames");
            }
            return false;
        }
        result_out->codec_name = "mpeg4";
    }

    if (options.write_analysis_artifacts) {
        result_out->report_path = normalized.output_dir / "music_bass_eq_clip_report.txt";
        result_out->eq_csv_path = normalized.output_dir / "music_bass_eq_analysis.csv";
        if (!WriteEqCsv(result_out->eq_csv_path, result_out->eq_points, error_out)
            || !WriteTextFile(result_out->report_path, BuildMusicBassEqClipReport(options, *result_out), error_out)) {
            return false;
        }
    }

    return true;
}

}  // namespace video