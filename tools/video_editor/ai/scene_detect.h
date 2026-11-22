// Scene-change detection ("edit decision list") over decoded frames —
// the data layer behind the "ai.detect_scenes" service (sdk/ve.core.h
// v3). Compares a low-res luma/chroma signature between sampled
// frames; a jump above the threshold is a cut. Pure worker code like
// the rest of ai/ — safe on any thread, no shared state.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

struct SceneDetectParams {
    double sample_step = 0.25;    // source seconds between probes
    double max_seconds = 600.0;   // source-span cap
    double threshold = 0.30;      // signature distance 0..1 => cut
    double min_scene = 0.5;       // merge cuts closer than this (s)
};

struct SceneCut {
    double t = 0.0;          // source seconds of the cut frame
    float strength = 0.0f;   // signature distance 0..1
};

// Decodes one frame every `sample_step` source seconds from
// `utf8_path` (the same bridge decoder ScanClipForObjects uses) and
// emits a SceneCut whenever the 16x16 luma + per-channel-mean
// signature jumps beyond `threshold`. Returns the SOURCE-domain cut
// times — callers map into timeline time via in_point/rate. False
// with `error_out` when the file has no decodable video.
bool DetectScenes(const std::string& utf8_path,
                  const SceneDetectParams& params,
                  const std::function<void(float)>& progress,
                  const std::function<bool()>& should_cancel,
                  std::vector<SceneCut>* out, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
