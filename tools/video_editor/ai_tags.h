// AI "Find Objects" clip tagging for the VideoEditor — the
// "AI.AISetTagsLayer" feature. Scans a media clip's decoded frames
// through YA.AIFindObject (src/content/ai/find_object) and merges
// per-sample detections into time-ranged ClipTag records stored on
// Clip::tags (serialized by project_store; drawn as timeline ticks;
// listed in the inspector).
//
// Model management: no downloading — the user drops a YOLO-family
// ONNX export (e.g. yolov8n.onnx) plus an optional coco.names-style
// label file into <exe>/assets/models/ (or <exe>/models/). The env
// var XER_FIND_OBJECT_MODEL overrides discovery. Without a model the
// feature reports "model not installed" — see FindObjectModel().
// Backend/model contract lives in find_object.h.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "timeline_model.h"

namespace XER::VideoEditor::Ai {

// Resolves a detection model: $XER_FIND_OBJECT_MODEL first (must
// exist), then the first *.onnx in <exe>/assets/models/ and
// <exe>/models/ — "yolov8*.onnx" / "yolo11*.onnx" names sort ahead of
// everything else. Empty when nothing is installed.
std::string FindObjectModel();
// Label file for `model_path`: <model_stem>.names wins, else
// "coco.names" beside the model. Empty when neither exists.
std::string FindObjectLabels(const std::string& model_path);
// Human-readable install hint for status/error messages.
const char* FindObjectModelHint();

struct TagScanParams {
    double sample_step = 0.5;     // source seconds between probes
    double max_seconds = 600.0;   // source-span cap for very long media
    float confidence_threshold = 0.25f;
    float nms_threshold = 0.45f;
    double merge_gap = 1.0;       // source s: bridge misses <= this gap
};

// Runs detection over the clip's consumed source span
// [in_point, in_point + duration*rate): decodes a frame every
// sample_step seconds via the media decoder bridge, Detect()s, and
// merges same-label hits on consecutive samples into ClipTag ranges
// (confidence = max over the range; a single-sample miss inside a
// merge_gap is bridged). `progress` gets 0..1 callbacks.
//
// Returns false with error_out set when the model/backend is
// unavailable, the media won't decode, or a null out is passed.
// Worker-thread safe (owns no shared state) — drive it from an
// AsyncOp/Lab job, never the UI thread.
bool ScanClipForObjects(const Clip& clip, const std::string& model_path,
                        const std::string& labels_path,
                        const TagScanParams& params,
                        const std::function<void(float)>& progress,
                        const std::function<bool()>& should_cancel,
                        std::vector<ClipTag>* out,
                        std::string* error_out);

}  // namespace XER::VideoEditor::Ai
