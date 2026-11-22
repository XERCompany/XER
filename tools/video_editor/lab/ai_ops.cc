// Implementation of ai_ops.h — BiRefNet matting + EfficientSAM-Ti
// smart-select on the shared XER::AI::Onnx session. The heavy lifting
// (backend probe/load/run) lives in onnx_session; this file owns the
// model contracts: plain-stretch square resize (neither staged model
// letterboxes — see assets/models/README.md), ImageNet normalization
// for BiRefNet, /255 for EfficientSAM, sigmoid matte / best-IoU mask
// decode, and resize-back to source pixels.
#include "lab/ai_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "content/ai/onnx_session/onnx_session.h"

namespace XER::VideoEditor::Lab {
namespace {

namespace Onnx = XER::AI::Onnx;

// ---- model discovery ---------------------------------------------------

std::string ProbeModelDirs(const char* file_name) {
    std::error_code ec;
    if (const char* env = std::getenv("XER_AI_MODELS_DIR");
        env != nullptr && *env != '\0') {
        const auto p = std::filesystem::path(env) / file_name;
        if (std::filesystem::is_regular_file(p, ec)) {
            return p.string();
        }
    }
#if defined(_WIN32)
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        const auto dir = std::filesystem::path(exe).parent_path();
        for (const auto& sub : {"assets/models", "models"}) {
            const auto p = dir / sub / file_name;
            if (std::filesystem::is_regular_file(p, ec)) {
                return p.string();
            }
        }
    }
#endif
    return {};
}

// ---- session cache -------------------------------------------------------
//
// Lazy per-model-path sessions guarded by one mutex, which also
// serializes Run: ORT/cv::dnn sessions reuse their output buffers, so
// concurrent runs on one cached session would race. The lab runs one
// AsyncOp at a time anyway; the mutex only costs correctness.

std::mutex g_sessions_mutex;
std::map<std::string, std::unique_ptr<Onnx::Session>> g_sessions;

Onnx::Session* SessionFor(const std::string& model_path,
                          std::string* error_out) {
    std::lock_guard lock(g_sessions_mutex);
    if (const auto it = g_sessions.find(model_path);
        it != g_sessions.end()) {
        return it->second.get();
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, "", &err)) {
        if (error_out) {
            *error_out = err.empty()
                             ? "model rejected by every ONNX backend"
                             : err;
        }
        return nullptr;
    }
    Onnx::Session* raw = session.get();
    g_sessions.emplace(model_path, std::move(session));
    return raw;
}

// The model's square input edge: declared static dims first, then the
// ONNX header probe, then the staged-model contract default (both
// BiRefNet-Lite and EfficientSAM-Ti are 1024).
int InputSquareFor(const Onnx::Session& session,
                   const std::string& model_path) {
    const std::vector<std::int64_t> d0 = session.InputDims(0);
    if (d0.size() == 4 && d0[2] == d0[3] && d0[2] > 0) {
        return static_cast<int>(d0[2]);
    }
    const int probed = Onnx::ProbeInputSquare(model_path);
    return probed > 0 ? probed : 1024;
}

// ---- image <-> tensor --------------------------------------------------

// Plain-stretch bilinear resize of packed BGRA8 into NCHW f32 planes
// (no letterbox — both staged models take the full square). `map`
// converts a 0..255 channel value into the tensor's expected range:
// v/255 for /255 models, (v/255 - mean)/std for normalized ones.
template <typename Map>
void BgraStretchToNchw(const std::uint8_t* bgra, int w, int h,
                       int stride, int size, const Map& map,
                       float* out) {
    const int n = size * size;
    for (int y = 0; y < size; ++y) {
        const float sy =
            (static_cast<float>(y) + 0.5f) * h / size - 0.5f;
        const int y0 =
            std::clamp(static_cast<int>(std::floor(sy)), 0, h - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float fy = std::clamp(sy - y0, 0.0f, 1.0f);
        for (int x = 0; x < size; ++x) {
            const float sx =
                (static_cast<float>(x) + 0.5f) * w / size - 0.5f;
            const int x0 =
                std::clamp(static_cast<int>(std::floor(sx)), 0, w - 1);
            const int x1 = std::min(x0 + 1, w - 1);
            const float fx = std::clamp(sx - x0, 0.0f, 1.0f);
            const std::uint8_t* p00 = bgra + y0 * stride + x0 * 4;
            const std::uint8_t* p10 = bgra + y0 * stride + x1 * 4;
            const std::uint8_t* p01 = bgra + y1 * stride + x0 * 4;
            const std::uint8_t* p11 = bgra + y1 * stride + x1 * 4;
            for (int c = 0; c < 3; ++c) {
                const int ch = 2 - c;   // BGRA -> R,G,B planes
                const float v =
                    (p00[ch] * (1.0f - fx) + p10[ch] * fx) *
                        (1.0f - fy) +
                    (p01[ch] * (1.0f - fx) + p11[ch] * fx) * fy;
                out[c * n + y * size + x] = map(c, v);
            }
        }
    }
}

// Bilinear resize of a single-channel float plane [sw,sh] -> w*h u8
// mask. `map` converts the float sample into 0..255 coverage.
template <typename Map>
std::vector<std::uint8_t> ResizePlaneToMask(const float* src, int sw,
                                            int sh, int w, int h,
                                            const Map& map) {
    std::vector<std::uint8_t> mask(
        static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const float sy =
            (static_cast<float>(y) + 0.5f) * sh / h - 0.5f;
        const int y0 =
            std::clamp(static_cast<int>(std::floor(sy)), 0, sh - 1);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = std::clamp(sy - y0, 0.0f, 1.0f);
        for (int x = 0; x < w; ++x) {
            const float sx =
                (static_cast<float>(x) + 0.5f) * sw / w - 0.5f;
            const int x0 =
                std::clamp(static_cast<int>(std::floor(sx)), 0, sw - 1);
            const int x1 = std::min(x0 + 1, sw - 1);
            const float fx = std::clamp(sx - x0, 0.0f, 1.0f);
            const float v =
                (src[y0 * sw + x0] * (1.0f - fx) +
                 src[y0 * sw + x1] * fx) *
                    (1.0f - fy) +
                (src[y1 * sw + x0] * (1.0f - fx) +
                 src[y1 * sw + x1] * fx) *
                    fy;
            mask[static_cast<std::size_t>(y) * w + x] = map(v);
        }
    }
    return mask;
}

bool Cancelled(const AiCallbacks& cb, std::string* error_out) {
    if (cb.should_cancel && cb.should_cancel()) {
        if (error_out) *error_out = "cancelled";
        return true;
    }
    return false;
}

void Report(const AiCallbacks& cb, float f) {
    if (cb.report_progress) cb.report_progress(f);
}

// First f32 output at/above a minimum element count — the matte/mask
// tensors dwarf any side outputs.
const Onnx::TensorView* BiggestOutput(
    const std::vector<Onnx::TensorView>& outs) {
    const Onnx::TensorView* best = nullptr;
    for (const Onnx::TensorView& t : outs) {
        if (t.data == nullptr) continue;
        if (best == nullptr ||
            t.ElementCount() > best->ElementCount()) {
            best = &t;
        }
    }
    return best;
}

// Finds a session output whose NAME contains `sub` (case-insensitive),
// else nullptr — callers fall back to shape matching.
const Onnx::TensorView* NamedOutput(
    const std::vector<Onnx::TensorView>& outs, const char* sub) {
    std::string needle = sub;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    for (const Onnx::TensorView& t : outs) {
        std::string low = t.name;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (low.find(needle) != std::string::npos) return &t;
    }
    return nullptr;
}

}  // namespace

std::string FindAiModel(const char* file_name) {
    if (file_name == nullptr || *file_name == '\0') return {};
    return ProbeModelDirs(file_name);
}

std::string AiModelHint(const char* file_name) {
    return std::string("model not installed — put ") +
           (file_name != nullptr ? file_name : "<model>") +
           " in assets/models/ next to the exe, or point "
           "XER_AI_MODELS_DIR at its folder";
}

std::vector<std::uint8_t> AlphaMatte(const video::Frame& frame,
                                     const std::string& model_path,
                                     const AiCallbacks& cb,
                                     std::string* error_out) {
    if (frame.Format() != video::PixelFormat::BGRA8 ||
        !frame.IsValid()) {
        if (error_out) *error_out = "AlphaMatte needs a BGRA8 frame";
        return {};
    }
    if (model_path.empty()) {
        if (error_out) *error_out = AiModelHint("birefnet_lite.onnx");
        return {};
    }
    Onnx::Session* session = SessionFor(model_path, error_out);
    if (session == nullptr) return {};
    std::lock_guard lock(g_sessions_mutex);   // serialize Run below

    const int w = frame.Width(), h = frame.Height();
    const int in = InputSquareFor(*session, model_path);
    const std::size_t n = static_cast<std::size_t>(3) * in * in;
    std::vector<float> nchw(n);
    // ImageNet normalization per the BiRefNet contract
    // (assets/models/README.md): (v/255 - mean)/std, R,G,B order.
    constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
    constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};
    BgraStretchToNchw(frame.Data(), w, h, frame.Stride(), in,
                      [&](int c, float v) {
                          return (v / 255.0f - kMean[c]) / kStd[c];
                      },
                      nchw.data());
    Report(cb, 0.15f);
    if (Cancelled(cb, error_out)) return {};

    Onnx::Input input;
    input.dims = {1, 3, in, in};
    input.data = nchw.data();
    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session->Run({input}, &outs, &err)) {
        if (error_out) {
            *error_out = err.empty() ? "matting inference failed" : err;
        }
        return {};
    }
    Report(cb, 0.85f);
    if (Cancelled(cb, error_out)) return {};

    const Onnx::TensorView* out = BiggestOutput(outs);
    if (out == nullptr || out->dims.size() != 4 ||
        out->dims[2] <= 0 || out->dims[3] <= 0) {
        if (error_out) *error_out = "unexpected matte output shape";
        return {};
    }
    const int sw = static_cast<int>(out->dims[3]);
    const int sh = static_cast<int>(out->dims[2]);
    const auto sigmoid = [](float x) {
        return 1.0f / (1.0f + std::exp(-x));
    };
    auto mask = ResizePlaneToMask(
        out->data, sw, sh, w, h, [&](float logit) {
            const float a = std::clamp(sigmoid(logit), 0.0f, 1.0f);
            return static_cast<std::uint8_t>(
                std::lround(a * 255.0f));
        });
    Report(cb, 1.0f);
    return mask;
}

video::Frame RemoveBackground(const video::Frame& frame,
                              const std::string& model_path,
                              const AiCallbacks& cb,
                              std::string* error_out) {
    const auto matte = AlphaMatte(frame, model_path, cb, error_out);
    if (matte.empty()) return {};
    video::Frame out;
    if (!out.Allocate(frame.Width(), frame.Height(),
                      video::PixelFormat::BGRA8)) {
        if (error_out) *error_out = "frame allocation failed";
        return {};
    }
    const std::size_t px =
        static_cast<std::size_t>(frame.Width()) * frame.Height();
    const std::uint8_t* src = frame.Data();
    std::uint8_t* dst = out.Data();
    for (std::size_t i = 0; i < px; ++i) {
        dst[i * 4 + 0] = src[i * 4 + 0];
        dst[i * 4 + 1] = src[i * 4 + 1];
        dst[i * 4 + 2] = src[i * 4 + 2];
        dst[i * 4 + 3] = matte[i];
    }
    return out;
}

std::vector<std::uint8_t> SmartSelectMask(
    const video::Frame& frame, const std::vector<IntPoint>& points,
    const std::string& model_path, const AiCallbacks& cb,
    std::string* error_out) {
    if (frame.Format() != video::PixelFormat::BGRA8 ||
        !frame.IsValid()) {
        if (error_out) *error_out = "SmartSelect needs a BGRA8 frame";
        return {};
    }
    if (points.empty()) {
        if (error_out) {
            *error_out = "smart select needs at least one click point";
        }
        return {};
    }
    if (model_path.empty()) {
        if (error_out) {
            *error_out = AiModelHint("efficientsam_ti.onnx");
        }
        return {};
    }
    Onnx::Session* session = SessionFor(model_path, error_out);
    if (session == nullptr) return {};
    std::lock_guard lock(g_sessions_mutex);   // serialize Run below

    const int w = frame.Width(), h = frame.Height();
    const int in = InputSquareFor(*session, model_path);
    std::vector<float> nchw(static_cast<std::size_t>(3) * in * in);
    BgraStretchToNchw(frame.Data(), w, h, frame.Stride(), in,
                      [](int, float v) { return v / 255.0f; },
                      nchw.data());

    // Prompt tensors per the EfficientSAM contract: up to 6 points,
    // zero-padded; labels 1 = foreground point, -1 = pad.
    constexpr int kMaxPoints = 6;
    std::vector<float> coords(2 * kMaxPoints, 0.0f);
    std::vector<float> labels(kMaxPoints, -1.0f);
    const int used = std::min<int>(
        kMaxPoints, static_cast<int>(points.size()));
    for (int i = 0; i < used; ++i) {
        const float fx = std::clamp<float>(points[i].x, 0, w - 1);
        const float fy = std::clamp<float>(points[i].y, 0, h - 1);
        coords[static_cast<std::size_t>(i) * 2 + 0] =
            fx * in / w;
        coords[static_cast<std::size_t>(i) * 2 + 1] =
            fy * in / h;
        labels[static_cast<std::size_t>(i)] = 1.0f;
    }
    Report(cb, 0.15f);
    if (Cancelled(cb, error_out)) return {};

    Onnx::Input image, pts, lbl;
    image.name = "batched_images";
    image.dims = {1, 3, in, in};
    image.data = nchw.data();
    pts.name = "batched_point_coords";
    pts.dims = {1, 1, kMaxPoints, 2};
    pts.data = coords.data();
    lbl.name = "batched_point_labels";
    lbl.dims = {1, 1, kMaxPoints};
    lbl.data = labels.data();
    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session->Run({image, pts, lbl}, &outs, &err)) {
        if (error_out) {
            *error_out =
                err.empty() ? "smart-select inference failed" : err;
        }
        return {};
    }
    Report(cb, 0.85f);
    if (Cancelled(cb, error_out)) return {};

    // Contract: output_masks [1,1,3,S,S] logits (>=0 = mask) and
    // iou_predictions [1,1,3] — pick the best-IoU proposal. Names
    // confirm the pairing; shapes are the fallback.
    const Onnx::TensorView* masks = NamedOutput(outs, "mask");
    const Onnx::TensorView* ious = NamedOutput(outs, "iou");
    for (const Onnx::TensorView& t : outs) {
        // Shape fallback: the big rank>=4 tensor is the masks.
        if (masks == nullptr && t.dims.size() >= 4 &&
            t.dims.back() > 16) {
            masks = &t;
        }
        if (ious == nullptr && t.data != nullptr &&
            t.ElementCount() <= 16) {
            ious = &t;
        }
    }
    if (masks == nullptr || masks->data == nullptr ||
        masks->dims.size() < 3) {
        if (error_out) *error_out = "unexpected mask output shape";
        return {};
    }
    // Contract shape [1,1,3,S,S] (rank 5; some exports squeeze to
    // rank 4 [1,3,S,S]): the two trailing dims are the square edge,
    // dims[-3] the proposal count.
    const std::size_t rank = masks->dims.size();
    const int side_h = static_cast<int>(masks->dims[rank - 2]);
    const int side_w = static_cast<int>(masks->dims[rank - 1]);
    const int proposals = static_cast<int>(masks->dims[rank - 3]);
    if (side_h <= 0 || side_w <= 0 || proposals < 1) {
        if (error_out) *error_out = "unexpected mask output shape";
        return {};
    }
    const int side = side_h;
    int best = 0;
    if (ious != nullptr && ious->data != nullptr) {
        float best_iou = -1e30f;
        const std::size_t count =
            std::min<std::size_t>(proposals, ious->ElementCount());
        for (std::size_t i = 0; i < count; ++i) {
            if (ious->data[i] > best_iou) {
                best_iou = ious->data[i];
                best = static_cast<int>(i);
            }
        }
    }
    const float* plane =
        masks->data +
        static_cast<std::size_t>(best) * side * side;
    auto mask = ResizePlaneToMask(
        plane, side, side, w, h,
        [](float logit) -> std::uint8_t {
            // >=0 logit = inside the instance (zoo demo threshold).
            return logit >= 0.0f ? 255 : 0;
        });
    Report(cb, 1.0f);
    return mask;
}

}  // namespace XER::VideoEditor::Lab
