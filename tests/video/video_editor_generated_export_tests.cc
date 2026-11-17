// Generated-source export suite — procedural clips (color matte,
// bars, text) must reach a real MP4 through ExportEngine without any
// decoder session (ExportEngine::Start skips probing for
// IsGeneratedSource clips; the compositor synthesizes their frames).
// Mirrors the export suite: the one real-media fixture is synthesized
// through the FFmpeg bridge, and every encode case self-skips via
// FFmpeg::IsAvailable().
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
using XER::VideoEditor::SourceKind;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;

constexpr int kClipW = 64;
constexpr int kClipH = 64;
constexpr int kClipFps = 24;
constexpr int kClipFrames = 24;            // 1.0 s of fixture media
constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioSamples = kAudioRate;  // 1.0 s
constexpr double kPi = 3.14159265358979323846;

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

std::filesystem::path ExportTempDir() {
    const auto dir =
        XER::VideoEditor::Test::TempDir() / "generated_export";
    std::filesystem::create_directories(dir);
    return dir;
}

void RemoveOutput(const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::remove(target, ec);
}

// -- real-media fixture (same moving-bar clip the export suite
//    synthesizes, trimmed to the helpers this suite needs) ----------

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

// 1 s 64x64 A/V clip — video + AAC tone so the mixed project exports
// one stream of each type.
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

// -- project fixtures -------------------------------------------------

Clip MakeGeneratedClip(const std::string& id, int track, double start,
                       double duration, SourceKind kind,
                       const std::string& param) {
    Clip c;
    c.id = id;
    c.track = track;
    c.timeline_start = start;
    c.duration = duration;
    c.source.kind = kind;   // source_path stays empty — no media file
    c.source.param = param;
    return c;
}

// One 3 s video track: matte -> bars -> title, all procedural.
Project MakeGeneratedProject() {
    Project project;
    project.width = kClipW;
    project.height = kClipH;
    project.fps_num = kClipFps;
    project.fps_den = 1;
    project.bg_color = {0, 0, 0, 255};
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.clips.push_back(MakeGeneratedClip(
        "matte", 0, 0.0, 1.0, SourceKind::ColorMatte, "#2244CC"));
    project.clips.push_back(MakeGeneratedClip(
        "bars", 0, 1.0, 1.0, SourceKind::ColorBars, "smpte75"));
    project.clips.push_back(MakeGeneratedClip(
        "title", 0, 2.0, 1.0, SourceKind::Text, "XER"));
    return project;
}

// Real A/V clip on V1 with a generated matte layered over it on V2.
Project MakeMixedProject(const std::string& av_source) {
    Project project;
    project.width = kClipW;
    project.height = kClipH;
    project.fps_num = kClipFps;
    project.fps_den = 1;
    project.bg_color = {0, 0, 0, 255};
    project.tracks.push_back(Track{TrackKind::Video, "V1"});
    project.tracks.push_back(Track{TrackKind::Video, "V2"});
    Clip media;
    media.id = "media";
    media.source_path = av_source;
    media.track = 0;
    media.timeline_start = 0.0;
    media.duration = 1.0;
    project.clips.push_back(media);
    project.clips.push_back(MakeGeneratedClip(
        "matte", 1, 0.0, 1.0, SourceKind::ColorMatte, "#80104080"));
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

// Shared assertions for a successful export: engine reports done, the
// progress stream is monotonic and reaches 1.0, and the target exists
// with a non-empty body.
void CheckDoneExport(const ExportEngine& engine,
                     const std::vector<double>& fractions,
                     const std::filesystem::path& output) {
    XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
    XER_VE_CHECK(std::filesystem::exists(output));
    std::error_code ec;
    XER_VE_CHECK(std::filesystem::file_size(output, ec) > 0);
    XER_VE_CHECK(!fractions.empty());
    XER_VE_CHECK(std::is_sorted(fractions.begin(), fractions.end()));
    if (!fractions.empty()) {
        XER_VE_CHECK(fractions.back() == 1.0);
    }
}

}  // namespace

int XER_VideoEditor_RunGeneratedExportTests() {
    // -- validation runs without FFmpeg -------------------------------
    {
        ExportEngine engine;
        ExportOptions options;   // empty output path
        std::string err;
        XER_VE_CHECK(
            !engine.Start(MakeGeneratedProject(), options, {}, &err));
        XER_VE_CHECK(!err.empty());
    }

    if (!FFmpeg::IsAvailable()) {
        return XER::VideoEditor::Test::Failures();
    }

    const auto dir = ExportTempDir();
    std::string err;

    // The mixed case needs one real source — synthesize it through the
    // bridge exactly like the export suite's fixtures.
    const auto av_path = dir / "gen_av.mp4";
    std::string av_codec;
    if (!MakeAvClip(Utf8Path(av_path), &av_codec, &err)) {
        std::fprintf(stderr,
                     "note: generated-export A/V fixture encode "
                     "failed: %s\n",
                     err.c_str());
        return XER::VideoEditor::Test::Failures();
    }
    if (av_codec.empty()) {
        av_codec = "libx264";
    }

    // -- generated-only project -> MP4 with one video stream ----------
    // No clip owns a source file, so Start's probe loop sees zero
    // paths; the compositor renders matte/bars/text procedurally.
    {
        const auto output = dir / "export_generated.mp4";
        RemoveOutput(output);
        ExportEngine engine;
        std::vector<double> fractions;
        const bool started = engine.Start(
            MakeGeneratedProject(), MakeOptions(output, av_codec),
            [&fractions](const ExportProgress& progress) {
                fractions.push_back(progress.fraction);
            },
            &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            CheckDoneExport(engine, fractions, output);
            // 3 s at 24 fps -> the full project length in frames.
            XER_VE_CHECK(
                engine.Progress().video_frames_total == 3 * kClipFps);

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            XER_VE_CHECK(CountStreams(info, "video") == 1);
            // Procedural clips own no audio: video-only output.
            XER_VE_CHECK(CountStreams(info, "audio") == 0);
        }
    }

    // -- mixed project: generated matte over real A/V media ----------
    {
        const auto output = dir / "export_mixed.mp4";
        RemoveOutput(output);
        ExportEngine engine;
        const bool started = engine.Start(
            MakeMixedProject(Utf8Path(av_path)),
            MakeOptions(output, av_codec), {}, &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            XER_VE_CHECK(engine.Status() == ExportStatus::kDone);
            XER_VE_CHECK(std::filesystem::exists(output));
            std::error_code ec;
            XER_VE_CHECK(std::filesystem::file_size(output, ec) > 0);

            FFmpeg::MediaInfo info;
            XER_VE_CHECK(ProbeOutput(output, &info));
            XER_VE_CHECK(CountStreams(info, "video") == 1);
            // The real clip's tone comes through the mixer even
            // though the top layer is generated.
            XER_VE_CHECK(CountStreams(info, "audio") == 1);
        }
    }

    // -- work-area export of generated content ------------------------
    {
        const auto output = dir / "export_generated_range.mp4";
        RemoveOutput(output);
        Project project = MakeGeneratedProject();
        project.work_in = 0.5;
        project.work_out = 1.5;   // 1.0 s of the matte/bars cut
        ExportEngine engine;
        std::vector<double> fractions;
        const bool started = engine.Start(
            project, MakeOptions(output, av_codec),
            [&fractions](const ExportProgress& progress) {
                fractions.push_back(progress.fraction);
            },
            &err);
        XER_VE_CHECK(started);
        if (started) {
            XER_VE_CHECK(engine.Wait());
            CheckDoneExport(engine, fractions, output);
            XER_VE_CHECK(
                engine.Progress().video_frames_total == kClipFps);
        }
    }

    return XER::VideoEditor::Test::Failures();
}
