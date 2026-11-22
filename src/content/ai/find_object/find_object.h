// YA.AIFindObject — object detection over BGRA video frames.
//
// Backend contract:
//   * ONNX Runtime (XER_AI_FO_ONNXRUNTIME at build time) — ORT C API
//     (onnxruntime_c_api.h), CPU execution provider. MSYS2 ships no
//     DirectML EP (dml_provider_factory.h absent), so CPU is the only
//     provider this build can use. The onnxruntime.dll import is
//     delay-loaded and LoadLibrary-probed before first use — a missing
//     or unloadable DLL (broken dependency closure) reports
//     `loadable:false`, it never reaches the delay-load binder.
//   * OpenCV DNN (XER_AI_FO_OPENCV) — cv::dnn::readNetFromONNX; works
//     with just an .onnx file, no ORT needed. Same Detect contract.
//   Backend pick order is ORT first, OpenCV DNN second; Detector::Load
//   accepts an explicit backend id to override.
//
// Model contract — two detector families are decoded:
//   1. YOLO-style single-shot exports (YOLOv8n / YOLO11n reference):
//      input  [1,3,H,W] float32 NCHW, RGB, /255, letterboxed
//      output [1,4+C,N] or [1,N,4+C] — cx,cy,w,h in input pixels +
//             C per-class scores (v8-style, no objectness column).
//   2. DETR-style two-output exports (RF-DETR / RT-DETR; the shipped
//      onnx-community/rfdetr_nano-ONNX is the reference):
//      input  same NCHW contract (RF-DETR needs no mean/std
//             normalization — /255 only, matching this module)
//      output boxes  [1,N,4]  — cx,cy,w,h, normalized 0..1 or input
//             pixels (auto-detected by range)
//             logits [1,N,C]  — un-activated; per-class sigmoid, argmax
//      Picked whenever a session reports >= 2 outputs.
// Input size resolution order: backend-reported static dims ->
// ProbeOnnxInputSquare (ONNX header protobuf walk) -> family default
// (384 for multi-output DETR, 640 for YOLO).
// Class labels come from a coco.names-style file (one label per line,
// '#' comment lines ignored) — pass it to Detector::Load; absent
// labels report as "class_<id>". For COCO-trained DETR exports the
// logit index IS the COCO id (1..90, gaps included), so the names
// file must be id-indexed (see assets/models/rfdetr_nano.names).
// Model distribution is out of scope: the caller resolves the path
// (tools/video_editor/ai_tags.h looks in <exe>/assets/models/).
//
// NMS is classic per-class IoU suppression implemented here — no
// extra dependencies. The whole module is thread-hostile like the
// runtimes it wraps: one Detector per thread, no shared state.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace video {
class Frame;  // src/video/frame.h — Detect overload only, no link dep
}

namespace XER::AI::FindObject {

// One detection in SOURCE-frame pixel coordinates (un-letterboxed).
struct Detection {
    std::string label;
    int class_id = -1;
    float confidence = 0.0f;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct Options {
    float confidence_threshold = 0.25f;
    float nms_threshold = 0.45f;   // IoU; <= 0 disables suppression
    int max_detections = 300;      // post-NMS cap
};

// One row of the backend matrix. `linked` = the SDK was present at
// build time; `loadable` = the runtime DLL(s) probed OK just now (on
// non-Windows targets loadable == linked). `detail` carries the wiring
// story or the failure reason.
struct BackendInfo {
    const char* id;   // "onnxruntime" | "opencv_dnn"
    bool linked = false;
    bool loadable = false;
    std::string detail;
};

// Full backend matrix — always lists both backends. Cheap; probes
// LoadLibrary once and caches the verdict.
std::vector<BackendInfo> Backends();
// Human-readable "  <id>  <status> — <detail>" lines, newline
// terminated — safe to print from smoke checks / --diag.
std::string BackendReport();
// First loadable backend id ("onnxruntime" preferred), "" when none.
std::string ActiveBackendId();

// coco.names-style label file: one label per line, blank lines and
// '#'-comment lines skipped. Missing/unreadable file -> empty vector.
std::vector<std::string> LoadLabels(const std::string& utf8_path);

class Detector {
public:
    Detector();
    ~Detector();
    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;

    // Loads `utf8_model_path` on `backend` ("" = first loadable, ORT
    // preferred). `utf8_labels_path` may be empty. Returns false with
    // error_out set when no backend is loadable, the model file is
    // missing/unreadable, or the runtime rejects it.
    bool Load(const std::string& utf8_model_path,
              const std::string& utf8_labels_path = "",
              const std::string& backend = "",
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;  // "" until Load succeeds
    // Model input square edge actually fed (e.g. 640); 0 until Load.
    int InputSize() const;

    // Detect on a packed BGRA8 buffer (B,G,R,A byte order). `stride`
    // is bytes per row (>= width*4). Empty vector when not loaded or
    // the buffer is invalid — check IsLoaded() and error paths via
    // Load(); a run-time inference failure returns empty too (the
    // contract is "no crash", not "always succeeds").
    std::vector<Detection> Detect(const std::uint8_t* bgra, int width,
                                  int height, int stride,
                                  const Options& options = {});
    // Same call for video::Frame — BGRA8 only (other formats return
    // empty). Header-declared; Frame needs no link dep (inline API).
    std::vector<Detection> Detect(const video::Frame& frame,
                                  const Options& options = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::AI::FindObject
