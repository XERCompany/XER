// Implementation of ai_tags.h — model discovery + the per-clip scan
// loop. Detection itself lives in YA.AIFindObject; this file owns the
// "which file / how a clip becomes tags" policy.
#include "ai_tags.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "content/ai/find_object/find_object.h"
#include "media_decoder.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace FindObj = XER::AI::FindObject;

// *.onnx candidates in `dir` (non-recursive), best-known detector names
// first: yolov8*.onnx, then yolo11*.onnx / yolo*.*, then other detector
// families (RF-DETR / RT-DETR / YOLOX / NanoDet — the models this
// module's decoder understands), then any *.onnx. The models dir also
// stages non-detector ONNX (BiRefNet matting, EfficientSAM) — those
// must never be picked as the object detector.
std::string FirstModelIn(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return {};
    std::vector<std::string> all;
    for (const auto& entry :
         std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".onnx") {
            all.push_back(entry.path().filename().string());
        }
    }
    if (all.empty()) return {};
    const auto score = [](const std::string& name) {
        std::string low = name;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (low.rfind("yolov8", 0) == 0) return 0;
        if (low.rfind("yolo11", 0) == 0) return 1;
        if (low.rfind("yolo", 0) == 0) return 2;
        for (const char* detector :
             {"rfdetr", "rf_detr", "rtdetr", "rt_detr", "detr",
              "yolox", "nanodet", "detect"}) {
            if (low.find(detector) != std::string::npos) return 3;
        }
        return 4;
    };
    std::sort(all.begin(), all.end(),
              [&](const std::string& a, const std::string& b) {
                  const int sa = score(a), sb = score(b);
                  return sa != sb ? sa < sb : a < b;
              });
    return (dir / all.front()).string();
}

}  // namespace

const char* FindObjectModelHint() {
    return "model not installed — put a detector ONNX (rfdetr_nano.onnx "
           "or yolov8n.onnx + matching .names) in assets/models/ next "
           "to the exe, or set XER_FIND_OBJECT_MODEL";
}

std::string FindObjectModel() {
    if (const char* env = std::getenv("XER_FIND_OBJECT_MODEL");
        env != nullptr && *env != '\0') {
        std::error_code ec;
        if (std::filesystem::is_regular_file(
                std::filesystem::path(env), ec)) {
            return env;
        }
    }
#if defined(_WIN32)
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        const auto dir = std::filesystem::path(exe).parent_path();
        for (const auto& sub : {"assets/models", "models"}) {
            if (std::string found = FirstModelIn(dir / sub);
                !found.empty()) {
                return found;
            }
        }
    }
#endif
    return {};
}

std::string FindObjectLabels(const std::string& model_path) {
    if (model_path.empty()) return {};
    std::error_code ec;
    const std::filesystem::path model(model_path);
    const std::filesystem::path stem_names =
        model.parent_path() / (model.stem().string() + ".names");
    if (std::filesystem::is_regular_file(stem_names, ec)) {
        return stem_names.string();
    }
    const std::filesystem::path coco =
        model.parent_path() / "coco.names";
    if (std::filesystem::is_regular_file(coco, ec)) {
        return coco.string();
    }
    return {};
}

bool ScanClipForObjects(const Clip& clip, const std::string& model_path,
                        const std::string& labels_path,
                        const TagScanParams& params,
                        const std::function<void(float)>& progress,
                        const std::function<bool()>& should_cancel,
                        std::vector<ClipTag>* out,
                        std::string* error_out) {
    if (out == nullptr) return false;
    out->clear();
    if (clip.source_path.empty() || clip.is_adjustment) {
        if (error_out) *error_out = "clip has no media to scan";
        return false;
    }
    if (model_path.empty()) {
        if (error_out) *error_out = FindObjectModelHint();
        return false;
    }

    FindObj::Detector detector;
    std::string err;
    if (!detector.Load(model_path, labels_path, "", &err)) {
        if (error_out) *error_out = err;
        return false;
    }

    MediaDecoderPtr decoder = MakeBridgeDecoder();
    if (decoder == nullptr) {
        if (error_out) *error_out = "no media decoder";
        return false;
    }
    if (!decoder->Open(clip.source_path, &err)) {
        if (error_out) *error_out = "decode open: " + err;
        return false;
    }
    if (decoder->GetWidth() <= 0 || decoder->GetHeight() <= 0) {
        if (error_out) *error_out = "media has no video stream";
        return false;
    }

    const double rate =
        clip.playback_rate > 0.0 ? clip.playback_rate : 1.0;
    const double span_src = clip.duration * rate;
    const double t_begin = std::max(0.0, clip.in_point);
    const double t_end =
        t_begin + std::min(span_src, params.max_seconds);
    const double step = params.sample_step > 0.05
                            ? params.sample_step
                            : 0.5;
    const int samples =
        static_cast<int>(std::ceil((t_end - t_begin) / step));
    if (samples <= 0) {
        if (error_out) *error_out = "clip span is empty";
        return false;
    }

    FindObj::Options opts;
    opts.confidence_threshold = params.confidence_threshold;
    opts.nms_threshold = params.nms_threshold;

    // Per-sample label presence: sample index -> best detection per
    // label (max confidence wins when a label repeats on a frame).
    std::vector<std::map<std::string, float>> hits(
        static_cast<std::size_t>(samples));

    std::vector<std::uint8_t> bgra;
    std::vector<std::uint8_t> kept;
    int width = 0, height = 0;
    for (int i = 0; i < samples; ++i) {
        if (should_cancel != nullptr && should_cancel()) {
            if (error_out) *error_out = "cancelled";
            return false;
        }
        const double t = t_begin + i * step;
        decoder->Seek(t);   // keyframe-backward; forward-discard below
        // Same bounded forward-discard as clip_poster's DecodeFrameAt:
        // NextFrame decodes from the keyframe onward; keep the last
        // decoded frame and stop at the first one at/after the target.
        // A failed NextFrame zero-fills `bgra`, so only successful
        // decodes are swapped into `kept`.
        constexpr int kMaxForwardFrames = 300;
        kept.clear();
        int kept_w = 0, kept_h = 0;
        for (int f = 0; f < kMaxForwardFrames &&
                        decoder->NextFrame(&bgra, &width, &height);
             ++f) {
            bgra.swap(kept);
            kept_w = width;
            kept_h = height;
            if (decoder->GetPositionSeconds() >= t) break;
        }
        if (kept.empty() || kept_w <= 0 || kept_h <= 0) {
            continue;   // undecodable sample — the merge can bridge it
        }
        const auto dets = detector.Detect(kept.data(), kept_w, kept_h,
                                          kept_w * 4, opts);
        for (const FindObj::Detection& d : dets) {
            auto& slot = hits[static_cast<std::size_t>(i)][d.label];
            slot = std::max(slot, d.confidence);
        }
        if (progress) {
            progress(static_cast<float>(i + 1) /
                     static_cast<float>(samples));
        }
    }

    // Merge consecutive same-label hits into ranges — a run of <=
    // merge_gap source seconds with no hit is bridged so one
    // flickering detection does not split a tag.
    const int gap_samples =
        std::max(0, static_cast<int>(params.merge_gap / step + 0.5));
    std::map<std::string, std::vector<ClipTag>> ranges;
    std::map<std::string, int> last_hit;   // label -> sample index
    for (int i = 0; i < samples; ++i) {
        for (const auto& [label, conf] :
             hits[static_cast<std::size_t>(i)]) {
            auto& list = ranges[label];
            const double t0 = t_begin + i * step;
            const double t1 = t0 + step;
            auto it = last_hit.find(label);
            if (!list.empty() && it != last_hit.end() &&
                i - it->second <= gap_samples + 1) {
                auto& last = list.back();
                last.end = t1;
                last.confidence = std::max(last.confidence, conf);
            } else {
                ClipTag tag;
                tag.start = t0;
                tag.end = t1;
                tag.label = label;
                tag.confidence = conf;
                list.push_back(std::move(tag));
            }
            last_hit[label] = i;
        }
    }
    for (auto& [label, list] : ranges) {
        for (ClipTag& tag : list) {
            tag.start = std::clamp(tag.start, t_begin, t_end);
            tag.end = std::clamp(tag.end, tag.start, t_end);
            out->push_back(std::move(tag));
        }
    }
    std::sort(out->begin(), out->end(),
              [](const ClipTag& a, const ClipTag& b) {
                  return a.start != b.start ? a.start < b.start
                                            : a.label < b.label;
              });
    return true;
}

}  // namespace XER::VideoEditor::Ai
