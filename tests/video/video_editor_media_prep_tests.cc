// MediaPrep suite — the media-prep worker pool behind the timeline's
// non-blocking poster/peaks hooks: dedup of (kind, path, bucket),
// queue cancel/purge, failure caching, the lookup tri-state and the
// dirty-flag drains the app Tick() consumes. The JobRunner seam
// fabricates results deterministically (blocking gate + call counts)
// so the queue semantics need no FFmpeg; a final end-to-end case
// (gated on the bridge) runs a real ClipPoster decode + catalog
// ExtractPoster through a temp catalog DB.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "video_editor_test_util.h"
#include "frame.h"
#include "helper/path_utils.h"
#include "media_catalog.h"
#include "media_prep.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;
using XER::VideoEditor::AudioPeaks;
using XER::VideoEditor::CatalogEntry;
using XER::VideoEditor::MediaCatalog;
using XER::VideoEditor::MediaPrep;

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

void FillFakeFrame(video::Frame* frame, std::uint8_t seed) {
    frame->Allocate(8, 8, video::PixelFormat::BGRA8);
    for (std::size_t i = 0; i < frame->DataSize(); ++i) {
        frame->Data()[i] = static_cast<std::uint8_t>(seed + i);
    }
}

void FillFakePeaks(AudioPeaks* peaks) {
    peaks->channels = 2;
    peaks->samples_per_peak = 256;
    peaks->buckets = 4;
    peaks->data.assign(2 * 4 * 2, 0.0f);
    // Channel 0 mins/max ramp — a recognizable payload.
    for (std::size_t b = 0; b < peaks->buckets; ++b) {
        peaks->data[b * 2] = -0.5f;
        peaks->data[b * 2 + 1] = 0.5f;
    }
}

// Per-path call counter + a path-gated blocker for the JobRunner
// seam — gives the tests deterministic control over worker progress.
struct RunnerState {
    std::mutex mutex;
    std::unordered_map<std::string, int> calls;
    std::string gated_path;                 // spins until released
    std::atomic<bool> release{false};
    std::atomic<bool> succeed{true};

    int CallsFor(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = calls.find(path);
        return it == calls.end() ? 0 : it->second;
    }
};

MediaPrep::JobRunner MakeRunner(RunnerState* state) {
    return [state](const MediaPrep::Key& key, MediaPrep::JobOut* out) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            ++state->calls[key.path];
        }
        // Spin (bounded — tests always release) while this path's job
        // occupies the worker, so queued requests can be observed.
        if (!state->gated_path.empty() && key.path == state->gated_path) {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(10);
            while (!state->release.load() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
        }
        if (!state->succeed.load()) return false;
        switch (key.kind) {
            case MediaPrep::Kind::kPosterAt:
                FillFakeFrame(out->frame, 0x11);
                break;
            case MediaPrep::Kind::kPeaks:
                FillFakePeaks(out->peaks);
                break;
            case MediaPrep::Kind::kImportPoster:
                break;   // no payload — success flag only
        }
        return true;
    };
}

}  // namespace

int XER_VideoEditor_RunMediaPrepTests() {
    using Lookup = MediaPrep::Lookup;
    const auto dir = XER::VideoEditor::Test::TempDir() / "media_prep";
    std::filesystem::create_directories(dir);
    std::string err;

    MediaCatalog catalog_store(Utf8Path(dir / "catalog.db"));
    if (!catalog_store.Open(&err)) {
        std::fprintf(stderr,
                     "note: media-prep temp catalog open failed: %s\n",
                     err.c_str());
        return 0;
    }
    MediaCatalog* catalog = &catalog_store;

    // -- 1. cold poster lookup -> kPending; warm -> kReady -----------
    {
        RunnerState state;
        MediaPrep prep(catalog, {.threads = 2}, MakeRunner(&state));
        video::Frame frame;
        XER_VE_CHECK(prep.LookupPoster("a.mp4", 1.0, &frame) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.PendingCount() >= 1);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(prep.LookupPoster("a.mp4", 1.0, &frame) ==
                     Lookup::kReady);
        XER_VE_CHECK(frame.IsValid());
        XER_VE_CHECK(frame.Width() == 8 && frame.Height() == 8);
        // Same 0.25 s bucket (1.0 and 1.01) -> one decode total.
        video::Frame again;
        XER_VE_CHECK(prep.LookupPoster("a.mp4", 1.01, &again) ==
                     Lookup::kReady);
        XER_VE_CHECK(state.CallsFor("a.mp4") == 1);
        // A different bucket is a distinct request.
        state.release.store(true);
        XER_VE_CHECK(prep.LookupPoster("a.mp4", 2.5, &again) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(state.CallsFor("a.mp4") == 2);
    }

    // -- 2. dedup: a queued/running key never double-decodes ---------
    {
        RunnerState state;
        state.gated_path = "gate.mp4";
        MediaPrep::Options opt;
        opt.threads = 1;
        MediaPrep prep(catalog, opt, MakeRunner(&state));
        video::Frame frame;
        // First request occupies the single worker (spinning); the
        // next five lookups hit the queued/running dedup set.
        XER_VE_CHECK(prep.LookupPoster("gate.mp4", 0.0, &frame) ==
                     Lookup::kPending);
        for (int i = 0; i < 5; ++i) {
            XER_VE_CHECK(prep.LookupPoster("gate.mp4", 0.0, &frame) ==
                         Lookup::kPending);
        }
        XER_VE_CHECK(prep.PendingCount() == 1);
        state.release.store(true);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(state.CallsFor("gate.mp4") == 1);
        XER_VE_CHECK(prep.LookupPoster("gate.mp4", 0.0, &frame) ==
                     Lookup::kReady);
    }

    // -- 3. failure -> kFailed, no silent re-decode ------------------
    {
        RunnerState state;
        state.succeed.store(false);
        MediaPrep prep(catalog, {.threads = 1}, MakeRunner(&state));
        video::Frame frame;
        XER_VE_CHECK(prep.LookupPoster("bad.mp4", 0.0, &frame) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(prep.PendingCount() == 0);
        XER_VE_CHECK(prep.LookupPoster("bad.mp4", 0.0, &frame) ==
                     Lookup::kFailed);
        XER_VE_CHECK(state.CallsFor("bad.mp4") == 1);   // no retry
        // PurgePath clears the failure record — the next lookup
        // re-enqueues (self-healing after e.g. a file rewrite).
        prep.PurgePath("bad.mp4");
        XER_VE_CHECK(prep.LookupPoster("bad.mp4", 0.0, &frame) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(state.CallsFor("bad.mp4") == 2);
    }

    // -- 4. CancelQueued drops waiting requests for a path -----------
    {
        RunnerState state;
        state.gated_path = "blocker.mp4";
        MediaPrep::Options opt;
        opt.threads = 1;
        MediaPrep prep(catalog, opt, MakeRunner(&state));
        video::Frame frame;
        prep.LookupPoster("blocker.mp4", 0.0, &frame);   // holds worker
        prep.LookupPoster("doomed.mp4", 0.0, &frame);
        prep.LookupPoster("doomed.mp4", 1.0, &frame);
        XER_VE_CHECK(prep.PendingCount() == 3);
        prep.CancelQueued("doomed.mp4");
        XER_VE_CHECK(prep.PendingCount() == 1);          // blocker runs
        state.release.store(true);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(state.CallsFor("doomed.mp4") == 0);
        // Cancelled media re-enqueues cleanly on the next lookup.
        XER_VE_CHECK(prep.LookupPoster("doomed.mp4", 0.0, &frame) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(state.CallsFor("doomed.mp4") == 1);
    }

    // -- 5. peaks lookup round-trips the AudioPeaks payload ----------
    {
        RunnerState state;
        MediaPrep prep(catalog, {.threads = 1}, MakeRunner(&state));
        AudioPeaks peaks;
        XER_VE_CHECK(prep.LookupPeaks("song.wav", &peaks) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(prep.LookupPeaks("song.wav", &peaks) ==
                     Lookup::kReady);
        XER_VE_CHECK(peaks.channels == 2);
        XER_VE_CHECK(peaks.buckets == 4);
        XER_VE_CHECK(peaks.data.size() == 16);
        XER_VE_CHECK(state.CallsFor("song.wav") == 1);
    }

    // -- 6. dirty flags: one-shot drains -----------------------------
    {
        RunnerState state;
        MediaPrep prep(catalog, {.threads = 1}, MakeRunner(&state));
        video::Frame frame;
        XER_VE_CHECK(!prep.ConsumeRenderDirty());
        XER_VE_CHECK(!prep.ConsumeCatalogDirty());
        prep.LookupPoster("dirty.mp4", 0.0, &frame);
        prep.RequestImportPoster("dirty.mp4");
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(prep.ConsumeRenderDirty());     // poster landed
        XER_VE_CHECK(!prep.ConsumeRenderDirty());    // drained
        XER_VE_CHECK(prep.ConsumeCatalogDirty());    // poster_path row
        XER_VE_CHECK(!prep.ConsumeCatalogDirty());
    }

    // -- 7. PurgePath during an in-flight decode drops the stale
    //       result (per-path generation guard — bug-audit fix) ------
    {
        RunnerState state;
        state.gated_path = "purged.wav";
        MediaPrep prep(catalog, {.threads = 1}, MakeRunner(&state));
        AudioPeaks peaks;
        XER_VE_CHECK(prep.LookupPeaks("purged.wav", &peaks) ==
                     Lookup::kPending);
        // Wait until the worker is inside RunJob, then purge the path
        // — the decode finishes AFTER its caches were thrown away.
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        while (state.CallsFor("purged.wav") == 0 &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        XER_VE_CHECK(state.CallsFor("purged.wav") == 1);
        prep.PurgePath("purged.wav");
        state.release.store(true);
        XER_VE_CHECK(prep.WaitIdle(10000));
        // The stale peaks must NOT have republished — and no failure
        // was recorded either, so the next lookup re-enqueues a fresh
        // decode (kPending, not kReady/kFailed).
        XER_VE_CHECK(prep.LookupPeaks("purged.wav", &peaks) ==
                     Lookup::kPending);
        XER_VE_CHECK(prep.WaitIdle(10000));
        XER_VE_CHECK(prep.LookupPeaks("purged.wav", &peaks) ==
                     Lookup::kReady);
        XER_VE_CHECK(peaks.channels == 2 && peaks.buckets == 4);
        XER_VE_CHECK(state.CallsFor("purged.wav") == 2);
    }

    // -- 8. end-to-end with the real decode path ---------------------
    if (XER::Bridge::FFmpeg::IsAvailable()) {
        const auto clip = dir / "real.mp4";
        XER::Bridge::FFmpeg::EncodeVideoParams params;
        params.output_path = Utf8Path(clip);
        params.width = 64;
        params.height = 48;
        params.pixel_format = "bgra";
        params.fps_num = 15;
        params.fps_den = 1;
        params.bit_rate = 200000;
        bool encoded = false;
        for (const char* codec : {"libx264", "mpeg4"}) {
            params.codec_name = codec;
            err.clear();
            if (XER::Bridge::FFmpeg::EncodeVideoFrameStream(
                    params, 30,
                    [](std::size_t index,
                       XER::Bridge::FFmpeg::VideoFrameInfo* out,
                       std::string*) {
                        out->width = 64;
                        out->height = 48;
                        out->pixel_format = "bgra";
                        out->line_sizes = {64 * 4, 0, 0, 0};
                        out->data.assign(64 * 48 * 4,
                                         static_cast<std::uint8_t>(
                                             40 + index));
                        return true;
                    },
                    &err)) {
                encoded = true;
                break;
            }
        }
        if (encoded) {
            MediaPrep prep(catalog);   // default runner — real decode
            const std::string utf8 = Utf8Path(clip);
            // Catalog import (no poster) -> ExtractPoster via worker.
            std::int64_t id = 0;
            XER_VE_CHECK(catalog->Import(utf8, &id, false, &err));
            prep.RequestImportPoster(utf8);
            video::Frame frame;
            XER_VE_CHECK(prep.LookupPoster(utf8, 0.5, &frame) ==
                         Lookup::kPending);
            XER_VE_CHECK(prep.WaitIdle(30000));
            XER_VE_CHECK(prep.LookupPoster(utf8, 0.5, &frame) ==
                         Lookup::kReady);
            XER_VE_CHECK(frame.IsValid() && frame.Width() == 64 &&
                         frame.Height() == 48);
            XER_VE_CHECK(prep.ConsumeRenderDirty());
            XER_VE_CHECK(prep.ConsumeCatalogDirty());
            CatalogEntry entry;
            XER_VE_CHECK(catalog->FindByPath(utf8, &entry, &err));
            XER_VE_CHECK(!entry.poster_path.empty());
        } else {
            std::fprintf(stderr,
                         "note: media-prep e2e fixture encode failed:"
                         " %s\n", err.c_str());
        }
    }

    // -- 9. queue-full: dropped import-poster requests are counted ---
    {
        RunnerState state;
        state.gated_path = "gate.mp4";
        MediaPrep::Options opt;
        opt.threads = 1;
        opt.max_pending = 1;
        MediaPrep prep(catalog, opt, MakeRunner(&state));
        video::Frame frame;
        // Occupy the single worker with a gated decode so the 1-deep
        // queue actually fills.
        XER_VE_CHECK(prep.LookupPoster("gate.mp4", 0.0, &frame) ==
                     Lookup::kPending);
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        while (state.CallsFor("gate.mp4") == 0 &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        XER_VE_CHECK(state.CallsFor("gate.mp4") == 1);
        prep.RequestImportPoster("p1.mp4");   // queued (queue depth 1)
        prep.RequestImportPoster("p2.mp4");   // dropped — queue full
        prep.RequestImportPoster("p3.mp4");   // dropped
        XER_VE_CHECK(prep.ConsumeDroppedPosterRequests() == 2);
        XER_VE_CHECK(prep.ConsumeDroppedPosterRequests() == 0);  // drained
        state.release.store(true);
        XER_VE_CHECK(prep.WaitIdle(10000));
    }

    return 0;
}
