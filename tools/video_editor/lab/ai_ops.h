// Object Lab AI ops — neural matting and promptable segmentation on
// video::Frame (packed BGRA8), built on the shared dual-backend ONNX
// session (src/content/ai/onnx_session — ORT delay-load-probed,
// cv::dnn fallback).
//
//   AlphaMatte / RemoveBackground — BiRefNet "remove background":
//     input_image [1,3,S,S] RGB ImageNet-normalized ->
//     output_image [1,1,S,S] logits -> sigmoid = alpha matte
//   SmartSelectMask — EfficientSAM-Ti point prompts:
//     batched_images [1,3,S,S] RGB /255 + point coords/labels ->
//     output_masks [1,1,3,S,S] + iou_predictions [1,1,3] (best-IoU)
//
// Sessions lazy-load on first use and are cached per model path
// (mutex-guarded) — the lab's AsyncOp keeps every call off the UI
// thread. A missing model file or an unloadable runtime reports a
// graceful error; nothing hard-fails.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "lab/object_ops.h"   // IntPoint
#include "video/frame.h"

namespace XER::VideoEditor::Lab {

// Optional progress/cancel plumbing — mirror of InpaintParams':
// the caller (a lab AsyncOp job) forwards JobControl.
struct AiCallbacks {
    std::function<bool()> should_cancel;
    std::function<void(float)> report_progress;
};

// Resolves a staged model file by name: $XER_AI_MODELS_DIR/<name>,
// then <exe>/assets/models/<name>, then <exe>/models/<name>.
// Empty string when the file is not installed.
std::string FindAiModel(const char* file_name);

// Install hint for status/error text when FindAiModel comes back
// empty.
std::string AiModelHint(const char* file_name);

// BiRefNet alpha matte: BGRA8 frame in -> w*h 0..255 coverage mask
// (255 = foreground). Empty vector + error_out on failure (bad frame,
// missing model, backend rejection, cancel).
std::vector<std::uint8_t> AlphaMatte(
    const video::Frame& frame, const std::string& model_path,
    const AiCallbacks& cb = {}, std::string* error_out = nullptr);

// Same matte applied: a same-size BGRA8 frame whose alpha is the
// matte (foreground cutout — what "Remove Background" exports).
// Invalid Frame on failure.
video::Frame RemoveBackground(
    const video::Frame& frame, const std::string& model_path,
    const AiCallbacks& cb = {}, std::string* error_out = nullptr);

// EfficientSAM-Ti smart select: positive click points in FRAME pixels
// -> w*h 0..255 instance mask (best of the model's 3 mask proposals
// by iou_predictions). Needs >= 1 point. Empty vector + error_out on
// failure.
std::vector<std::uint8_t> SmartSelectMask(
    const video::Frame& frame, const std::vector<IntPoint>& points,
    const std::string& model_path, const AiCallbacks& cb = {},
    std::string* error_out = nullptr);

}  // namespace XER::VideoEditor::Lab
