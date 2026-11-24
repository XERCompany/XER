// Implementation of ai/sam2_session.h — SAM2.1-hiera-tiny encoder +
// prompt decoder on the shared dual-backend ONNX session. Contracts
// verified 2026-11-24 against the staged fp32 protos (see
// assets/models/README.md): preprocess is stretch-to-1024 RGB /255
// with ImageNet normalization (preprocessor_config.json); prompt
// tensors follow the transformers.js Sam2Processor host contract —
// points in original-image px rescaled into the 1024 encoder space,
// xyxy boxes, i64 labels (1 fg / 0 bg / -1 pad), empty tensors for
// the unused prompt kind, best-of-3 head by iou_scores.
#include "ai/sam2_session.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace Onnx = XER::AI::Onnx;

// ImageNet normalization from the staged preprocessor_config.json.
constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};

// Dummy non-null pointer for EMPTY prompt tensors — the session
// requires a data pointer even when the element count is 0 (the
// ORT binding creates a 0-byte tensor; the pointer is never read).
float g_empty_tensor_cell = 0.0f;
std::int64_t g_empty_tensor_cell_i64 = 0;

// BGRA8 -> NCHW f32, plain-stretch bilinear resize to the model
// square, channel order R,G,B (SAM wants RGB — unlike YuNet's BGR),
// /255 then ImageNet mean/std (the export's preprocessor contract).
void BgraStretchToSam2(const std::uint8_t* bgra, int w, int h,
                       int stride, int size, float* out) {
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
                // BGRA -> RGB: model channel c reads byte 2-c.
                const float v =
                    (p00[2 - c] * (1.0f - fx) + p10[2 - c] * fx) *
                        (1.0f - fy) +
                    (p01[2 - c] * (1.0f - fx) + p11[2 - c] * fx) * fy;
                out[c * n + y * size + x] =
                    (v * (1.0f / 255.0f) - kMean[c]) / kStd[c];
            }
        }
    }
}

// Finds an output tensor by exact name (the embedding names carry a
// '.' so prefix/suffix tricks would be fragile — exact match only).
const Onnx::TensorView* FindOut(
    const std::vector<Onnx::TensorView>& outs, const char* name) {
    for (const Onnx::TensorView& t : outs) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

}  // namespace

std::string Sam2EncoderModel() {
    return Lab::FindAiModel(kSam2EncoderFile);
}
std::string Sam2DecoderModel() {
    return Lab::FindAiModel(kSam2DecoderFile);
}

const char* Sam2ModelHint() {
    return "SAM2.1 models not installed — put vision_encoder.onnx + "
           "vision_encoder.onnx_data + prompt_encoder_mask_decoder."
           "onnx + .onnx_data in assets/models/sam21/ next to the "
           "exe (or point XER_AI_MODELS_DIR at a folder holding a "
           "sam21/ subdirectory)";
}

bool Sam2ModelsStaged() {
    return !Sam2EncoderModel().empty() && !Sam2DecoderModel().empty();
}

// ---- mask helpers -------------------------------------------------

bool Sam2MaskBounds(const float* logits, int mw, int mh, float* cx,
                    float* cy, float* w, float* h) {
    if (logits == nullptr || mw <= 0 || mh <= 0 || cx == nullptr ||
        cy == nullptr || w == nullptr || h == nullptr) {
        return false;
    }
    int x0 = mw, y0 = mh, x1 = -1, y1 = -1;
    for (int y = 0; y < mh; ++y) {
        const float* row = logits + static_cast<std::size_t>(y) * mw;
        for (int x = 0; x < mw; ++x) {
            if (row[x] >= 0.0f) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < x0 || y1 < y0) return false;
    const double fw = static_cast<double>(mw);
    const double fh = static_cast<double>(mh);
    *w = static_cast<float>((x1 - x0 + 1) / fw);
    *h = static_cast<float>((y1 - y0 + 1) / fh);
    *cx = static_cast<float>((x0 + (x1 - x0 + 1) * 0.5) / fw);
    *cy = static_cast<float>((y0 + (y1 - y0 + 1) * 0.5) / fh);
    return true;
}

bool Sam2MaskCentroid(const float* logits, int mw, int mh, float* cx,
                      float* cy) {
    if (logits == nullptr || mw <= 0 || mh <= 0 || cx == nullptr ||
        cy == nullptr) {
        return false;
    }
    double sx = 0.0, sy = 0.0;
    std::size_t n = 0;
    for (int y = 0; y < mh; ++y) {
        const float* row = logits + static_cast<std::size_t>(y) * mw;
        for (int x = 0; x < mw; ++x) {
            if (row[x] >= 0.0f) {
                sx += x + 0.5;
                sy += y + 0.5;
                ++n;
            }
        }
    }
    if (n == 0) return false;
    *cx = static_cast<float>(sx / n / mw);
    *cy = static_cast<float>(sy / n / mh);
    return true;
}

std::vector<std::uint8_t> Sam2MaskBits(const float* logits, int mw,
                                       int mh, int gw, int gh) {
    const std::size_t nbits =
        static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
    std::vector<std::uint8_t> bits((nbits + 7) / 8, 0);
    if (logits == nullptr || mw <= 0 || mh <= 0 || gw <= 0 ||
        gh <= 0) {
        return bits;
    }
    for (int gy = 0; gy < gh; ++gy) {
        // Source rect this cell covers (integer-bounded).
        const int y0 = gy * mh / gh;
        const int y1 = std::max(y0 + 1, (gy + 1) * mh / gh);
        for (int gx = 0; gx < gw; ++gx) {
            const int x0 = gx * mw / gw;
            const int x1 = std::max(x0 + 1, (gx + 1) * mw / gw);
            unsigned on = 0, total = 0;
            for (int y = y0; y < y1; ++y) {
                const float* row =
                    logits + static_cast<std::size_t>(y) * mw;
                for (int x = x0; x < x1; ++x) {
                    on += row[x] >= 0.0f ? 1u : 0u;
                    ++total;
                }
            }
            if (on * 2 >= total && total > 0) {
                const std::size_t i =
                    static_cast<std::size_t>(gy) * gw + gx;
                bits[i >> 3] |=
                    static_cast<std::uint8_t>(1u << (7 - (i & 7)));
            }
        }
    }
    return bits;
}

float Sam2BitsIoU(const std::uint8_t* a, const std::uint8_t* b,
                  int gw, int gh) {
    if (a == nullptr || b == nullptr || gw <= 0 || gh <= 0) {
        return 0.0f;
    }
    const std::size_t nbytes =
        (static_cast<std::size_t>(gw) * gh + 7) / 8;
    unsigned inter = 0, uni = 0;
    for (std::size_t i = 0; i < nbytes; ++i) {
        inter += static_cast<unsigned>(std::popcount(
            static_cast<unsigned>(a[i] & b[i])));
        uni += static_cast<unsigned>(std::popcount(
            static_cast<unsigned>(a[i] | b[i])));
    }
    return uni > 0 ? static_cast<float>(inter) / uni : 0.0f;
}

// ---- encoder ------------------------------------------------------

Sam2Encoder::Sam2Encoder() = default;
Sam2Encoder::~Sam2Encoder() = default;

bool Sam2Encoder::Load(const std::string& model_path,
                       std::string* error_out) {
    session_.reset();
    if (model_path.empty()) {
        if (error_out) *error_out = Sam2ModelHint();
        return false;
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, "", &err)) {
        if (error_out) {
            *error_out = "sam2 vision encoder load failed: " +
                         (err.empty()
                              ? std::string("rejected by every ONNX "
                                            "backend")
                              : err);
        }
        return false;
    }
    session_ = std::move(session);
    nchw_.assign(static_cast<std::size_t>(3) * kSam2InputSize *
                     kSam2InputSize,
                 0.0f);
    return true;
}

bool Sam2Encoder::IsLoaded() const {
    return session_ != nullptr && session_->IsLoaded();
}

const std::string& Sam2Encoder::BackendId() const {
    static const std::string kEmpty;
    return session_ != nullptr ? session_->BackendId() : kEmpty;
}

bool Sam2Encoder::Encode(const std::uint8_t* bgra, int w, int h,
                         int stride, Sam2Embedding* out,
                         std::string* error_out) {
    if (out == nullptr) return false;
    *out = Sam2Embedding{};
    if (!IsLoaded()) {
        if (error_out) *error_out = "sam2 encoder not loaded";
        return false;
    }
    if (bgra == nullptr || w <= 0 || h <= 0 || stride < w * 4) {
        if (error_out) *error_out = "degenerate frame";
        return false;
    }
    BgraStretchToSam2(bgra, w, h, stride, kSam2InputSize,
                      nchw_.data());
    Onnx::Input input;
    input.name = "pixel_values";
    input.dims = {1, 3, kSam2InputSize, kSam2InputSize};
    input.data = nchw_.data();
    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session_->Run({input}, &outs, &err)) {
        if (error_out) {
            *error_out = "sam2 encoder run failed: " + err;
        }
        return false;
    }
    static const char* kNames[3] = {
        "image_embeddings.0", "image_embeddings.1",
        "image_embeddings.2"};
    for (int i = 0; i < 3; ++i) {
        const Onnx::TensorView* t = FindOut(outs, kNames[i]);
        if (t == nullptr || t->data == nullptr ||
            t->ElementCount() == 0) {
            if (error_out) {
                *error_out = std::string("sam2 encoder output '") +
                             kNames[i] + "' missing";
            }
            return false;
        }
        out->feats[i].assign(t->data, t->data + t->ElementCount());
        out->dims[i] = t->dims;
    }
    out->src_w = w;
    out->src_h = h;
    return true;
}

// ---- decoder ------------------------------------------------------

Sam2Decoder::Sam2Decoder() = default;
Sam2Decoder::~Sam2Decoder() = default;

bool Sam2Decoder::Load(const std::string& model_path,
                       std::string* error_out) {
    session_.reset();
    if (model_path.empty()) {
        if (error_out) *error_out = Sam2ModelHint();
        return false;
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, "", &err)) {
        if (error_out) {
            *error_out = "sam2 prompt decoder load failed: " +
                         (err.empty()
                              ? std::string("rejected by every ONNX "
                                            "backend")
                              : err);
        }
        return false;
    }
    session_ = std::move(session);
    return true;
}

bool Sam2Decoder::IsLoaded() const {
    return session_ != nullptr && session_->IsLoaded();
}

const std::string& Sam2Decoder::BackendId() const {
    static const std::string kEmpty;
    return session_ != nullptr ? session_->BackendId() : kEmpty;
}

bool Sam2Decoder::Decode(const Sam2Embedding& emb,
                         const Sam2Prompt& prompt, Sam2Mask* out,
                         std::string* error_out) {
    if (out == nullptr) return false;
    *out = Sam2Mask{};
    if (!IsLoaded()) {
        if (error_out) *error_out = "sam2 decoder not loaded";
        return false;
    }
    if (!emb.Valid()) {
        if (error_out) *error_out = "degenerate image embedding";
        return false;
    }
    const std::size_t n_points = prompt.points.size() / 2;
    if (n_points == 0 && !prompt.has_box) {
        if (error_out) {
            *error_out = "sam2 prompt needs a point or a box";
        }
        return false;
    }
    if (prompt.labels.size() < n_points) {
        if (error_out) {
            *error_out = "sam2 prompt labels shorter than points";
        }
        return false;
    }

    // Frame px -> the 1024 encoder space (SamImageProcessor's
    // reshape_input_points: scale by reshaped/original per axis).
    const float sx =
        static_cast<float>(kSam2InputSize) / emb.src_w;
    const float sy =
        static_cast<float>(kSam2InputSize) / emb.src_h;

    std::vector<Onnx::Input> inputs;
    inputs.reserve(6);

    // input_points f32 [1,1,N,2] — empty [1,1,0,2] for box-only.
    Onnx::Input in_points;
    in_points.name = "input_points";
    std::vector<float> pts(n_points * 2);
    for (std::size_t i = 0; i < n_points; ++i) {
        pts[2 * i] = prompt.points[2 * i] * sx;
        pts[2 * i + 1] = prompt.points[2 * i + 1] * sy;
    }
    if (n_points > 0) {
        in_points.dims = {1, 1,
                          static_cast<std::int64_t>(n_points), 2};
        in_points.data = pts.data();
    } else {
        in_points.dims = {1, 1, 0, 2};
        in_points.data = &g_empty_tensor_cell;
    }
    inputs.push_back(in_points);

    // input_labels i64 — [1,1,N] for point prompts (labels ride the
    // point-batch dim, num_boxes==1 here); empty [1,1,0] box-only.
    Onnx::Input in_labels;
    in_labels.name = "input_labels";
    if (n_points > 0) {
        in_labels.dims = {1, 1, static_cast<std::int64_t>(n_points)};
        in_labels.data_i64 = prompt.labels.data();
    } else {
        in_labels.dims = {1, 1, 0};
        in_labels.data_i64 = &g_empty_tensor_cell_i64;
    }
    inputs.push_back(in_labels);

    // input_boxes f32 [1,M,4] xyxy — [1,0,4] empty for point-only.
    Onnx::Input in_boxes;
    in_boxes.name = "input_boxes";
    float box_scaled[4];
    if (prompt.has_box) {
        box_scaled[0] = prompt.box[0] * sx;
        box_scaled[1] = prompt.box[1] * sy;
        box_scaled[2] = prompt.box[2] * sx;
        box_scaled[3] = prompt.box[3] * sy;
        in_boxes.dims = {1, 1, 4};
        in_boxes.data = box_scaled;
    } else {
        in_boxes.dims = {1, 0, 4};
        in_boxes.data = &g_empty_tensor_cell;
    }
    inputs.push_back(in_boxes);

    // image_embeddings.{0,1,2} verbatim from the encoder run.
    static const char* kEmbNames[3] = {
        "image_embeddings.0", "image_embeddings.1",
        "image_embeddings.2"};
    Onnx::Input in_emb[3];
    for (int i = 0; i < 3; ++i) {
        in_emb[i].name = kEmbNames[i];
        in_emb[i].dims = emb.dims[i];
        in_emb[i].data = emb.feats[i].data();
        inputs.push_back(in_emb[i]);
    }

    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session_->Run(inputs, &outs, &err)) {
        if (error_out) {
            *error_out = "sam2 decoder run failed: " + err;
        }
        return false;
    }

    const Onnx::TensorView* iou = FindOut(outs, "iou_scores");
    const Onnx::TensorView* masks = FindOut(outs, "pred_masks");
    const Onnx::TensorView* obj =
        FindOut(outs, "object_score_logits");
    if (iou == nullptr || masks == nullptr ||
        iou->dims.size() < 3 || masks->dims.size() < 5 ||
        iou->ElementCount() < 3) {
        if (error_out) {
            *error_out = "sam2 decoder outputs missing/misshaped";
        }
        return false;
    }
    // pred_masks [1, n, 3, S, S] — pick the best-IoU head of row 0
    // (every prompt this wrapper builds produces n == 1).
    const std::int64_t n_heads = iou->dims[2];
    const std::int64_t s_h = masks->dims[3];
    const std::int64_t s_w = masks->dims[4];
    if (n_heads <= 0 || s_h <= 0 || s_w <= 0 ||
        masks->ElementCount() <
            static_cast<std::size_t>(n_heads * s_h * s_w)) {
        if (error_out) {
            *error_out = "sam2 decoder produced degenerate masks";
        }
        return false;
    }
    int best = 0;
    for (std::int64_t i = 1; i < n_heads; ++i) {
        if (iou->data[i] > iou->data[best]) {
            best = static_cast<int>(i);
        }
    }
    const std::size_t plane =
        static_cast<std::size_t>(s_h) * s_w;
    out->logits.assign(
        masks->data + static_cast<std::size_t>(best) * plane,
        masks->data + (static_cast<std::size_t>(best) + 1) * plane);
    out->w = static_cast<int>(s_w);
    out->h = static_cast<int>(s_h);
    out->iou = iou->data[best];
    if (obj != nullptr && obj->ElementCount() > 0) {
        const float l = obj->data[0];
        out->object_score =
            1.0f / (1.0f + std::exp(-std::clamp(l, -30.0f, 30.0f)));
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
