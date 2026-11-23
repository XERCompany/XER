// ui2_media_prep smoke — see ui2_media_prep_smoke.h. Real catalog +
// real MediaPrep worker pool over a real FFmpeg-encoded A/V fixture;
// only the window/renderer is absent.
#include "ui2_media_prep_smoke.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

#include "frame.h"   // src/video — video::Frame for the poster assert
#include "helper/path_utils.h"
#include "media_catalog.h"
#include "media_prep.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace FFmpeg = XER::Bridge::FFmpeg;
namespace Path = XER::Helper::Path;

// Mirrors smoke_checks.cc's scratch area (distinct file name so a
// full-suite run never shares rows with the catalog/proxy checks).
std::filesystem::path SmokeTempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     "xer_ui2_media_prep_smoke";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

// 320x180 libx264 + 48 kHz stereo AAC, ~1.07 s: video frames carry a
// moving grey ramp (poster must be non-uniform), audio is a mid-gain
// sine (peaks must be non-silent).
bool EncodeAvFixture(const std::filesystem::path& out,
                     std::string* error_out) {
    constexpr int kW = 320, kH = 180;
    constexpr int kFps = 15;
    constexpr std::size_t kVideoFrames = 16;   // 1.0667 s
    constexpr int kRate = 48000, kChannels = 2;
    constexpr int kChunkSamples = 1024;
    constexpr std::size_t kAudioChunks = 50;   // 51200 samples ~1.067 s

    FFmpeg::EncodeAudioVideoParams params;
    params.video.output_path = Utf8Path(out);
    params.video.codec_name = "libx264";
    params.video.width = kW;
    params.video.height = kH;
    params.video.pixel_format = "bgra";
    params.video.fps_num = kFps;
    params.audio.codec_name = "aac";
    params.audio.sample_rate = kRate;
    params.audio.channels = kChannels;
    params.audio.sample_format = "s16";

    const auto video_provider = [](std::size_t index,
                                   FFmpeg::VideoFrameInfo* f,
                                   std::string*) -> bool {
        f->width = kW;
        f->height = kH;
        f->pixel_format = "bgra";
        f->pts = f->best_effort_timestamp =
            static_cast<std::int64_t>(index);
        f->line_sizes = {kW * 4, 0, 0, 0};
        f->data.assign(static_cast<std::size_t>(kW) * kH * 4u, 0);
        // Per-frame ramp: each row's grey grows with x and the frame
        // index so any decoded poster is provably non-uniform.
        for (int y = 0; y < kH; ++y) {
            auto* row = f->data.data() + y * kW * 4;
            for (int x = 0; x < kW; ++x) {
                const std::uint8_t v = static_cast<std::uint8_t>(
                    (x + static_cast<int>(index) * 13) & 0xFF);
                row[x * 4 + 0] = v;          // B
                row[x * 4 + 1] = (v + 40);   // G
                row[x * 4 + 2] = (v + 80);   // R
                row[x * 4 + 3] = 0xFF;       // A
            }
        }
        return true;
    };

    const auto audio_provider = [](std::size_t index,
                                   FFmpeg::AudioFrameInfo* f,
                                   std::string*) -> bool {
        f->sample_rate = kRate;
        f->channels = kChannels;
        f->sample_count = kChunkSamples;
        f->planar = false;
        f->sample_format = "s16";
        f->pts = f->best_effort_timestamp =
            static_cast<std::int64_t>(index) * kChunkSamples;
        f->data.assign(static_cast<std::size_t>(kChunkSamples) *
                           kChannels * 2u,
                       0);
        auto* pcm = reinterpret_cast<std::int16_t*>(f->data.data());
        for (int i = 0; i < kChunkSamples; ++i) {
            const double t =
                static_cast<double>(index * kChunkSamples + i) / kRate;
            const auto s = static_cast<std::int16_t>(
                std::lrint(std::sin(2.0 * 3.14159265358979323846 *
                                    440.0 * t) *
                           16000.0));
            pcm[i * kChannels + 0] = s;
            pcm[i * kChannels + 1] = s;
        }
        return true;
    };

    return FFmpeg::EncodeAudioVideoStream(params, kVideoFrames,
                                          video_provider, kAudioChunks,
                                          audio_provider, error_out);
}

bool ValidBgra(const video::Frame& f) {
    return f.IsValid() && f.Format() == video::PixelFormat::BGRA8 &&
           f.Width() > 0 && f.Height() > 0 &&
           f.DataSize() >= static_cast<std::size_t>(f.Width()) *
                               static_cast<std::size_t>(f.Height()) *
                               4u;
}

// Decode -> populate round trip for one Lookup* call: the first ask
// may legitimately answer kPending (enqueue) or kReady (worker beat
// us); after WaitIdle the SAME ask must answer kReady.
std::string CheckPosterRoundTrip(MediaPrep& prep,
                                 const std::string& path) {
    video::Frame frame;
    const MediaPrep::Lookup first =
        prep.LookupPoster(path, 0.5, &frame);
    if (first == MediaPrep::Lookup::kFailed) {
        return "poster lookup answered kFailed on a valid fixture";
    }
    if (!prep.WaitIdle(60000)) {
        return "prep pool did not go idle after poster request";
    }
    if (prep.LookupPoster(path, 0.5, &frame) !=
        MediaPrep::Lookup::kReady) {
        return "poster lookup not kReady after idle";
    }
    if (!ValidBgra(frame)) {
        return "decoded poster is not a valid BGRA frame";
    }
    if (frame.Width() > 320 && frame.Height() > 320) {
        return "poster bypassed the 320px thumbnail cap";
    }
    return {};
}

std::string CheckPeaksRoundTrip(MediaPrep& prep,
                                const std::string& path) {
    AudioPeaks peaks;
    const MediaPrep::Lookup first = prep.LookupPeaks(path, &peaks);
    if (first == MediaPrep::Lookup::kFailed) {
        return "peaks lookup answered kFailed on a valid fixture";
    }
    if (!prep.WaitIdle(60000)) {
        return "prep pool did not go idle after peaks request";
    }
    if (prep.LookupPeaks(path, &peaks) != MediaPrep::Lookup::kReady) {
        return "peaks lookup not kReady after idle";
    }
    if (peaks.channels <= 0 || peaks.buckets == 0 ||
        peaks.data.size() <
            peaks.buckets * 2u *
                static_cast<std::size_t>(peaks.channels)) {
        return "decoded peaks violate the AudioPeaks layout contract";
    }
    // The fixture's sine must produce real amplitude somewhere.
    bool non_silent = false;
    for (const float v : peaks.data) {
        if (std::fabs(v) > 0.05f) {
            non_silent = true;
            break;
        }
    }
    if (!non_silent) {
        return "decoded peaks are silent for a sine fixture";
    }
    return {};
}

}  // namespace

std::string Ui2MediaPrepSmoke() {
    if (!FFmpeg::IsAvailable()) {
        return "ffmpeg bridge unavailable";
    }
    const auto dir = SmokeTempDir();
    const auto fixture = dir / "prep_fixture.mp4";
    const auto probe_only = dir / "prep_probe_only.mp4";
    const std::string db_path = Utf8Path(dir / "prep_catalog.db");
    {
        std::error_code ec;
        std::filesystem::remove(fixture, ec);
        std::filesystem::remove(probe_only, ec);
        std::filesystem::remove(Path::FromUtf8(db_path), ec);
    }
    std::string err;
    if (!EncodeAvFixture(fixture, &err)) {
        return "fixture encode failed: " + err;
    }
    if (!EncodeAvFixture(probe_only, &err)) {
        return "second fixture encode failed: " + err;
    }

    MediaCatalog catalog(db_path);
    if (!catalog.Open(&err) || !catalog.IsOpen()) {
        return "catalog open failed: " + err;
    }
    const std::string fixture_utf8 = Utf8Path(fixture);
    const std::string probe_utf8 = Utf8Path(probe_only);

    // ---- catalog probe state transition: none -> ready --------------
    std::int64_t id = -1;
    if (!catalog.Import(fixture_utf8, &id, false, &err) || id <= 0) {
        catalog.Close();
        return "catalog Import failed: " + err;
    }
    CatalogEntry entry;
    if (!catalog.FindByPath(fixture_utf8, &entry, &err) ||
        !entry.HasVideo() || !entry.HasAudio() ||
        entry.duration_seconds <= 0.0 || entry.width <= 0 ||
        entry.height <= 0) {
        catalog.Close();
        return "imported row is missing probed metadata";
    }

    {
        MediaPrep prep(&catalog);

        // ---- real decodes through the pool ---------------------------
        if (const std::string e = CheckPosterRoundTrip(prep, fixture_utf8);
            !e.empty()) {
            catalog.Close();
            return e;
        }
        if (const std::string e = CheckPeaksRoundTrip(prep, fixture_utf8);
            !e.empty()) {
            catalog.Close();
            return e;
        }
        // The drain-to-UI contract: results landed -> exactly one true.
        if (!prep.ConsumeRenderDirty()) {
            catalog.Close();
            return "render-dirty flag never landed";
        }
        if (prep.ConsumeRenderDirty()) {
            catalog.Close();
            return "render-dirty flag did not drain one-shot";
        }

        // ---- bin poster write (ExtractPoster -> poster_path) ---------
        prep.RequestImportPoster(fixture_utf8);
        if (!prep.WaitIdle(60000)) {
            catalog.Close();
            return "prep pool did not go idle after poster request";
        }
        if (!prep.ConsumeCatalogDirty()) {
            catalog.Close();
            return "catalog-dirty flag never landed for poster write";
        }
        if (!catalog.FindByPath(fixture_utf8, &entry) ||
            entry.poster_path.empty()) {
            catalog.Close();
            return "poster_path column never landed";
        }
        {
            std::error_code ec;
            if (!std::filesystem::exists(
                    Path::FromUtf8(entry.poster_path), ec)) {
                catalog.Close();
                return "poster_path points at a missing file";
            }
        }

        // ---- probe job: path with no row materializes one ------------
        prep.RequestProbe(probe_utf8);
        if (!prep.WaitIdle(60000)) {
            catalog.Close();
            return "prep pool did not go idle after probe request";
        }
        CatalogEntry probed;
        if (!catalog.FindByPath(probe_utf8, &probed) ||
            !probed.HasVideo() || probed.duration_seconds <= 0.0) {
            catalog.Close();
            return "probe job did not produce a catalog row";
        }
        if (!prep.ConsumeCatalogDirty()) {
            catalog.Close();
            return "probe job did not flag the catalog dirty";
        }

        // ---- negative cache: dead media answers kFailed ---------------
        AudioPeaks dead;
        const MediaPrep::Lookup first =
            prep.LookupPeaks(Utf8Path(dir / "does_not_exist.mp4"),
                             &dead);
        if (first == MediaPrep::Lookup::kFailed) {
            // An instant failure is legal (fast-path miss) — but then
            // the negative record must already be in place.
        } else if (!prep.WaitIdle(60000)) {
            catalog.Close();
            return "prep pool did not go idle on dead-media request";
        }
        if (prep.LookupPeaks(Utf8Path(dir / "does_not_exist.mp4"),
                             &dead) != MediaPrep::Lookup::kFailed) {
            catalog.Close();
            return "dead media did not land in the negative cache";
        }
    }

    // ---- TTL: an expired failure re-enqueues exactly once -------------
    {
        MediaPrep::Options options;
        options.failed_ttl_seconds = 0.05;
        MediaPrep prep(&catalog, options, MediaPrep::JobRunner{});
        const std::string missing =
            Utf8Path(dir / "ttl_missing.mp4");
        AudioPeaks dead;
        (void)prep.LookupPeaks(missing, &dead);
        if (!prep.WaitIdle(60000)) {
            catalog.Close();
            return "TTL pool did not go idle";
        }
        if (prep.LookupPeaks(missing, &dead) !=
            MediaPrep::Lookup::kFailed) {
            catalog.Close();
            return "fresh failure did not answer kFailed";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        if (prep.LookupPeaks(missing, &dead) !=
            MediaPrep::Lookup::kPending) {
            catalog.Close();
            return "expired failure did not re-enqueue";
        }
        if (!prep.WaitIdle(60000)) {
            catalog.Close();
            return "TTL pool did not go idle after re-enqueue";
        }
        if (prep.LookupPeaks(missing, &dead) !=
            MediaPrep::Lookup::kFailed) {
            catalog.Close();
            return "re-failed request did not re-enter the cache";
        }
    }

    catalog.Close();
    return {};
}

}  // namespace XER::VideoEditor::Ui2
