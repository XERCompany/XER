// YuNet face detection for the VideoEditor — the auto face-blur
// feature (docs/wiki/research/2026-11-23-ai-features-wave2.md §5).
// Runs the staged face_detection_yunet.onnx on the shared
// XER::AI::Onnx session and feeds its boxes into the SAME track /
// mosaic machinery as object tracking: ScanClipFaces lands a
// ClipTrack labeled "face" on Clip::object_track, which the
// builtin:xer.fx.face_blur / xer.fx.track_mosaic effect pixelates
// live (no fx changes needed — the tracked box resolves at the
// frame's source time in the compositor).
//
// Contract (verified 2026-11-23 against opencv_zoo
// face_detection_yunet_2023mar.onnx, fp32, 233 KB —
// assets/models/README.md): the export is FIXED [1,3,640,640] BGR
// NCHW (raw 0..255, no mean/std — dnn::blobFromImage semantics), so
// probes are stretched to 640² and boxes mapped back. Twelve outputs
// across strides 8/16/32: cls_s/obj_s [1,N,1] (already activated),
// bbox_s [1,N,4] (anchor-relative), kps_s [1,N,10] (5 landmarks).
// Decode mirrors cv::FaceDetectorYN exactly (score =
// sqrt(cls*obj), box = anchor + exp(bbox)*stride, kps =
// anchor-relative) — see face_detect.cc.
//
// Pure worker/model code like the rest of ai/ — the FaceDetector
// session is thread-hostile; scan on the worker, never the UI
// thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ai/smart_reframe.h"
#include "timeline_model.h"

namespace XER::AI::Onnx {
class Session;
}

namespace XER::VideoEditor::Ai {

inline constexpr const char* kYunetFile =
    "face_detection_yunet.onnx";
// The staged export's fixed square input edge.
inline constexpr int kYunetInputSize = 640;

// Effect id registered in builtin_fx.cc. "Face Blur" renders through
// the same compositor branch as xer.fx.track_mosaic — the tracked
// box comes from Clip::object_track, which ScanClipFaces fills with
// label "face". Distinct id so the effects panel lists the
// privacy-named entry and the clip's effect stack self-documents.
inline constexpr const char* kFaceBlurId = "xer.fx.face_blur";

// Resolves the staged model — empty when face_detection_yunet.onnx
// is not installed (XER_AI_MODELS_DIR / assets/models / models).
std::string FaceDetectModel();
const char* FaceDetectModelHint();

struct FaceDetectParams {
    float score_threshold = 0.6f;  // sqrt(cls*obj) — opencv default .9
    float nms_threshold = 0.3f;    // IoU — opencv default .3
    int top_k = 64;                // NMS keep cap (opencv feeds 5000)
};

// One decoded face: box in probed-frame PIXELS + score + the five
// landmarks YuNet reports (right eye, left eye, nose tip, right and
// left mouth corners — the future faces-tab keypoints).
struct FaceBox {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float score = 0.0f;
    float landmarks[10] = {};
};

// ---- decode / NMS (session-free, smoke-testable) --------------------
// Borrowed views over one stride's raw output tensors. `count` =
// anchors on the stride = (in_w/stride)*(in_h/stride); cls/obj hold
// `count` values, bbox count*4, kps count*10 (nullable — landmarks
// zero-fill then).
struct YunetHeadView {
    int stride = 0;
    const float* cls = nullptr;
    const float* obj = nullptr;
    const float* bbox = nullptr;
    const float* kps = nullptr;
    std::size_t count = 0;
};

// Decodes every head into input-space boxes and runs greedy IoU NMS.
// Boxes are in the (stretched) input pixels — callers map back to
// source pixels. Thresholds from `params`.
std::vector<FaceBox> DecodeYunet(const YunetHeadView* heads,
                                 std::size_t n_heads, int in_w,
                                 int in_h,
                                 const FaceDetectParams& params);

// Greedy IoU NMS over decoded boxes (score-descending), capped at
// top_k keeps. Free-standing so the smoke can exercise it without a
// model.
std::vector<FaceBox> NmsFaces(std::vector<FaceBox> boxes,
                              float nms_threshold, int top_k);

// One detector = one ONNX session (thread-hostile). Load()s the
// staged YuNet export; Detect()s BGRA8 frames of arbitrary size —
// internally stretched to kYunetInputSize square and mapped back.
class FaceDetector {
public:
    FaceDetector();
    ~FaceDetector();
    FaceDetector(const FaceDetector&) = delete;
    FaceDetector& operator=(const FaceDetector&) = delete;

    bool Load(const std::string& model_path,
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;

    // Empty vector when not loaded or the frame is degenerate —
    // matches FindObj::Detector::Detect's unloaded contract.
    std::vector<FaceBox> Detect(const std::uint8_t* bgra, int w,
                                int h, int stride,
                                const FaceDetectParams& params);

private:
    std::unique_ptr<XER::AI::Onnx::Session> session_;
    std::vector<float> nchw_;    // 3*640*640 scratch
};

// Scans the clip's consumed source span through the shared
// detector-agnostic frame scan (ai/smart_reframe.h ScanClipFrames)
// with this detector bound; each probe records the LARGEST face as
// the track sample (single-track limitation — multi-face storage is
// the open question in the research page). `out` lands samples +
// label "face" + classes {"face"} when anything was picked. Same
// error contract as ScanClipTrack (ai/object_track.h).
bool ScanClipFaces(const Clip& clip, const std::string& model_path,
                   const ReframeScanParams& scan_params,
                   const FaceDetectParams& detect_params,
                   const std::function<void(float)>& progress,
                   const std::function<bool()>& should_cancel,
                   ClipTrack* out, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
