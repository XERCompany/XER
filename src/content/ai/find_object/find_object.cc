// Implementation of find_object.h — see the header for the backend and
// model contracts. The dual-backend ONNX session machinery (ORT
// delay-load-probed + cv::dnn fallback, runtime-DLL probes, ONNX
// header shape probe) lives in the shared
// src/content/ai/onnx_session helper — this TU keeps only the
// detection-specific parts: BGRA->letterboxed NCHW preprocessing, the
// YOLO/DETR output decoders, per-class NMS and the Detector facade.
#include "find_object.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <system_error>
#include <vector>

#include "content/ai/onnx_session/onnx_session.h"
#include "video/frame.h"

namespace XER::AI::FindObject {
namespace {

namespace Onnx = XER::AI::Onnx;

// ---- preprocessing ----------------------------------------------------

// Letterbox + BGRA->RGB + /255 into a NCHW f32 buffer sized
// 3*in_w*in_h. Returns the letterbox scale and x/y padding in input
// pixels so detections map back to source pixels.
struct Letterbox {
    float scale = 1.0f;   // source -> input scale
    float pad_x = 0.0f;
    float pad_y = 0.0f;
};

// out must already be sized 3*in_w*in_h. Fills with 114/255 (YOLO's
// letterbox gray), then writes the scaled image centered.
Letterbox BgraToNchw(const std::uint8_t* bgra, int w, int h, int stride,
                     int in_w, int in_h, float* out) {
    Letterbox lb;
    const float kPad = 114.0f / 255.0f;
    std::fill(out, out + static_cast<std::size_t>(3) * in_w * in_h, kPad);
    lb.scale = std::min(static_cast<float>(in_w) / w,
                        static_cast<float>(in_h) / h);
    const int rw = std::max(1, static_cast<int>(std::lround(w * lb.scale)));
    const int rh = std::max(1, static_cast<int>(std::lround(h * lb.scale)));
    lb.pad_x = static_cast<float>(in_w - rw) * 0.5f;
    lb.pad_y = static_cast<float>(in_h - rh) * 0.5f;
    const int ox = static_cast<int>(std::lround(lb.pad_x));
    const int oy = static_cast<int>(std::lround(lb.pad_y));
    // Bilinear resize straight into the NCHW planes — the module has no
    // SIMD helpers, and detection is a background (AsyncOp) feature, so
    // a scalar resample is plenty.
    for (int y = 0; y < rh; ++y) {
        const float sy = (y + 0.5f) / lb.scale - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, h - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float fy = std::clamp(sy - y0, 0.0f, 1.0f);
        for (int x = 0; x < rw; ++x) {
            const float sx = (x + 0.5f) / lb.scale - 0.5f;
            const int x0 =
                std::clamp(static_cast<int>(std::floor(sx)), 0, w - 1);
            const int x1 = std::min(x0 + 1, w - 1);
            const float fx = std::clamp(sx - x0, 0.0f, 1.0f);
            const std::uint8_t* p00 = bgra + y0 * stride + x0 * 4;
            const std::uint8_t* p10 = bgra + y0 * stride + x1 * 4;
            const std::uint8_t* p01 = bgra + y1 * stride + x0 * 4;
            const std::uint8_t* p11 = bgra + y1 * stride + x1 * 4;
            // p[0]=B p[1]=G p[2]=R
            for (int c = 0; c < 3; ++c) {
                const int ch = 2 - c;   // out is R,G,B
                const float v =
                    (p00[ch] * (1.0f - fx) + p10[ch] * fx) * (1.0f - fy) +
                    (p01[ch] * (1.0f - fx) + p11[ch] * fx) * fy;
                out[static_cast<std::size_t>(c) * in_w * in_h +
                    (oy + y) * in_w + (ox + x)] = v / 255.0f;
            }
        }
    }
    return lb;
}

// ---- output decode + NMS ----------------------------------------------

struct RawBox {
    float cx, cy, w, h;
    int class_id;
    float score;
};

using TensorView = Onnx::TensorView;

// YOLO v8-style decode: output dims [1,A,N] or [1,N,A] (a leading batch
// dim of 1 may also be absent). A = 4 + C. Boxes are cx,cy,w,h in
// INPUT pixels; scores are per-class maxes.
std::vector<RawBox> DecodeYolo(const float* data,
                               const std::vector<std::int64_t>& dims,
                               float conf_thresh) {
    std::vector<RawBox> boxes;
    if (data == nullptr || dims.size() < 2 || dims.size() > 3) {
        return boxes;
    }
    std::int64_t a = dims[dims.size() - 2];
    std::int64_t n = dims[dims.size() - 1];
    bool attr_first = true;   // [1,A,N]
    if (a > n) {
        // [1,N,A] — rows are boxes, columns attrs.
        std::swap(a, n);
        attr_first = false;
    }
    if (a < 5 || n < 1) return boxes;
    const std::int64_t classes = a - 4;
    const auto at = [&](std::int64_t attr, std::int64_t i) -> float {
        return attr_first ? data[attr * n + i] : data[i * a + attr];
    };
    boxes.reserve(static_cast<std::size_t>(std::min<std::int64_t>(n, 4096)));
    for (std::int64_t i = 0; i < n; ++i) {
        const float px = at(0, i), py = at(1, i);
        const float w = at(2, i), h = at(3, i);
        if (!(w > 0.0f) || !(h > 0.0f)) continue;   // NaN-safe reject
        int best = -1;
        float best_score = conf_thresh;
        for (std::int64_t c = 0; c < classes; ++c) {
            const float s = at(4 + c, i);
            if (s > best_score) {
                best_score = s;
                best = static_cast<int>(c);
            }
        }
        if (best < 0) continue;
        boxes.push_back({px, py, w, h, best, best_score});
    }
    return boxes;
}

// DETR-family decode (RF-DETR / RT-DETR exports, e.g.
// onnx-community/rfdetr_*-ONNX): TWO outputs instead of YOLO's one —
// a boxes tensor [1,N,4] in cx,cy,w,h and a class-logits tensor
// [1,N,C]. Boxes may be normalized to 0..1 (RF-DETR) or already in
// input pixels (some RT-DETR exports) — detected by range. Logits are
// un-activated; scores are per-class sigmoids. DETR is NMS-free by
// design but the shared NMS tail is harmless (near-zero duplicate
// overlap) and stays enabled.
//
// Pairing rule: the output whose last dim == 4 is boxes, the one with
// last dim > 4 is logits; when names are present they break ties /
// confirm ("box"/"det" vs "logit"/"label"/"score"). Returns empty on
// ambiguity — better a silent miss than garbage boxes.
std::vector<RawBox> DecodeDetr(const std::vector<TensorView>& outs,
                                 float conf_thresh, int input_size) {
    const TensorView* boxes = nullptr;
    const TensorView* logits = nullptr;
    const auto last_dim = [](const TensorView& t) -> std::int64_t {
        return t.dims.empty() ? -1 : t.dims.back();
    };
    const auto rank = [](const TensorView& t) -> std::size_t {
        return t.dims.size();
    };
    const auto name_has = [](const TensorView& t, const char* sub) {
        std::string low = t.name;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return low.find(sub) != std::string::npos;
    };
    for (const TensorView& t : outs) {
        const std::int64_t last = last_dim(t);
        const std::size_t r = rank(t);
        if (t.data == nullptr || (r != 2 && r != 3)) continue;
        if (last == 4) {
            if (boxes == nullptr) boxes = &t;
        } else if (last > 4) {
            if (logits == nullptr || name_has(t, "logit") ||
                name_has(t, "label") || name_has(t, "score") ||
                name_has(t, "cls")) {
                logits = &t;
            }
        }
    }
    // Name-guided disambiguation for the pathological C == 4 case
    // (Roboflow warns shape-matching swaps outputs when num_classes+1
    // == 4): prefer explicit name matches when both candidates exist.
    for (const TensorView& t : outs) {
        if (name_has(t, "logit") || name_has(t, "label") ||
            name_has(t, "score")) {
            logits = &t;
        } else if ((name_has(t, "box") || name_has(t, "det")) &&
                   last_dim(t) == 4) {
            boxes = &t;
        }
    }
    if (boxes == nullptr || logits == nullptr || boxes == logits) {
        return {};
    }
    const std::int64_t n = boxes->dims[rank(*boxes) - 2];
    const std::int64_t classes =
        logits->dims[rank(*logits) - 1];
    const std::int64_t n_logits =
        rank(*logits) >= 2 ? logits->dims[rank(*logits) - 2] : 0;
    if (n < 1 || classes < 1 || n_logits != n ||
        boxes->data == nullptr || logits->data == nullptr) {
        return {};
    }
    // Normalized vs input-pixel boxes: normalized cxcywh stays in
    // [0,~1.05]; anything past ~2 input px is already pixels.
    float max_coord = 0.0f;
    for (std::int64_t i = 0; i < n * 4; ++i) {
        const float v = std::fabs(boxes->data[i]);
        if (v > max_coord) max_coord = v;
    }
    const float box_scale =
        max_coord <= 2.0f ? static_cast<float>(input_size) : 1.0f;

    const auto sigmoid = [](float x) {
        return 1.0f / (1.0f + std::exp(-x));
    };
    std::vector<RawBox> out;
    out.reserve(static_cast<std::size_t>(std::min<std::int64_t>(n, 4096)));
    for (std::int64_t i = 0; i < n; ++i) {
        const float* row = logits->data + i * classes;
        int best = -1;
        float best_score = conf_thresh;
        for (std::int64_t c = 0; c < classes; ++c) {
            const float s = sigmoid(row[c]);
            if (s > best_score) {
                best_score = s;
                best = static_cast<int>(c);
            }
        }
        if (best < 0) continue;
        const float cx = boxes->data[i * 4 + 0] * box_scale;
        const float cy = boxes->data[i * 4 + 1] * box_scale;
        const float w = boxes->data[i * 4 + 2] * box_scale;
        const float h = boxes->data[i * 4 + 3] * box_scale;
        if (!(w > 0.0f) || !(h > 0.0f)) continue;
        out.push_back({cx, cy, w, h, best, best_score});
    }
    return out;
}

float IoU(const Detection& a, const Detection& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w);
    const float y1 = std::min(a.y + a.h, b.y + b.h);
    const float inter = std::max(0.0f, x1 - x0) * std::max(0.0f, y1 - y0);
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

// Shared tail: un-letterbox, clamp to source, per-class NMS, label.
std::vector<Detection> Postprocess(std::vector<RawBox> boxes,
                                   const Letterbox& lb, int src_w,
                                   int src_h,
                                   const std::vector<std::string>& labels,
                                   const Options& options) {
    std::vector<Detection> dets;
    dets.reserve(boxes.size());
    for (const RawBox& b : boxes) {
        Detection d;
        d.class_id = b.class_id;
        d.confidence = b.score;
        const float x0 = (b.cx - b.w * 0.5f - lb.pad_x) / lb.scale;
        const float y0 = (b.cy - b.h * 0.5f - lb.pad_y) / lb.scale;
        const float x1 = (b.cx + b.w * 0.5f - lb.pad_x) / lb.scale;
        const float y1 = (b.cy + b.h * 0.5f - lb.pad_y) / lb.scale;
        d.x = std::clamp(x0, 0.0f, static_cast<float>(src_w));
        d.y = std::clamp(y0, 0.0f, static_cast<float>(src_h));
        d.w = std::clamp(x1, 0.0f, static_cast<float>(src_w)) - d.x;
        d.h = std::clamp(y1, 0.0f, static_cast<float>(src_h)) - d.y;
        if (d.w <= 0.0f || d.h <= 0.0f) continue;
        d.label = b.class_id >= 0 &&
                          b.class_id < static_cast<int>(labels.size())
                      ? labels[static_cast<std::size_t>(b.class_id)]
                      : "class_" + std::to_string(b.class_id);
        dets.push_back(std::move(d));
    }
    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });
    if (options.nms_threshold > 0.0f) {
        std::vector<Detection> kept;
        kept.reserve(dets.size());
        std::vector<bool> dead(dets.size(), false);
        for (std::size_t i = 0; i < dets.size(); ++i) {
            if (dead[i]) continue;
            kept.push_back(dets[i]);
            for (std::size_t j = i + 1; j < dets.size(); ++j) {
                if (!dead[j] &&
                    dets[j].class_id == dets[i].class_id &&
                    IoU(dets[i], dets[j]) > options.nms_threshold) {
                    dead[j] = true;
                }
            }
        }
        dets.swap(kept);
    }
    if (options.max_detections > 0 &&
        dets.size() > static_cast<std::size_t>(options.max_detections)) {
        dets.resize(static_cast<std::size_t>(options.max_detections));
    }
    return dets;
}

}  // namespace

std::vector<BackendInfo> Backends() {
    std::vector<BackendInfo> rows;
    for (const Onnx::BackendInfo& ob : Onnx::Backends()) {
        BackendInfo b;
        b.id = ob.id;
        b.linked = ob.linked;
        b.loadable = ob.loadable;
        b.detail = ob.detail;
        rows.push_back(std::move(b));
    }
    return rows;
}

std::string BackendReport() {
    std::string out = "FindObject backends (YA.AIFindObject):\n";
    for (const BackendInfo& b : Backends()) {
        out += "  ";
        out += b.id;
        for (std::size_t n = std::strlen(b.id); n < 14; ++n) out += ' ';
        if (!b.linked) {
            out += ": not linked";
        } else {
            out += (b.loadable ? ": available" : ": linked, unloadable");
        }
        if (!b.detail.empty()) {
            out += " — ";
            out += b.detail;
        }
        out += '\n';
    }
    return out;
}

std::string ActiveBackendId() { return Onnx::ActiveBackendId(); }

std::vector<std::string> LoadLabels(const std::string& utf8_path) {
    std::vector<std::string> labels;
    std::ifstream in{std::filesystem::path(utf8_path)};
    if (!in) return labels;
    std::string line;
    while (std::getline(in, line)) {
        const auto b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos || line[b] == '#') continue;
        const auto e = line.find_last_not_of(" \t\r\n");
        labels.push_back(line.substr(b, e - b + 1));
    }
    return labels;
}

struct Detector::Impl {
    Onnx::Session session;
    std::vector<std::string> labels;
    int input_size = 0;
    std::vector<float> nchw;
    bool ready = false;
};

Detector::Detector() : impl_(std::make_unique<Impl>()) {}
Detector::~Detector() = default;

bool Detector::Load(const std::string& utf8_model_path,
                    const std::string& utf8_labels_path,
                    const std::string& backend, std::string* error_out) {
    impl_->session = Onnx::Session{};
    impl_->ready = false;
    impl_->input_size = 0;
    impl_->labels.clear();

    if (!impl_->session.Load(utf8_model_path, backend, error_out)) {
        return false;
    }

    // Input square: backend-reported static dims first (ORT sees them
    // when the export pins H=W; cv::dnn never reports dims), then the
    // ONNX header probe; when the model itself declares dynamic dims,
    // fall back per family: multi-output DETR exports feed their
    // trained resolution (RF-DETR-N 384), single-output YOLO exports
    // keep the classic 640.
    const std::vector<std::int64_t> d0 = impl_->session.InputDims(0);
    if (d0.size() == 4 && d0[2] == d0[3] && d0[2] > 0) {
        impl_->input_size = static_cast<int>(d0[2]);
    }
    if (impl_->input_size <= 0) {
        const int probed = Onnx::ProbeInputSquare(utf8_model_path);
        impl_->input_size =
            probed > 0
                ? probed
                : (impl_->session.OutputCount() >= 2 ? 384 : 640);
    }
    if (!utf8_labels_path.empty()) {
        impl_->labels = LoadLabels(utf8_labels_path);
    }
    impl_->nchw.assign(static_cast<std::size_t>(3) * impl_->input_size *
                           impl_->input_size,
                       0.0f);
    impl_->ready = true;
    return true;
}

bool Detector::IsLoaded() const { return impl_->ready; }
const std::string& Detector::BackendId() const {
    return impl_->session.BackendId();
}
int Detector::InputSize() const { return impl_->input_size; }

std::vector<Detection> Detector::Detect(const std::uint8_t* bgra,
                                        int width, int height,
                                        int stride,
                                        const Options& options) {
    if (!impl_->ready || !impl_->session.IsLoaded() ||
        bgra == nullptr ||
        width <= 0 || height <= 0 || stride < width * 4) {
        return {};
    }
    const int in = impl_->input_size;
    const Letterbox lb =
        BgraToNchw(bgra, width, height, stride, in, in,
                   impl_->nchw.data());
    Onnx::Input input;
    input.dims = {1, 3, in, in};   // empty name binds positionally
    input.data = impl_->nchw.data();
    std::vector<TensorView> outs;
    std::string err;
    if (!impl_->session.Run({input}, &outs, &err)) {
        return {};
    }
    std::vector<RawBox> boxes;
    if (outs.size() >= 2) {
        // DETR-family export (RF-DETR / RT-DETR): separate boxes +
        // logits tensors, no NMS in graph.
        boxes = DecodeDetr(outs, options.confidence_threshold, in);
    }
    if (boxes.empty() && !outs.empty()) {
        // YOLO-style single output (or unrecognized layout — decode
        // output 0 anyway; a non-YOLO shape just yields nothing).
        boxes = DecodeYolo(outs[0].data, outs[0].dims,
                           options.confidence_threshold);
    }
    return Postprocess(std::move(boxes), lb, width, height,
                       impl_->labels, options);
}

std::vector<Detection> Detector::Detect(const video::Frame& frame,
                                        const Options& options) {
    if (frame.Format() != video::PixelFormat::BGRA8 ||
        !frame.IsValid()) {
        return {};
    }
    return Detect(frame.Data(), frame.Width(), frame.Height(),
                  frame.Width() * 4, options);
}

}  // namespace XER::AI::FindObject
