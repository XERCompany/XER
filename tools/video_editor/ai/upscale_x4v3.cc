// Implementation of ai/upscale_x4v3.h — the x4v3 session wrapper.
// The model is a clean [B,3,H,W] RGB f32 0..1 -> [B,3,4H,4W] map;
// the wrapper converts BGRA <-> NCHW blocks and leaves tiling to
// ai/img2img_runner.
#include "ai/upscale_x4v3.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace Onnx = XER::AI::Onnx;

}  // namespace

std::string UpscaleModel() { return Lab::FindAiModel(kX4v3File); }

const char* UpscaleModelHint() {
    return "model not installed — put realesr-general-x4v3.onnx in "
           "assets/models/ next to the exe, or point "
           "XER_AI_MODELS_DIR at its folder";
}

bool UpscaleAvailable() {
    return !UpscaleModel().empty() &&
           !Onnx::ActiveBackendId().empty();
}

UpscaleX4v3::UpscaleX4v3() = default;
UpscaleX4v3::~UpscaleX4v3() = default;

bool UpscaleX4v3::Load(const std::string& model_path,
                       const std::string& backend,
                       std::string* error_out) {
    session_.reset();
    input_name_.clear();
    output_name_.clear();
    if (model_path.empty()) {
        if (error_out) *error_out = UpscaleModelHint();
        return false;
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, backend, &err)) {
        if (error_out) {
            *error_out = "x4v3 load failed: " +
                         (err.empty()
                              ? std::string("rejected by every ONNX "
                                            "backend")
                              : err);
        }
        return false;
    }
    // Contract audit: one image input + one image output. Names are
    // captured (not assumed) so a re-export with different labels
    // still binds.
    if (session->InputNames().empty() ||
        session->OutputNames().empty()) {
        if (error_out) {
            *error_out =
                "x4v3 model declares no inputs/outputs";
        }
        return false;
    }
    input_name_ = session->InputNames().front();
    output_name_ = session->OutputNames().front();
    session_ = std::move(session);
    return true;
}

bool UpscaleX4v3::IsLoaded() const {
    return session_ != nullptr && session_->IsLoaded();
}

const std::string& UpscaleX4v3::BackendId() const {
    static const std::string kEmpty;
    return session_ != nullptr ? session_->BackendId() : kEmpty;
}

bool UpscaleX4v3::RunTile(const std::uint8_t* bgra, int w, int h,
                          int stride, std::vector<std::uint8_t>* out,
                          std::string* error_out) {
    if (out == nullptr) {
        if (error_out) *error_out = "null output block";
        return false;
    }
    out->clear();
    if (!IsLoaded()) {
        if (error_out) *error_out = "x4v3 session is not loaded";
        return false;
    }
    if (bgra == nullptr || w <= 0 || h <= 0 || stride < w * 4) {
        if (error_out) *error_out = "degenerate tile input";
        return false;
    }

    // BGRA -> NCHW RGB 0..1 (the export's only normalization).
    const std::size_t n =
        static_cast<std::size_t>(3) * w * h;
    nchw_.resize(n);
    float* r = nchw_.data();
    float* g = r + static_cast<std::size_t>(w) * h;
    float* b = g + static_cast<std::size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row =
            bgra + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* px = row + x * 4;
            const std::size_t i =
                static_cast<std::size_t>(y) * w + x;
            r[i] = px[2] / 255.0f;
            g[i] = px[1] / 255.0f;
            b[i] = px[0] / 255.0f;
        }
    }

    Onnx::Input input;
    input.name = input_name_;
    input.dims = {1, 3, h, w};
    input.data = nchw_.data();
    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session_->Run({input}, &outs, &err)) {
        if (error_out) {
            *error_out = "x4v3 inference failed: " + err;
        }
        return false;
    }
    const Onnx::TensorView* view = nullptr;
    for (const Onnx::TensorView& t : outs) {
        if (t.name == output_name_) {
            view = &t;
            break;
        }
    }
    const int ow = w * kX4v3Scale;
    const int oh = h * kX4v3Scale;
    const std::size_t expected =
        static_cast<std::size_t>(3) * ow * oh;
    if (view == nullptr || view->data == nullptr ||
        view->dims.size() != 4 || view->dims[1] != 3 ||
        view->ElementCount() != expected) {
        if (error_out) {
            *error_out =
                "x4v3 output contract mismatch (expected [1,3,4h,"
                "4w])";
        }
        return false;
    }

    // NCHW RGB 0..1 -> BGRA, clamped.
    out->assign(static_cast<std::size_t>(ow) * oh * 4u, 0);
    const float* or_ = view->data;
    const float* og = or_ + static_cast<std::size_t>(ow) * oh;
    const float* ob = og + static_cast<std::size_t>(ow) * oh;
    const auto to8 = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(v * 255.0f), 0l, 255l));
    };
    for (int y = 0; y < oh; ++y) {
        std::uint8_t* row =
            out->data() + static_cast<std::size_t>(y) * ow * 4u;
        for (int x = 0; x < ow; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(y) * ow + x;
            std::uint8_t* px = row + x * 4;
            px[0] = to8(ob[i]);
            px[1] = to8(og[i]);
            px[2] = to8(or_[i]);
            px[3] = 0xFF;
        }
    }
    return true;
}

bool UpscaleX4v3::UpscaleFrame(
    const std::uint8_t* bgra, int w, int h, int stride,
    const Img2ImgParams& params,
    const std::function<void(float)>& progress,
    const std::function<bool()>& should_cancel,
    std::vector<std::uint8_t>* out, int* out_w, int* out_h,
    std::string* error_out) {
    if (!IsLoaded()) {
        if (error_out) *error_out = "x4v3 session is not loaded";
        return false;
    }
    Img2ImgParams p = params;
    p.scale = kX4v3Scale;
    Img2ImgTileFn tile_fn =
        [this](const std::uint8_t* tile, int tw, int th, int ts,
               std::uint8_t* dst, int dw, int dh, int ds,
               std::string* err) -> bool {
        if (dw != tw * kX4v3Scale || dh != th * kX4v3Scale) {
            if (err) *err = "x4v3 tile scale mismatch";
            return false;
        }
        std::vector<std::uint8_t> block;
        if (!RunTile(tile, tw, th, ts, &block, err)) return false;
        for (int y = 0; y < dh; ++y) {
            std::memcpy(dst + static_cast<std::size_t>(y) * ds,
                    block.data() + static_cast<std::size_t>(y) * dw * 4u,
                    static_cast<std::size_t>(dw) * 4u);
        }
        return true;
    };
    return RunImageToImage(bgra, w, h, stride, p, tile_fn, progress,
                           should_cancel, out, out_w, out_h,
                           error_out);
}

}  // namespace XER::VideoEditor::Ai
