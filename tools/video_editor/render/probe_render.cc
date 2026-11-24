// render/probe_render.cc — body of `video_editor --probe-render`
// (hidden dev flag): headless self-check of the chromium render-port
// slice-1 primitives. Exercises VideoFramePool recycling, the
// owned/texture frame factories, AudioClock/WallClock time sources and
// FrameScheduler deadline/drop math. No UI, no GPU, no media files —
// the presenter class itself is verified by compile + the slice-2
// integration work.
#include <cmath>
#include <cstdio>
#include <vector>

#include "frame_scheduler.h"
#include "video_frame.h"

namespace XER::VideoEditor::Render {
namespace {

bool Nearly(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

}  // namespace

int RunRenderProbe() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) {
            ++failures;
        }
    };

    std::printf("=== video_editor --probe-render "
                "(chromium-port slice 1) ===\n");

    // ---- VideoFramePool: storage recycles through release ----
    {
        VideoFramePool pool;
        const std::uint8_t* first = nullptr;
        {
            auto f = pool.Acquire(VideoPixelFormat::kBGRA8,
                                  VideoSize{320, 240}, VideoRect{},
                                  /*ts*/ 1000, /*dur*/ 33333);
            check(f && f->IsMappable() && f->planes() == 1 &&
                      f->stride(0) == 320 * 4,
                  "pool acquire: BGRA8 layout");
            check(f && f->visible_rect() ==
                           VideoRect{0, 0, 320, 240} &&
                      f->timestamp_us() == 1000,
                  "pool acquire: metadata");
            first = f->data(0);
            f->data(0)[0] = 0xAB;
        }  // last ref dies -> storage returns to the pool
        auto g = pool.Acquire(VideoPixelFormat::kBGRA8,
                              VideoSize{320, 240}, VideoRect{}, 2000,
                              33333);
        check(g && g->data(0) == first && g->data(0)[0] == 0xAB,
              "pool reuse: same storage, zero-copy");
        const auto st = pool.stats();
        // idle_buffers is 0 here: the only storage went back to the
        // pool on release and was immediately re-acquired by `g`.
        check(st.pool_hits == 1 && st.pool_misses == 1 &&
                  st.returned == 1 && st.idle_buffers == 0,
              "pool stats: hit/miss/returned accounting");
        // A different size is a different key — no false sharing.
        auto h = pool.Acquire(VideoPixelFormat::kBGRA8,
                              VideoSize{64, 64}, VideoRect{}, 0, 0);
        check(h && h->data(0) != first, "pool keying: size+format");
    }

    // ---- WrapOwnedMemory: adopts the caller's buffer ----
    {
        std::vector<std::uint8_t> px(64 * 64 * 4, 0x11);
        const std::uint8_t* raw = px.data();
        auto f = VideoFrame::WrapOwnedMemory(
            VideoPixelFormat::kBGRA8, VideoSize{64, 64}, VideoRect{},
            500, 0, std::move(px));
        check(f && f->data(0) == raw && f->data(0)[0] == 0x11,
              "WrapOwnedMemory: adopts buffer without copy");
    }

    // ---- WrapTexture: opaque handle + mailbox release ----
    {
        bool released = false;
        {
            auto f = VideoFrame::WrapTexture(
                VideoPixelFormat::kNV12, VideoSize{1920, 1080},
                VideoRect{}, 0, 0,
                VideoTexture{reinterpret_cast<void*>(0xDEAD),
                             /*subresource*/ 3, /*generation*/ 7},
                [&released] { released = true; });
            check(f && !f->IsMappable() &&
                      f->storage_type() ==
                          VideoFrameStorage::kOpaqueTexture &&
                      f->texture().subresource == 3 &&
                      f->texture().generation == 7,
                  "WrapTexture: opaque handle metadata");
        }
        check(released, "WrapTexture: mailbox release on last ref");
    }

    // ---- AudioClockTimeSource: media time from samples ----
    {
        AudioClockTimeSource clock(48000);
        clock.Start(0.0);
        clock.WroteAudioSamples(4800);
        check(Nearly(clock.MediaTimeSeconds(), 0.1),
              "audio clock: front_timestamp = written/rate");
        clock.SetBaseTime(2.5);  // seek rebase zeroes the accumulator
        check(Nearly(clock.MediaTimeSeconds(), 2.5),
              "audio clock: SetBaseTime rebase");
    }

    // ---- WallClockTimeSource: manual now_fn determinism ----
    {
        double now = 10.0;
        WallClockTimeSource clock([&now] { return now; });
        clock.Start(1.0);
        clock.SetPlaybackRate(2.0);
        now += 0.5;
        check(Nearly(clock.MediaTimeSeconds(), 2.0),
              "wall clock: anchor + wallDelta*rate");
        clock.Pause();
        now += 5.0;
        check(Nearly(clock.MediaTimeSeconds(), 2.0),
              "wall clock: pause freezes media time");
    }

    // ---- FrameScheduler: deadline window + drop accounting ----
    {
        AudioClockTimeSource clock(48000);
        clock.Start(0.0);
        clock.WroteAudioSamples(4800);  // media at 0.1 s
        FrameScheduler sched(&clock);
        sched.SetDisplayInterval(1.0 / 60.0);
        sched.SetMediaFrameDuration(1.0 / 30.0);

        const FrameTick t = sched.BeginFrame(/*wall_now*/ 1.0);
        check(Nearly(t.display_time_wall, 1.0 + 1.0 / 60.0, 1e-3),
              "scheduler: vsync-grid display time");
        // media_at_display = 0.1 + (1/60)*1.0; window spans one
        // display interval of playback.
        check(Nearly(t.deadline_min, 0.1 + 1.0 / 60.0, 1e-3) &&
                  Nearly(t.deadline_max, 0.1 + 2.0 / 60.0, 1e-3),
              "scheduler: deadline media window");

        // Present the due frame, then one 3 slots later — the two
        // skipped slots count as drops (rate 1 => step 1).
        sched.OnFramePresented(t.deadline_min);
        sched.OnFramePresented(t.deadline_min + 3.0 / 30.0);
        check(sched.counters().presented == 2 &&
                  sched.counters().dropped == 2,
              "scheduler: dropped-frame accounting");

        // Missed deadline: a tick whose deadline window expires in
        // media time with no present landing in it.
        clock.WroteAudioSamples(48000);  // media -> ~1.1 s
        sched.BeginFrame(1.2);           // outstanding window ~[1.117,1.133]
        clock.WroteAudioSamples(48000);  // media -> ~2.1 s: window passed
        sched.BeginFrame(1.4);           // observes the expiry
        check(sched.counters().missed_deadlines == 1,
              "scheduler: missed-deadline counting");

        check(Nearly(sched.preferred_render_interval(), 1.0 / 30.0),
              "scheduler: preferred render interval");
    }

    std::printf("probe-render: %s\n",
                failures == 0 ? "ALL CHECKS PASSED" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

}  // namespace XER::VideoEditor::Render
