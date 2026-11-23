// Implementation of ai/face_detect.h — YuNet on the shared ONNX
// session. Decode + NMS mirror cv::FaceDetectorYN (opencv
// modules/objdetect/src/face_detect.cpp) verbatim so the staged
// export behaves identically to the zoo demo:
//   score   = sqrt(clamp(cls,0,1) * clamp(obj,0,1))
//   center  = (anchor + bbox[0..1]) * stride
//   size    = exp(bbox[2..3]) * stride   -> x1/y1 = center - size/2
//   kps     = (kps + anchor) * stride    (5 pts: re, le, nose, rm, lm)
//   NMS     = greedy IoU, score order, top_k cap
// The fixed 640² export gets the probed frame plain-stretched to the
// model square and boxes mapped back (the same downscale-then-map
// recipe ScanClipDetections already uses for its 640px probes).
#include "ai/face_detect.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "content/ai/onnx_session/onnx_session.h"
#include "lab/ai_ops.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace Onnx = XER::AI::Onnx;

// BGRA8 -> NCHW f32, plain-stretch bilinear resize to the model
// square, channel order B,G,R, raw 0..255 (blobFromImage semantics —
// the YuNet export carries no normalization).
void BgraStretchToYunet(const std::uint8_t* bgra, int w, int h,
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
            for (int c = 0; c < 3; ++c) {   // 0=B,1=G,2=R — blob order
                const float v =
                    (p00[c] * (1.0f - fx) + p10[c] * fx) *
                        (1.0f - fy) +
                    (p01[c] * (1.0f - fx) + p11[c] * fx) * fy;
                out[c * n + y * size + x] = v;
            }
        }
    }
}

float IoU(const FaceBox& a, const FaceBox& b) {
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    const float iw = std::max(0.0f, x2 - x1);
    const float ih = std::max(0.0f, y2 - y1);
    const float inter = iw * ih;
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

// Finds the output tensor whose name ENDS with `suffix` (e.g. "_8")
// and STARTS with `prefix` ("cls") — the 12 YuNet heads are named
// cls_8/obj_16/bbox_32/... so exact-suffix matching resists any
// export that pads names.
const Onnx::TensorView* Head(
    const std::vector<Onnx::TensorView>& outs, const char* prefix,
    const char* suffix) {
    const std::string pre = prefix;
    const std::string suf = suffix;
    for (const Onnx::TensorView& t : outs) {
        if (t.name.size() < pre.size() + suf.size()) continue;
        if (t.name.compare(0, pre.size(), pre) != 0) continue;
        if (t.name.compare(t.name.size() - suf.size(), suf.size(),
                           suf) != 0) {
            continue;
        }
        return &t;
    }
    return nullptr;
}

}  // namespace

std::string FaceDetectModel() { return Lab::FindAiModel(kYunetFile); }

const char* FaceDetectModelHint() {
    return "model not installed — put face_detection_yunet.onnx in "
           "assets/models/ next to the exe, or point "
           "XER_AI_MODELS_DIR at its folder";
}

std::vector<FaceBox> NmsFaces(std::vector<FaceBox> boxes,
                              float nms_threshold, int top_k) {
    std::sort(boxes.begin(), boxes.end(),
              [](const FaceBox& a, const FaceBox& b) {
                  return a.score > b.score;
              });
    const float thr = std::clamp(nms_threshold, 0.0f, 1.0f);
    std::vector<FaceBox> keep;
    keep.reserve(boxes.size());
    for (const FaceBox& b : boxes) {
        if (top_k > 0 &&
            static_cast<int>(keep.size()) >= top_k) {
            break;
        }
        bool suppressed = false;
        for (const FaceBox& k : keep) {
            if (IoU(b, k) > thr) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) keep.push_back(b);
    }
    return keep;
}

std::vector<FaceBox> DecodeYunet(const YunetHeadView* heads,
                                 std::size_t n_heads, int in_w,
                                 int in_h,
                                 const FaceDetectParams& params) {
    std::vector<FaceBox> all;
    if (heads == nullptr || in_w <= 0 || in_h <= 0) return all;
    const float thr = std::clamp(params.score_threshold, 0.0f, 1.0f);
    for (std::size_t hi = 0; hi < n_heads; ++hi) {
        const YunetHeadView& head = heads[hi];
        if (head.stride <= 0 || head.cls == nullptr ||
            head.obj == nullptr || head.bbox == nullptr) {
            continue;
        }
        const int cols = in_w / head.stride;
        const int rows = in_h / head.stride;
        if (cols <= 0 || rows <= 0) continue;
        const std::size_t grid =
            static_cast<std::size_t>(cols) * rows;
        const std::size_t count = std::min(head.count, grid);
        for (std::size_t idx = 0; idx < count; ++idx) {
            const float cls =
                std::clamp(head.cls[idx], 0.0f, 1.0f);
            const float obj =
                std::clamp(head.obj[idx], 0.0f, 1.0f);
            const float score = std::sqrt(cls * obj);
            if (score < thr) continue;
            const int r = static_cast<int>(idx / cols);
            const int c = static_cast<int>(idx % cols);
            const float* b = head.bbox + idx * 4;
            FaceBox f;
            const float cx = (c + b[0]) * head.stride;
            const float cy = (r + b[1]) * head.stride;
            f.w = std::exp(b[2]) * head.stride;
            f.h = std::exp(b[3]) * head.stride;
            f.x = cx - f.w * 0.5f;
            f.y = cy - f.h * 0.5f;
            f.score = score;
            if (head.kps != nullptr) {
                const float* k = head.kps + idx * 10;
                for (int n = 0; n < 5; ++n) {
                    f.landmarks[2 * n] =
                        (k[2 * n] + c) * head.stride;
                    f.landmarks[2 * n + 1] =
                        (k[2 * n + 1] + r) * head.stride;
                }
            }
            all.push_back(f);
        }
    }
    return NmsFaces(std::move(all), params.nms_threshold,
                    params.top_k);
}

FaceDetector::FaceDetector() = default;
FaceDetector::~FaceDetector() = default;

bool FaceDetector::Load(const std::string& model_path,
                        std::string* error_out) {
    session_.reset();
    if (model_path.empty()) {
        if (error_out) *error_out = FaceDetectModelHint();
        return false;
    }
    auto session = std::make_unique<Onnx::Session>();
    std::string err;
    if (!session->Load(model_path, "", &err)) {
        if (error_out) {
            *error_out = "yunet load failed: " +
                         (err.empty()
                              ? std::string("rejected by every ONNX "
                                            "backend")
                              : err);
        }
        return false;
    }
    session_ = std::move(session);
    nchw_.assign(static_cast<std::size_t>(3) * kYunetInputSize *
                     kYunetInputSize,
                 0.0f);
    return true;
}

bool FaceDetector::IsLoaded() const {
    return session_ != nullptr && session_->IsLoaded();
}

const std::string& FaceDetector::BackendId() const {
    static const std::string kEmpty;
    return session_ != nullptr ? session_->BackendId() : kEmpty;
}

std::vector<FaceBox> FaceDetector::Detect(
    const std::uint8_t* bgra, int w, int h, int stride,
    const FaceDetectParams& params) {
    if (!IsLoaded() || bgra == nullptr || w <= 0 || h <= 0 ||
        stride < w * 4) {
        return {};
    }
    BgraStretchToYunet(bgra, w, h, stride, kYunetInputSize,
                       nchw_.data());
    Onnx::Input input;
    input.name = "input";
    input.dims = {1, 3, kYunetInputSize, kYunetInputSize};
    input.data = nchw_.data();
    std::vector<Onnx::TensorView> outs;
    std::string err;
    if (!session_->Run({input}, &outs, &err)) {
        return {};
    }
    // Assemble the three head views by output-name suffix; anchors
    // per stride = (640/stride)^2 (6400 / 1600 / 400).
    static const int kStrides[3] = {8, 16, 32};
    YunetHeadView heads[3];
    for (int i = 0; i < 3; ++i) {
        const std::string suf =
            "_" + std::to_string(kStrides[i]);
        const Onnx::TensorView* cls = Head(outs, "cls", suf.c_str());
        const Onnx::TensorView* obj = Head(outs, "obj", suf.c_str());
        const Onnx::TensorView* bbox =
            Head(outs, "bbox", suf.c_str());
        const Onnx::TensorView* kps = Head(outs, "kps", suf.c_str());
        heads[i].stride = kStrides[i];
        heads[i].cls = cls != nullptr ? cls->data : nullptr;
        heads[i].obj = obj != nullptr ? obj->data : nullptr;
        heads[i].bbox = bbox != nullptr ? bbox->data : nullptr;
        heads[i].kps = kps != nullptr ? kps->data : nullptr;
        heads[i].count =
            cls != nullptr ? cls->ElementCount() : 0;
    }
    std::vector<FaceBox> boxes =
        DecodeYunet(heads, 3, kYunetInputSize, kYunetInputSize,
                    params);
    // Input-space pixels -> probed-frame pixels (the stretch
    // inverse).
    const float sx = static_cast<float>(w) / kYunetInputSize;
    const float sy = static_cast<float>(h) / kYunetInputSize;
    for (FaceBox& f : boxes) {
        f.x *= sx;
        f.w *= sx;
        f.y *= sy;
        f.h *= sy;
        for (int n = 0; n < 5; ++n) {
            f.landmarks[2 * n] *= sx;
            f.landmarks[2 * n + 1] *= sy;
        }
    }
    return boxes;
}

bool ScanClipFaces(const Clip& clip, const std::string& model_path,
                   const ReframeScanParams& scan_params,
                   const FaceDetectParams& detect_params,
                   const std::function<void(float)>& progress,
                   const std::function<bool()>& should_cancel,
                   ClipTrack* out, std::string* error_out) {
    if (out == nullptr) return false;
    *out = ClipTrack{};
    if (clip.source_path.empty() || clip.is_adjustment) {
        if (error_out) *error_out = "clip has no media to scan";
        return false;
    }
    if (model_path.empty()) {
        if (error_out) *error_out = FaceDetectModelHint();
        return false;
    }
    FaceDetector detector;
    std::string err;
    if (!detector.Load(model_path, &err)) {
        if (error_out) *error_out = err;
        return false;
    }
    ClipReframe samples;
    const bool ok = ScanClipFrames(
        clip, scan_params,
        [&](const std::uint8_t* bgra, int w, int h,
            int stride) -> std::vector<TrackDetection> {
            const auto faces =
                detector.Detect(bgra, w, h, stride, detect_params);
            std::vector<TrackDetection> dets;
            dets.reserve(faces.size());
            for (const FaceBox& f : faces) {
                TrackDetection td;
                td.x = f.x;
                td.y = f.y;
                td.w = f.w;
                td.h = f.h;
                td.confidence = f.score;
                td.class_id = 0;
                td.label = "face";
                dets.push_back(td);
            }
            return dets;
        },
        // Primary face per probe: largest box wins (single-track —
        // multi-face storage is the open question on the research
        // page; a tracked box that jumps between faces is bridged by
        // the same smoothing the object track uses).
        [](const std::vector<TrackDetection>& dets)
            -> const TrackDetection* {
            const TrackDetection* best = nullptr;
            double best_area = -1.0;
            for (const TrackDetection& d : dets) {
                const double area =
                    static_cast<double>(d.w) * d.h;
                if (area > best_area) {
                    best_area = area;
                    best = &d;
                }
            }
            return best;
        },
        progress, should_cancel, &samples, error_out);
    if (!ok) {
        return false;
    }
    out->samples = std::move(samples.samples);
    if (!out->samples.empty()) {
        out->label = "face";
        out->classes = {"face"};
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
