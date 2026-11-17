// Object Lab suite — tools/video_editor/lab (mkl_accel, object_ops,
// async_op). Synthetic BGRA8 frames only; no decoders, no files.
// NOTE: intentionally NOT registered in tests/CMakeLists.txt yet —
// wire it into XER_VideoEditor_Tests (source list + test_main.cc) when
// the parent integrates the lab.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "video_editor_test_util.h"
#include "lab/async_op.h"
#include "lab/mkl_accel.h"
#include "lab/object_ops.h"
#include "video/frame.h"

namespace {

using XER::VideoEditor::Lab::AccelBackend;
using XER::VideoEditor::Lab::AsyncOp;
using XER::VideoEditor::Lab::InpaintParams;
using XER::VideoEditor::Lab::IntPoint;
using XER::VideoEditor::Lab::IntRect;
using XER::VideoEditor::Lab::JobControl;
using XER::VideoEditor::Lab::PatchSsd;
using XER::VideoEditor::Lab::Sdot;

struct Bgra {
    std::uint8_t b, g, r, a;
};

void SetPx(video::Frame& f, int x, int y, Bgra c) {
    std::uint8_t* p = f.Data() +
                      (static_cast<size_t>(y) * f.Width() + x) * 4;
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
    p[3] = c.a;
}

Bgra GetPx(const video::Frame& f, int x, int y) {
    const std::uint8_t* p = f.Data() +
                            (static_cast<size_t>(y) * f.Width() + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

bool SameColor(Bgra a, Bgra b, int tol) {
    return std::abs(a.b - b.b) <= tol && std::abs(a.g - b.g) <= tol &&
           std::abs(a.r - b.r) <= tol;
}

video::Frame MakeQuadrants(int w, int h) {
    video::Frame f(w, h, video::PixelFormat::BGRA8);
    const Bgra tl{220, 20, 20, 255};   // blue (BGR order)
    const Bgra tr{20, 220, 20, 255};   // green
    const Bgra bl{20, 20, 220, 255};   // red
    const Bgra br{20, 200, 220, 255};  // yellow-ish
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Bgra c = (y < h / 2) ? (x < w / 2 ? tl : tr)
                                       : (x < w / 2 ? bl : br);
            SetPx(f, x, y, c);
        }
    }
    return f;
}

// Two-tone vertical split: left cool, right warm.
video::Frame MakeHalves(int w, int h) {
    video::Frame f(w, h, video::PixelFormat::BGRA8);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            SetPx(f, x, y, x < w / 2 ? Bgra{200, 40, 40, 255}
                                     : Bgra{40, 40, 200, 255});
        }
    }
    return f;
}

}  // namespace

int XER_VideoEditor_RunLabTests() {
    using namespace XER::VideoEditor;

    // -- 1. Acceleration backend + sdot --------------------------------
    {
        const char* name = Lab::BackendName();
        XER_VE_CHECK(name != nullptr && name[0] != '\0');
        const AccelBackend backend = Lab::ActiveBackend();
        XER_VE_CHECK(backend == AccelBackend::Scalar ||
                     backend == AccelBackend::MKL ||
                     backend == AccelBackend::OpenBLAS);

        const float a[3] = {1.0f, 2.0f, 3.0f};
        const float b[3] = {4.0f, 5.0f, 6.0f};
        XER_VE_CHECK(std::fabs(Sdot(3, a, b) - 32.0f) < 1e-4f);
        // (a-b)^2 = 9+9+9
        XER_VE_CHECK(std::fabs(PatchSsd(3, a, b) - 27.0f) < 1e-3f);
        XER_VE_CHECK(PatchSsd(0, a, b) == 0.0f);
        XER_VE_CHECK(PatchSsd(-5, a, b) == 0.0f);
        XER_VE_CHECK(Sdot(0, a, b) == 0.0f);
    }

    // -- 2. CutOutRect --------------------------------------------------
    {
        video::Frame src = MakeQuadrants(16, 16);
        video::Frame cut = Lab::CutOutRect(src, IntRect{4, 4, 8, 8});
        XER_VE_CHECK(cut.IsValid());
        XER_VE_CHECK(cut.Width() == 16 && cut.Height() == 16);
        // Inside the rect: source pixels, alpha forced 255.
        XER_VE_CHECK(SameColor(GetPx(cut, 5, 5), GetPx(src, 5, 5), 0));
        XER_VE_CHECK(SameColor(GetPx(cut, 10, 10), GetPx(src, 10, 10), 0));
        XER_VE_CHECK(GetPx(cut, 5, 5).a == 255);
        XER_VE_CHECK(GetPx(cut, 11, 11).a == 255);
        // Outside: fully transparent.
        XER_VE_CHECK(GetPx(cut, 0, 0).a == 0);
        XER_VE_CHECK(GetPx(cut, 15, 15).a == 0);
        XER_VE_CHECK(GetPx(cut, 0, 0).b == 0 && GetPx(cut, 0, 0).g == 0 &&
                     GetPx(cut, 0, 0).r == 0);
        XER_VE_CHECK(GetPx(cut, 3, 8).a == 0);   // just left of rect
        XER_VE_CHECK(GetPx(cut, 12, 8).a == 0);  // just right
        // Degenerate / out-of-bounds rects.
        XER_VE_CHECK(!Lab::CutOutRect(src, IntRect{0, 0, 0, 4}).IsValid());
        XER_VE_CHECK(
            !Lab::CutOutRect(src, IntRect{100, 100, 4, 4}).IsValid());
        // Partially out of bounds: only the in-bounds part is opaque.
        video::Frame clipped = Lab::CutOutRect(src, IntRect{12, 12, 8, 8});
        XER_VE_CHECK(clipped.IsValid());
        XER_VE_CHECK(GetPx(clipped, 13, 13).a == 255);
        XER_VE_CHECK(GetPx(clipped, 11, 13).a == 0);
        // Wrong pixel format rejected.
        video::Frame nv12(16, 16, video::PixelFormat::NV12);
        XER_VE_CHECK(!Lab::CutOutRect(nv12, IntRect{0, 0, 4, 4}).IsValid());
    }

    // -- 3. CutOutLasso --------------------------------------------------
    {
        video::Frame src = MakeQuadrants(16, 16);
        const std::vector<IntPoint> diamond = {
            {8, 2}, {14, 8}, {8, 14}, {2, 8}};

        video::Frame hard = Lab::CutOutLasso(src, diamond, 0);
        XER_VE_CHECK(hard.IsValid());
        XER_VE_CHECK(GetPx(hard, 8, 8).a == 255);      // center inside
        XER_VE_CHECK(GetPx(hard, 8, 3).a == 255);      // near top vertex
        XER_VE_CHECK(GetPx(hard, 0, 0).a == 0);        // corner outside
        XER_VE_CHECK(GetPx(hard, 15, 15).a == 0);
        XER_VE_CHECK(SameColor(GetPx(hard, 8, 8), GetPx(src, 8, 8), 0));
        // Hard edge: alpha is binary over the whole frame.
        bool has_partial = false;
        for (size_t i = 3; i < hard.DataSize(); i += 4) {
            if (hard.Data()[i] != 0 && hard.Data()[i] != 255) {
                has_partial = true;
            }
        }
        XER_VE_CHECK(!has_partial);

        // Feathered edge: some boundary pixel sits between 0 and 255,
        // while deep inside stays 255 and far outside stays 0.
        video::Frame soft = Lab::CutOutLasso(src, diamond, 6);
        XER_VE_CHECK(soft.IsValid());
        has_partial = false;
        for (size_t i = 3; i < soft.DataSize(); i += 4) {
            if (soft.Data()[i] != 0 && soft.Data()[i] != 255) {
                has_partial = true;
            }
        }
        XER_VE_CHECK(has_partial);
        XER_VE_CHECK(GetPx(soft, 8, 8).a == 255);
        XER_VE_CHECK(GetPx(soft, 0, 0).a == 0);
        XER_VE_CHECK(GetPx(soft, 15, 0).a == 0);
        // Degenerate inputs.
        XER_VE_CHECK(!Lab::CutOutLasso(src, {{1, 1}, {2, 2}}, 2).IsValid());
        XER_VE_CHECK(!Lab::CutOutLasso(src, {}, 2).IsValid());
    }

    // -- 4. CopyRegionToLayer -------------------------------------------
    {
        video::Frame src = MakeQuadrants(16, 16);
        std::vector<std::uint8_t> mask(16 * 16, 0);
        mask[5 * 16 + 5] = 255;    // one opaque px inside
        mask[6 * 16 + 5] = 128;    // one half-alpha px
        video::Frame layer = Lab::CopyRegionToLayer(src, mask);
        XER_VE_CHECK(layer.IsValid());
        XER_VE_CHECK(GetPx(layer, 5, 5).a == 255);
        XER_VE_CHECK(SameColor(GetPx(layer, 5, 5), GetPx(src, 5, 5), 0));
        XER_VE_CHECK(GetPx(layer, 5, 6).a == 128);
        XER_VE_CHECK(SameColor(GetPx(layer, 5, 6), GetPx(src, 5, 6), 0));
        XER_VE_CHECK(GetPx(layer, 7, 7).a == 0);
        XER_VE_CHECK(GetPx(layer, 7, 7).b == 0 &&
                     GetPx(layer, 7, 7).g == 0 &&
                     GetPx(layer, 7, 7).r == 0);
        // Wrong-size mask rejected.
        XER_VE_CHECK(
            !Lab::CopyRegionToLayer(src, {255, 255}).IsValid());
    }

    // -- 5. InpaintRect --------------------------------------------------
    {
        video::Frame src = MakeHalves(48, 32);
        const Bgra marker{220, 0, 220, 255};  // magenta "object"
        const IntRect hole{19, 11, 10, 10};   // straddles the split
        for (int y = hole.y; y < hole.y + hole.h; ++y) {
            for (int x = hole.x; x < hole.x + hole.w; ++x) {
                SetPx(src, x, y, marker);
            }
        }
        video::Frame before = src;
        std::vector<std::uint8_t> before_px(src.DataSize());
        std::memcpy(before_px.data(), src.Data(), src.DataSize());

        std::vector<float> progress_log;
        InpaintParams params;
        params.search_radius = 12;
        params.iterations = 4;
        params.report_progress = [&progress_log](float f) {
            progress_log.push_back(f);
        };
        XER_VE_CHECK(Lab::InpaintRect(src, hole, params));

        // Every hole pixel: opaque, and not the marker colour.
        for (int y = hole.y; y < hole.y + hole.h; ++y) {
            for (int x = hole.x; x < hole.x + hole.w; ++x) {
                const Bgra p = GetPx(src, x, y);
                XER_VE_CHECK(p.a == 255);
                XER_VE_CHECK(!SameColor(p, marker, 4));
            }
        }
        // Outside the hole nothing moved.
        for (int y = 0; y < src.Height(); ++y) {
            for (int x = 0; x < src.Width(); ++x) {
                if (x >= hole.x && x < hole.x + hole.w && y >= hole.y &&
                    y < hole.y + hole.h) {
                    continue;
                }
                const size_t i =
                    (static_cast<size_t>(y) * src.Width() + x) * 4;
                XER_VE_CHECK(src.Data()[i + 0] == before_px[i + 0] &&
                             src.Data()[i + 1] == before_px[i + 1] &&
                             src.Data()[i + 2] == before_px[i + 2] &&
                             src.Data()[i + 3] == before_px[i + 3]);
            }
        }
        // Plausible neighborhood stats: mean colour inside the hole is
        // close to the mean of the ring around it (2-px band).
        double ring[3] = {0, 0, 0};
        int ring_n = 0;
        const IntRect ring_rect{hole.x - 2, hole.y - 2, hole.w + 4,
                                hole.h + 4};
        for (int y = ring_rect.y; y < ring_rect.y + ring_rect.h; ++y) {
            for (int x = ring_rect.x; x < ring_rect.x + ring_rect.w;
                 ++x) {
                if (x < 0 || y < 0 || x >= before.Width() ||
                    y >= before.Height()) {
                    continue;
                }
                const bool in_hole = x >= hole.x && x < hole.x + hole.w &&
                                     y >= hole.y && y < hole.y + hole.h;
                if (in_hole) continue;
                const Bgra p = GetPx(before, x, y);
                ring[0] += p.b;
                ring[1] += p.g;
                ring[2] += p.r;
                ++ring_n;
            }
        }
        double fill[3] = {0, 0, 0};
        int fill_n = 0;
        for (int y = hole.y; y < hole.y + hole.h; ++y) {
            for (int x = hole.x; x < hole.x + hole.w; ++x) {
                const Bgra p = GetPx(src, x, y);
                fill[0] += p.b;
                fill[1] += p.g;
                fill[2] += p.r;
                ++fill_n;
            }
        }
        for (int c = 0; c < 3; ++c) {
            const double ring_mean = ring[c] / ring_n;
            const double fill_mean = fill[c] / fill_n;
            XER_VE_CHECK(std::fabs(fill_mean - ring_mean) <= 64.0);
        }
        // Progress reported monotonic-ish within [0,1], ends at 1.0.
        XER_VE_CHECK(!progress_log.empty());
        XER_VE_CHECK(progress_log.back() == 1.0f);
        for (float f : progress_log) {
            XER_VE_CHECK(f >= 0.0f && f <= 1.0f);
        }

        // Cancel path: returns false, frame untouched.
        video::Frame cancel_frame = MakeHalves(48, 32);
        for (int y = hole.y; y < hole.y + hole.h; ++y) {
            for (int x = hole.x; x < hole.x + hole.w; ++x) {
                SetPx(cancel_frame, x, y, marker);
            }
        }
        std::vector<std::uint8_t> snap(cancel_frame.DataSize());
        std::memcpy(snap.data(), cancel_frame.Data(),
                    cancel_frame.DataSize());
        InpaintParams cancelling;
        cancelling.search_radius = 12;
        cancelling.should_cancel = [] { return true; };
        XER_VE_CHECK(!Lab::InpaintRect(cancel_frame, hole, cancelling));
        XER_VE_CHECK(std::memcmp(snap.data(), cancel_frame.Data(),
                                 snap.size()) == 0);

        // InpaintMask path with a lasso-shaped hole.
        video::Frame masked = MakeHalves(48, 32);
        const std::vector<IntPoint> blob = {
            {30, 8}, {38, 12}, {36, 22}, {28, 20}};
        const std::vector<std::uint8_t> lasso_hole =
            Lab::PolygonMask(48, 32, blob, 0);
        XER_VE_CHECK(!lasso_hole.empty());
        XER_VE_CHECK(Lab::InpaintMask(masked, lasso_hole, 12));
        XER_VE_CHECK(GetPx(masked, 33, 15).a == 255);
        // Bad inputs.
        video::Frame empty;
        XER_VE_CHECK(!Lab::InpaintRect(empty, hole, 8));
        XER_VE_CHECK(
            !Lab::InpaintMask(masked, {1, 2, 3}, 8));
        video::Frame whole = MakeHalves(16, 16);
        XER_VE_CHECK(!Lab::InpaintMask(
            whole, std::vector<std::uint8_t>(16 * 16, 255), 8));
    }

    // -- 6. AsyncOp ------------------------------------------------------
    {
        AsyncOp op;
        XER_VE_CHECK(!op.IsRunning());
        XER_VE_CHECK(std::fabs(op.Progress()) < 1e-6f);
        XER_VE_CHECK(!op.HasResult());

        std::atomic<int> work_ticks{0};
        XER_VE_CHECK(op.Start([&work_ticks](const JobControl& ctl) {
            for (int i = 0; i < 10; ++i) {
                if (ctl.ShouldCancel()) return false;
                ctl.ReportProgress(static_cast<float>(i + 1) / 10.0f);
                ++work_ticks;
            }
            return true;
        }));
        // Cannot double-start while running or with a pending result.
        XER_VE_CHECK(!op.Start([](const JobControl&) { return true; }));

        bool ok = false, cancelled = false;
        std::string error;
        for (int i = 0; i < 200 && !op.PopResult(&ok, &cancelled, &error);
             ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        XER_VE_CHECK(ok);
        XER_VE_CHECK(!cancelled);
        XER_VE_CHECK(error.empty());
        XER_VE_CHECK(work_ticks.load() == 10);
        XER_VE_CHECK(!op.IsRunning());

        // Cancel path.
        std::atomic<bool> saw_cancel{false};
        XER_VE_CHECK(op.Start([&saw_cancel](const JobControl& ctl) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(5);
            while (!ctl.ShouldCancel()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return true;  // would mean cancel never arrived
                }
                std::this_thread::yield();
            }
            saw_cancel = true;
            return false;
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        op.RequestCancel();
        ok = true;
        cancelled = false;
        error.clear();
        bool got_result = false;
        for (int i = 0; i < 500 && !got_result; ++i) {
            got_result = op.PopResult(&ok, &cancelled, &error);
            if (!got_result) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
        }
        XER_VE_CHECK(got_result);
        XER_VE_CHECK(cancelled);
        XER_VE_CHECK(!ok);
        XER_VE_CHECK(saw_cancel.load());

        // Work reporting an error message surfaces it via PopResult.
        XER_VE_CHECK(op.Start([](const JobControl& ctl) {
            ctl.Fail("boom");
            return false;
        }));
        ok = true;
        error.clear();
        for (int i = 0; i < 200 && !op.PopResult(&ok, &cancelled, &error);
             ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        XER_VE_CHECK(!ok);
        XER_VE_CHECK(error == "boom");

        // Progress clamping.
        XER_VE_CHECK(op.Start([](const JobControl& ctl) {
            ctl.ReportProgress(4.0f);
            return true;
        }));
        for (int i = 0; i < 200 && !op.PopResult(&ok, &cancelled, &error);
             ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        XER_VE_CHECK(op.Progress() <= 1.0f);
    }

    return 0;
}
