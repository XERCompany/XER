#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace video {

struct MusicBassEqClipOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::filesystem::path output_dir;
    double duration_seconds = 15.0;
    int width = 1280;
    int height = 720;
    int fps = 30;
    int target_sample_rate = 44100;
    std::string codec_name = "libx264";
    bool write_analysis_artifacts = true;
};

struct MusicBassEqFramePoint {
    double time_seconds = 0.0;
    float bass = 0.0f;
    float low_mid = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float rms = 0.0f;
    float dominant_hz = 0.0f;
};

struct MusicBassEqClipResult {
    std::filesystem::path output_path;
    std::filesystem::path report_path;
    std::filesystem::path eq_csv_path;
    double duration_seconds = 0.0;
    int width = 0;
    int height = 0;
    int fps = 0;
    int sample_rate = 0;
    std::size_t audio_frames_used = 0;
    std::size_t video_frames_written = 0;
    std::string codec_name;
    float bass_peak = 0.0f;
    float bass_average = 0.0f;
    float low_mid_peak = 0.0f;
    float mid_peak = 0.0f;
    float high_peak = 0.0f;
    float rms_peak = 0.0f;
    bool opengl_enabled = false;
    bool opencl_enabled = false;
    bool opencl_postprocess_used = false;
    bool vulkan_enabled = false;
    bool vulkan_frame_accel_used = false;
    bool frame_cache_enabled = false;
    std::size_t frame_cache_frames = 0;
    std::filesystem::path frame_cache_root;
    std::vector<MusicBassEqFramePoint> eq_points;
};

std::string BuildMusicBassEqClipReport(const MusicBassEqClipOptions& options, const MusicBassEqClipResult& result);

bool GenerateMusicBassEqClip(
    const MusicBassEqClipOptions& options,
    MusicBassEqClipResult* result_out,
    std::string* error_out = nullptr);

}  // namespace video