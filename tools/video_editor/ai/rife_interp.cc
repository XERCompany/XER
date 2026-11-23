// Implementation of ai/rife_interp.h — the RIFE 4.25-lite session
// wrapper. Pads BGRA pairs to the model's multiple-of-64 grid with
// edge replicate (right/bottom), runs (img0, img1, timestep) ->
// frame, and crops the result back. Powers-of-two factors recurse
// at t=0.5 (every hop interpolates a REAL neighboring pair — the
// timestep the model is most accurate at); other factors take the
// timestep directly.
#include "ai/rife_interp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace Onnx = XER::AI::Onnx;

const Onnx::TensorView* FindOut(const std::vector<Onnx::TensorView>& o,
                                const char* name) {
    for (const Onnx::TensorView& t : o) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

// BGRA block -> NCHW RGB 0..1 on the aligned (padded) grid; the pad
// columns/rows replicate the last real pixel of each row/the last
// real row — edge-replicate keeps the flow net's border math
// honest (zero pads read as a dark border object).
void BgraToPaddedNchw(const std::uint8_t* bgra, int w, int h,
                      int stride, int pw, int ph, float* out) {
    float* r = out;
    float* g = r + static_cast<std::size_t>(pw) * ph;
    float* b = g + static_cast<std::size_t>(pw) * ph;
    for (int y = 0; y < ph; ++y) {
        const int sy = std::min(y, h - 1);
        const std::uint8_t* row =
            bgra + static_cast<std::size_t>(sy) * stride;
        for (int x = 0; x < pw; ++x) {
            const int sx = std::min(x, w - 1);
            const std::uint8_t* px = row + sx * 4;
            const std::size_t i =
                static_cast<std::size_t>(y) * pw + x;
            r[i] = px[2] / 255.0f;
            g[i] = px[1] / 255.0f;
            b[i] = px[0] / 255.0f;
        }
    }
}

}  // namespace

std::string RifeModel() { return Lab::FindAiModel(kRifeFile); }

const char* RifeModelHint() {
    return "model not installed — put rife_v425lite.onnx in "
           "assets/models/ next to the exe, or point "
           "XER_AI_MODELS_DIR at its folder";
}

bool RifeBackendOk(const std::string& backend_id) {
    // cv::dnn GridSample stays unverified this wave — the product
    // path admits ORT only. The smoke's moving-square check is the
    // experiment that can widen this later.
    return backend_id == kRifeRequiredBackend;
}

bool RifeAvailable() {
    if (RifeModel().empty()) return false;
    for (const Onnx::BackendInfo& b : Onnx::Backends()) {
        if (RifeBackendOk(b.id) && b.loadable) return true;
    }
    return false;
}

int RifePadDim(int dim) {
    if (dim <= 0) return 0;
    return ((dim + kRifeAlign - 1) / kRifeAlign) * kRifeAlign;
}

std::vector<float> RifeInterpPlan(int factor) {
    std::vector<float> plan;
    if (factor < 2) return plan;
    plan.reserve(static_cast<std::size_t>(factor) - 1);
    for (int k = 1; k < factor; ++k) {
        plan.push_back(static_cast<float>(k) /
                       static_cast<float>(factor));
    }
    return plan;
}

RifeInterp::RifeInterp() = default;
RifeInterp::~RifeInterp() = default;

bool RifeInterp::Load(const std::string& model_path,
                      const std::string& backend,
                      std::string* error_out) {
    session_.reset();
    timestep_dims_ = {1, 1, 1, 1};
    if (model_path.empty()) {
        if (error_out) *error_out = RifeModelHint();
        return false;
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, backend, &err)) {
        if (error_out) {
            *error_out = "rife load failed: " +
                         (err.empty()
                              ? std::string("rejected by every ONNX "
                                            "backend")
                              : err);
        }
        return false;
    }
    // Contract audit: img0/img1/timestep in, frame out. The
    // timestep's declared rank rides like silero's `sr` ([1,1,1,1]
    // verified; a re-export's scalar/singleton shape is honored so
    // ORT's strict shape check never sees a mismatch).
    const auto& names = session->InputNames();
    bool have_img0 = false, have_img1 = false, have_ts = false;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == "img0") have_img0 = true;
        if (names[i] == "img1") have_img1 = true;
        if (names[i] == "timestep") {
            have_ts = true;
            const std::vector<std::int64_t> d = session->InputDims(i);
            std::size_t count = 1;
            bool ok = !d.empty();
            for (const std::int64_t dim : d) {
                if (dim <= 0 || dim > 4) {
                    ok = false;
                    break;
                }
                count *= static_cast<std::size_t>(dim);
            }
            if (ok && count == 1) timestep_dims_ = d;
        }
    }
    if (!have_img0 || !have_img1 || !have_ts) {
        if (error_out) {
            *error_out =
                "rife contract mismatch (expected img0/img1/"
                "timestep inputs)";
        }
        return false;
    }
    session_ = std::move(session);
    return true;
}

bool RifeInterp::IsLoaded() const {
    return session_ != nullptr && session_->IsLoaded();
}

const std::string& RifeInterp::BackendId() const {
    static const std::string kEmpty;
    return session_ != nullptr ? session_->BackendId() : kEmpty;
}

bool RifeInterp::Interpolate(const std::uint8_t* a,
                             const std::uint8_t* b, int w, int h,
                             int stride, float t,
                             std::vector<std::uint8_t>* out,
                             std::string* error_out) {
    if (out == nullptr) {
        if (error_out) *error_out = "null output frame";
        return false;
    }
    out->clear();
    if (!IsLoaded()) {
        if (error_out) *error_out = "rife session is not loaded";
        return false;
    }
    if (a == nullptr || b == nullptr || w <= 0 || h <= 0 ||
        stride < w * 4) {
        if (error_out) *error_out = "degenerate frame pair";
        return false;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f || t >= 1.0f) {
        // Endpoint degenerate: return the verbatim endpoint — no
        // inference (and no alignment cost) needed.
        const std::uint8_t* src = t <= 0.0f ? a : b;
        out->assign(static_cast<std::size_t>(w) * h * 4u, 0);
        for (int y = 0; y < h; ++y) {
            std::memcpy(out->data() +
                            static_cast<std::size_t>(y) * w * 4u,
                        src + static_cast<std::size_t>(y) * stride,
                        static_cast<std::size_t>(w) * 4u);
        }
        return true;
    }

    const int pw = RifePadDim(w);
    const int ph = RifePadDim(h);
    const std::size_t plane =
        static_cast<std::size_t>(pw) * ph;
    nchw0_.resize(plane * 3u);
    nchw1_.resize(plane * 3u);
    BgraToPaddedNchw(a, w, h, stride, pw, ph, nchw0_.data());
    BgraToPaddedNchw(b, w, h, stride, pw, ph, nchw1_.data());

    Onnx::Input i0, i1, its;
    i0.name = "img0";
    i0.dims = {1, 3, ph, pw};
    i0.data = nchw0_.data();
    i1.name = "img1";
    i1.dims = {1, 3, ph, pw};
    i1.data = nchw1_.data();
    its.name = "timestep";
    its.dims = timestep_dims_;
    its.data = &t;

    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session_->Run({i0, i1, its}, &outs, &err)) {
        if (error_out) {
            *error_out = "rife inference failed: " + err;
        }
        return false;
    }
    const Onnx::TensorView* frame = FindOut(outs, "frame");
    if (frame == nullptr || frame->data == nullptr ||
        frame->dims.size() != 4 || frame->dims[1] != 3 ||
        frame->ElementCount() != plane * 3u) {
        if (error_out) {
            *error_out =
                "rife output contract mismatch (expected frame "
                "[1,3,ph,pw])";
        }
        return false;
    }

    // NCHW RGB 0..1 padded -> BGRA cropped to w x h.
    out->assign(static_cast<std::size_t>(w) * h * 4u, 0);
    const float* or_ = frame->data;
    const float* og = or_ + plane;
    const float* ob = og + plane;
    const auto to8 = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(v * 255.0f), 0l, 255l));
    };
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row =
            out->data() + static_cast<std::size_t>(y) * w * 4u;
        const float* rr = or_ + static_cast<std::size_t>(y) * pw;
        const float* rg = og + static_cast<std::size_t>(y) * pw;
        const float* rb = ob + static_cast<std::size_t>(y) * pw;
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = row + x * 4;
            px[0] = to8(rb[x]);
            px[1] = to8(rg[x]);
            px[2] = to8(rr[x]);
            px[3] = 0xFF;
        }
    }
    return true;
}

bool RifeInterp::InterpolateBetween(
    const std::uint8_t* a, const std::uint8_t* b, int w, int h,
    int stride, int factor,
    std::vector<std::vector<std::uint8_t>>* out,
    std::string* error_out) {
    if (out == nullptr) {
        if (error_out) *error_out = "null output list";
        return false;
    }
    out->clear();
    if (factor < 2) return true;
    if (!IsLoaded()) {
        if (error_out) *error_out = "rife session is not loaded";
        return false;
    }
    // Powers of two recurse at t=0.5; other factors take their
    // subdivided timesteps directly — see RifeInterpPlan.
    const bool pow2 = (factor & (factor - 1)) == 0;
    if (!pow2) {
        for (int k = 1; k < factor; ++k) {
            std::vector<std::uint8_t> frame;
            std::string err;
            if (!Interpolate(a, b, w, h, stride,
                             static_cast<float>(k) /
                                 static_cast<float>(factor),
                             &frame, &err)) {
                if (error_out) *error_out = err;
                return false;
            }
            out->push_back(std::move(frame));
        }
        return true;
    }
    // Recursive halving: mid = I(a,b,0.5), then recurse into
    // (a,mid) and (mid,b) until the level produces factor-1
    // intermediates. Level L synthesizes 2^L-1 frames total.
    std::vector<std::uint8_t> mid;
    std::string err;
    if (!Interpolate(a, b, w, h, stride, 0.5f, &mid, &err)) {
        if (error_out) *error_out = err;
        return false;
    }
    if (factor == 2) {
        out->push_back(std::move(mid));
        return true;
    }
    std::vector<std::vector<std::uint8_t>> left, right;
    // mid is a packed w*h*4 block — its stride is w*4, not the
    // caller's `stride`.
    if (!InterpolateBetween(a, mid.data(), w, h, w * 4, factor / 2,
                            &left, &err) ||
        !InterpolateBetween(mid.data(), b, w, h, w * 4, factor / 2,
                            &right, &err)) {
        if (error_out) *error_out = err;
        return false;
    }
    out->reserve(static_cast<std::size_t>(factor) - 1);
    for (auto& f : left) out->push_back(std::move(f));
    out->push_back(std::move(mid));
    for (auto& f : right) out->push_back(std::move(f));
    return true;
}

}  // namespace XER::VideoEditor::Ai
