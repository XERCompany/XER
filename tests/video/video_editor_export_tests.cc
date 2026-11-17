// Export suite — Phase-1C ExportEngine coverage. Validation cases run
// without FFmpeg; encode cases synthesize their media fixtures through
// the bridge (no binary assets) and self-skip via IsAvailable(), same
// pattern as the decoder/poster suites.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "export_engine.h"
#include "helper/path_utils.h"
#include "timeline_model.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;
namespace FFmpeg = XER::Bridge::FFmpeg;
using XER::VideoEditor::Clip;
using XER::VideoEditor::ExportEngine;
using XER::VideoEditor::ExportOptions;
using XER::VideoEditor::ExportProgress;
using XER::VideoEditor::ExportStatus;
using XER::VideoEditor::Project;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;

constexpr int kClipW = 64;
constexpr int kClipH = 64;
constexpr int kClipFps = 24;
constexpr int kClipFrames = 24;          // 1.0 s
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioSamples = kAudioRate;  // 1.0 s
constexpr double kPi = 3.14159265358979323846;

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

std::filesystem::path ExportTempDir() {
    const auto dir = XER::VideoEditor::Test::TempDir() / "export";
    std::filesystem::create_directories(dir);
    return dir;
}

void RemoveTempArtifacts(const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::remove(target, ec);
    const auto parent = target.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, ec)) {
        return;
    }
    const std::string prefix = target.filename().string() + ".tmp.";
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        if (entry.path().filename().string().find(prefix) == 0) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

bool NoTempArtifacts(const std::filesystem::path& target) {
    const auto parent = target.parent_path();
    std::error_code ec;
    if (parent.empty() || !std::filesystem::is_directory(parent, ec)) {
        return true;
    }
    const std::string prefix = target.filename().string() + ".tmp.";
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        if (entry.path().filename().string().find(prefix) == 0) {
            return false;
        }
    }
    return true;
}

// 64x64 BGRA frame with a moving vertical bar — enough motion for the
// encoder to emit real frames instead of dropping them as static.
void FillClipFrame(FFmpeg::VideoFrameInfo* frame, std::size_t index) {
    frame->width = kClipW;
    frame->height = kClipH;
    frame->pixel_format = "bgra";
    frame->line_sizes = {kClipW * 4, 0, 0, 0};
    frame->data.assign(static_cast<std::size_t>(kClipW) * kClipH * 4u,
                       0);
    const int bar_x = static_cast<int>(index * 4) % kClipW;
    for (int y = 0; y < kClipH; ++y) {
        for (int x = bar_x; x < bar_x + 8 && x < kClipW; ++x) {
            const std::size_t o =
                (static_cast<std::size_t>(y) * kClipW + x) * 4u;
            frame->data[o + 0] = 30;
            frame->data[o + 1] = 60;
            frame->data[o + 2] = 220;
            frame->data[o + 3] = 255;
        }
    }
}

bool EncodeVideoWithFallback(FFmpeg::EncodeVideoParams* params,
                             std::size_t frame_count,
                             std::string* used_codec,
                             std::string* err) {
    for (const char* codec : {"libx264", "mpeg4"}) {
        params->codec_name = codec;
        err->clear();
        if (FFmpeg::EncodeVideoFrameStream(
                *params, frame_count,
                [](std::size_t index, FFmpeg::VideoFrameInfo* out,
                   std::string*) {
                    FillClipFrame(out, index);
                    return true;
                },
                err)) {
            if (used_codec != nullptr) {
                *used_codec = codec;
            }
            return true;
        }
    }
    return false;
}

void FillSine(FFmpeg::AudioFrameInfo* frame, std::size_t first_sample,
              int sample_count) {
    frame->sample_rate = kAudioRate;
    frame->channels = kAudioChannels;
    frame->sample_count = sample_count;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->data.assign(
        static_cast<std::size_t>(sample_count) * kAudioChannels * 2u,
        0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / kAudioRate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * kPi * 440.0 * t));
        pcm[i * kAudioChannels + 0] = v;
        pcm[i * kAudioChannels + 1] = v;
    }
}

std::vector<FFmpeg::AudioFrameInfo> MakeSineChunks(int total_samples) {
    std::vector<FFmpeg::AudioFrameInfo> chunks;
    for (int done = 0; done < total_samples;) {
        const int n = std::min(1024, total_samples - done);
        FFmpeg::AudioFrameInfo frame;
        FillSine(&frame, static_cast<std::size_t>(done), n);
        chunks.push_back(std::move(frame));
        done += n;
    }
    return chunks;
}

bool MakeVideoOnlyClip(const std::string& utf8_path,
                       std::string* used_codec, std::string* err) {
    FFmpeg::EncodeVideoParams params;
    params.output_path = utf8_path;
    params.width = kClipW;
    params.height = kClipH;
    params.pixel_format = "bgra";
    params.fps_num = kClipFps;
    params.fps_den = 1;
    params.bit_rate = 200000;
    return EncodeVideoWithFallback(&params, kClipFrames, used_codec,
                                   err);
}

bool MakeWavFile(const std::string& utf8_path, int total_samples,
                 std::string* err) {
    FFmpeg::EncodeAudioParams params;
    params.output_path = utf8_path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kAudioRate;
    params.channels = kAudioChannels;
    params.sample_format = "s16";
    return FFmpeg::EncodeAudioFrames(
        params, MakeSineChunks(total_samples), err);
}

bool MakeAvClip(const std::string& utf8_path,
                std::string* used_codec, std::string* err) {
    FFmpeg::EncodeAudioVideoParams params;
    params.video.output_path = utf8_path;
    params.video.width = kClipW;
    params.video.height = kClipH;
    params.video.pixel_format = "bgra";
    params.video.fps_num = kClipFps;
    params.video.fps_den = 1;
    params.video.bit_rate = 200000;
    params.audio.codec_name = "aac";
    params.audio.sample_rate = kAudioRate;
    params.audio.channels = kAudioChannels;
    params.audio.sample_format = "s16";
    params.audio.bit_rate = 128000;

    const auto chunks = MakeSineChunks(kAudioSamples);
    auto video_provider = [](std::size_t index,
                             FFmpeg::VideoFrameInfo* out,
                             std::string*) {
        FillClipFrame(out, index);
        return true;
    };
    auto audio_provider = [&chunks](std::size_t index,
                                    FFmpeg::AudioFrameInfo* out,
                                    std::string*) {
        if (index >= chunks.size()) {
            return false;
        }
        *out = chunks[index];
        return true;
    };

    for (const char* codec : {"libx264", "mpeg4"}) {
        params.video.codec_name = codec;
        err->clear();
        if (FFmpeg::EncodeAudioVideoStream(
                params, kClipFrames, video_provider, chunks.size(),
                audio_provider, err)) {
            if (used_codec != nullptr) {
                *used_codec = codec;
            }
            return true;
        }
    }
    return false;
}

Project MakeVideoProject(const std::string& source) {
    Project project;
    project.width = kClipW;
    project.height = kClipH;
    project.fps_num = kClipFps;
    project.fps_den = 1;
    project.bg_color = {0, 0, 0, 255};
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    Clip clip;
    clip.id = "clip-video";
    clip.source_path = source;
    clip.track = 0;
    clip.timeline_start = 0.0;
    clip.duration = 1.0;
    project.clips.push_back(clip);
    return project;
}

Project MakeAudioProject(const std::string& source) {
    Project project = MakeVideoProject(source);
    project.tracks.clear();
    project.tracks.push_back(Track{TrackKind::Audio, "A1"});
    project.clips.front().id = "clip-audio";
    project.clips.front().track = 0;
    return project;
}

Project MakeAvProject(const std::string& source) {
    Project project = MakeVideoProject(source);
    project.clips.front().id = "clip-av";
    return project;
}

int CountStreams(const FFmpeg::MediaInfo& info,
                 const std::string& media_type) {
    int count = 0;
    for (const auto& stream : info.streams) {
        if (stream.media_type == media_type) {
            ++count;
        }
    }
    return count;
}

ExportOptions MakeOptions(const std::filesystem::path& output,
                          const std::string& video_codec) {
    ExportOptions options;
    options.output_path = Utf8Path(output);
    options.video_codec = video_codec;
    options.audio_codec = "aac";
    options.video_bit_rate = 200000;
    options.audio_bit_rate = 96000;
    return options;
}

bool ProbeOutput(const std::filesystem::path& output,
                 FFmpeg::MediaInfo* info) {
    std::string err;
    return FFmpeg::ProbeMedia(Utf8Path(output), info, &err);
}

}  // namespace

int XER_VideoEditor_RunExportTests() {
    // -- validation (no FFmpeg needed) -------------------------------
    {
        ExportEngine engine;
        Project empty;
        ExportOptions options;
        options.output_path = "out.mp4";
        std::string err;
        XER_VE_CHECK(!engine.Start(empty, options, {}, &err));
        XER_VE_CHECK(!err.empty());
    }
    {
        ExportEngine engine;
        Project project = MakeVideoProject("missing-source.mp4");
        ExportOptions options;
        std::string err;
        XER_VE_CHECK(!engine.Start(project, options, {}, &err));
        XER_VE_CHECK(!err.empty());
    }

    if (!FFmpeg::IsAvailable()) {
        return 0;
    }

    const auto dir = ExportTempDir();
    std::string err;

    const auto video_path = dir / "clip_video.mp4";
    const auto av_path = dir / "clip_av.mp4";
    const auto wav_path = dir / "tone.wav";
    std::string video_codec;
    if (!MakeVideoOnlyClip(Utf8Path(video_path), &video_codec, &err)) {
        std::fprintf(stderr,
                     "note: export video fixture encode failed: %s\n",
                     err.c_str());
        return 0;
    }
    if (video_codec.empty()) {
        video_codec = "libx264";
    }
    if (!MakeWavFile(Utf8Path(wav_path), kAudioSamples, &err)) {
        std::fprintf(stderr,
                     "note: export wav fixture encode failed: %s\n",
                     err.c_str());
        return 0;
    }
    std::string av_codec;
    if (!MakeAvClip(Utf8Path(av_path), &av_codec, &err)) {
        std::fprintf(stderr,
                     "note: export A/V fixture encode failed: %s\n",
                     err.c_str());
        return 0;
    }

    // -- video-only project -> one video stream, no audio ------------
    {
        const auto output = dir / "export_video_only.mp4";
        RemoveTempArtifacts(output);
        ExportEngine engine;
        std::vector<double> fractions;
        const bool started = engine.Start(
            MakeVideoProject(Utf8Path(video_path)),
            MakeOptions(output, video_codec),
            [&fractions](const ExportProgress& progress) {
                fractions.push_back(progress.fraction);
            },
            &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
            XER_VE_CHECK(std::filesystem::exists(output));
            XER_VE_CHECK(NoTempArtifacts(output));
            XER_VE_CHECK(!fractions.empty());
            XER_VE_CHECK(
                std::is_sorted(fractions.begin(), fractions.end()));
            XER_VE_CHECK(fractions.back() == 1.0);

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            XER_VE_CHECK(CountStreams(info, "video") == 1);
            XER_VE_CHECK(CountStreams(info, "audio") == 0);
        }
    }

    // -- audio-only project -> one audio stream, no video ------------
    {
        const auto output = dir / "export_audio_only.mp4";
        RemoveTempArtifacts(output);
        ExportEngine engine;
        const bool started = engine.Start(
            MakeAudioProject(Utf8Path(wav_path)),
            MakeOptions(output, video_codec), {}, &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
            XER_VE_CHECK(std::filesystem::exists(output));
            XER_VE_CHECK(NoTempArtifacts(output));

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            XER_VE_CHECK(CountStreams(info, "video") == 0);
            XER_VE_CHECK(CountStreams(info, "audio") == 1);
            const auto audio_stream = std::find_if(
                info.streams.begin(), info.streams.end(),
                [](const FFmpeg::StreamInfo& stream) {
                    return stream.media_type == "audio";
                });
            if (audio_stream != info.streams.end()) {
                std::vector<FFmpeg::AudioFrameInfo> frames;
                XER_VE_CHECK(FFmpeg::DecodeAudioFrames(
                    Utf8Path(output), audio_stream->index, 2, &frames,
                    &err));
                XER_VE_CHECK(!frames.empty());
            }
        }
    }

    // -- A/V project -> one video + one audio stream -----------------
    {
        const auto output = dir / "export_av.mp4";
        RemoveTempArtifacts(output);
        ExportEngine engine;
        const bool started = engine.Start(
            MakeAvProject(Utf8Path(av_path)),
            MakeOptions(output, av_codec), {}, &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
            XER_VE_CHECK(std::filesystem::exists(output));
            XER_VE_CHECK(NoTempArtifacts(output));

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            XER_VE_CHECK(CountStreams(info, "video") == 1);
            XER_VE_CHECK(CountStreams(info, "audio") == 1);
            int video_index = -1;
            int audio_index = -1;
            for (const auto& stream : info.streams) {
                if (stream.media_type == "video") video_index = stream.index;
                if (stream.media_type == "audio") audio_index = stream.index;
            }
            if (video_index >= 0) {
                std::vector<FFmpeg::VideoFrameInfo> frames;
                XER_VE_CHECK(FFmpeg::DecodeVideoFrames(
                    Utf8Path(output), video_index, 2, &frames, &err));
                XER_VE_CHECK(!frames.empty());
            }
            if (audio_index >= 0) {
                std::vector<FFmpeg::AudioFrameInfo> frames;
                XER_VE_CHECK(FFmpeg::DecodeAudioFrames(
                    Utf8Path(output), audio_index, 2, &frames, &err));
                XER_VE_CHECK(!frames.empty());
            }
        }
    }

    // -- missing source fails before a target/temp is produced -------
    {
        const auto output = dir / "export_missing.mp4";
        RemoveTempArtifacts(output);
        ExportEngine engine;
        Project project = MakeVideoProject(
            Utf8Path(dir / "does_not_exist.mp4"));
        const bool started = engine.Start(
            project, MakeOptions(output, video_codec), {}, &err);
        XER_VE_CHECK(!started);
        XER_VE_CHECK(!err.empty());
        XER_VE_CHECK(!std::filesystem::exists(output));
        XER_VE_CHECK(NoTempArtifacts(output));
    }

    // -- cancel mid-export removes target + temp ----------------------
    {
        const auto output = dir / "export_cancel.mp4";
        RemoveTempArtifacts(output);
        ExportEngine engine;
        bool cancel_sent = false;
        const bool started = engine.Start(
            MakeAvProject(Utf8Path(av_path)),
            MakeOptions(output, av_codec),
            [&engine, &cancel_sent](const ExportProgress& progress) {
                const bool did_work =
                    progress.video_frames_done > 0 ||
                    progress.audio_chunks_done > 0;
                if (did_work && !cancel_sent) {
                    cancel_sent = true;
                    engine.Cancel();
                }
            },
            &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(cancel_sent);
            XER_VE_CHECK(engine.Status() == ExportStatus::kCancelled);
            XER_VE_CHECK(!std::filesystem::exists(output));
            XER_VE_CHECK(NoTempArtifacts(output));
        }
    }

    // -- work-area export renders only [work_in, work_out) ------------
    {
        const auto output = dir / "export_work_area.mp4";
        RemoveTempArtifacts(output);
        Project project = MakeVideoProject(Utf8Path(video_path));
        project.work_in = 0.25;
        project.work_out = 0.75;   // 0.5 s of 24 fps = 12 frames
        ExportEngine engine;
        const bool started = engine.Start(
            project, MakeOptions(output, video_codec), {}, &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
            XER_VE_CHECK(std::filesystem::exists(output));
            XER_VE_CHECK(NoTempArtifacts(output));
            // Progress totals reflect the range, not the project.
            XER_VE_CHECK(engine.Progress().video_frames_total == 12);

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            XER_VE_CHECK(CountStreams(info, "video") == 1);
            int video_index = -1;
            for (const auto& stream : info.streams) {
                if (stream.media_type == "video") {
                    video_index = stream.index;
                }
            }
            if (video_index >= 0) {
                std::vector<FFmpeg::VideoFrameInfo> frames;
                XER_VE_CHECK(FFmpeg::DecodeVideoFrames(
                    Utf8Path(output), video_index, 64, &frames, &err));
                XER_VE_CHECK(frames.size() == 12);
            }
        }
    }

    // -- explicit options range overrides the project work area ------
    {
        const auto output = dir / "export_range.mp4";
        RemoveTempArtifacts(output);
        Project project = MakeVideoProject(Utf8Path(video_path));
        project.work_in = 0.0;
        project.work_out = 1.0;      // ignored — options range wins
        ExportOptions options = MakeOptions(output, video_codec);
        options.range_in = 0.5;
        options.range_out = 1.0;     // 0.5 s -> 12 frames
        ExportEngine engine;
        const bool started =
            engine.Start(project, options, {}, &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
            XER_VE_CHECK(std::filesystem::exists(output));
            XER_VE_CHECK(engine.Progress().video_frames_total == 12);

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            int video_index = -1;
            for (const auto& stream : info.streams) {
                if (stream.media_type == "video") {
                    video_index = stream.index;
                }
            }
            if (video_index >= 0) {
                std::vector<FFmpeg::VideoFrameInfo> frames;
                XER_VE_CHECK(FFmpeg::DecodeVideoFrames(
                    Utf8Path(output), video_index, 64, &frames, &err));
                XER_VE_CHECK(frames.size() == 12);
            }
        }
    }

    // -- uncreatable output path -> kError, no partial target ---------
    {
        const auto blocker = dir / "not_a_directory";
        {
            std::ofstream file(blocker, std::ios::binary);
            file << "blocker";
        }
        const auto output = blocker / "export.mp4";
        ExportEngine engine;
        const bool started = engine.Start(
            MakeVideoProject(Utf8Path(video_path)),
            MakeOptions(output, video_codec), {}, &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kError);
            XER_VE_CHECK(!engine.Error().empty());
            XER_VE_CHECK(!std::filesystem::exists(output));
        }
    }

    return XER::VideoEditor::Test::Failures();
}
