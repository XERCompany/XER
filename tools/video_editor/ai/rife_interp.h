// RIFE 4.25-lite frame interpolation for the VideoEditor — the
// wave-2 "Smooth slow-mo (AI)" clip bounce
// (docs/wiki/research/2026-11-23-ai-features-wave2.md §1). Wraps
// the staged rife_v425lite.onnx on the shared XER::AI::Onnx
// session — same staged-model story as the rest of ai/
// (Lab::FindAiModel under assets/models).
//
// Contract (verified 2026-11-23 against t9749/rife-4.25-lite-onnx
// rife_v425lite.onnx, fp32, 22.7 MB, MIT —
// assets/models/README.md):
//   input  `img0`/`img1` f32 [1,3,H,W] RGB 0..1 — the pair, dynamic
//          H/W but BOTH must be multiples of 64 (this wrapper pads
//          right/bottom with edge-replicate and crops the result
//          back)
//   input  `timestep`      f32 [1,1,1,1] — interpolation position,
//          0 = img0, 1 = img1
//   output `frame`         f32 [1,3,H,W] RGB 0..1 — the middle frame
//
// BACKEND GATE (research §1 flag): RIFE is GridSample-heavy and
// cv::dnn's GridSample path is UNVERIFIED. The default Load binds
// whatever backend loads first (ORT preferred), but the bounce and
// the UI gate go through RifeBackendOk() — "onnxruntime" only —
// until the smoke's moving-square check has proven a cv::dnn run
// produces real interpolation (a session that loads but computes
// garbage fails that check loudly, which is the verification the
// research asked for).
//
// Productization seam: clip ctx-menu "Smooth slow-mo (AI)" on a
// playback_rate < 1 clip -> ai/ai_bounce.h decode->interpolate->
// encode worker -> <stem>.xer.slowmo<N>x.mp4, then the clip is
// repointed at the baked file through history (rate becomes rate*N). Video-only output — speed-changed audio would need
// a stretcher this wave doesn't have; the bounced clip simply has
// no audio stream.
//
// RifeInterp is one session = one instance; thread-hostile like
// the runtimes it wraps — bounce on the worker, never the UI
// thread.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace XER::AI::Onnx {
class Session;
}

namespace XER::VideoEditor::Ai {

inline constexpr const char* kRifeFile = "rife_v425lite.onnx";
// RIFE's spatial alignment requirement — H and W must each be a
// multiple of this (the export's flow pyramid depth).
inline constexpr int kRifeAlign = 64;
// Backend id the bounce path requires while cv::dnn's GridSample
// stays unverified (see the header gate note).
inline constexpr const char* kRifeRequiredBackend = "onnxruntime";

// Resolves the staged model — empty when rife_v425lite.onnx is not
// installed (XER_AI_MODELS_DIR / assets/models / models).
std::string RifeModel();
const char* RifeModelHint();
// Model staged AND the ORT backend (the GridSample-safe one)
// loadable — the UI gate for the slow-mo action.
bool RifeAvailable();
// True when `backend_id` is allowed to run RIFE for the
// productized path (ORT only until cv::dnn's GridSample is
// verified — the smoke's moving-square check is the verifier).
bool RifeBackendOk(const std::string& backend_id);

// One interpolator = one ONNX session (thread-hostile).
class RifeInterp {
public:
    RifeInterp();
    ~RifeInterp();
    RifeInterp(const RifeInterp&) = delete;
    RifeInterp& operator=(const RifeInterp&) = delete;

    // Loads `model_path` on `backend` ("" = first loadable, ORT
    // preferred). The GridSample gate is NOT enforced here —
    // callers that need the verified path gate on
    // RifeBackendOk(BackendId()) after load or pass
    // kRifeRequiredBackend explicitly.
    bool Load(const std::string& model_path,
              const std::string& backend = "",
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;

    // (a, b, t) -> the interpolated frame. All BGRA8 w x h (stride
    // >= w*4); t is clamped to [0,1] (0 returns a, 1 returns b
    // verbatim without inference). `out` receives w*h*4 BGRA.
    // False with error_out when unloaded, args are degenerate, the
    // run fails, or the output contract mismatches.
    bool Interpolate(const std::uint8_t* a, const std::uint8_t* b,
                     int w, int h, int stride, float t,
                     std::vector<std::uint8_t>* out,
                     std::string* error_out = nullptr);

    // Appends `factor - 1` intermediate frames (each w*h*4 BGRA, in
    // OUTPUT order) between a and b — the slow-mo bake primitive.
    // factor 2 -> [t=0.5]; powers of two recurse (4x synthesizes
    // t=0.5 first, then interpolates (a,mid) and (mid,b) — better
    // motion accuracy on large steps); other factors subdivide the
    // timestep directly (t = k/factor). factor < 2 appends
    // nothing and returns true.
    bool InterpolateBetween(const std::uint8_t* a,
                            const std::uint8_t* b, int w, int h,
                            int stride, int factor,
                            std::vector<std::vector<std::uint8_t>>* out,
                            std::string* error_out = nullptr);

private:
    std::unique_ptr<XER::AI::Onnx::Session> session_;
    std::vector<float> nchw0_;
    std::vector<float> nchw1_;
    std::vector<int64_t> timestep_dims_;
};

// ---- session-free helpers (smoke-testable) ----------------------
// The interpolation plan for one pair: the timesteps, IN OUTPUT
// ORDER, a bake of `factor` emits between the endpoints — powers
// of two via recursive halving, others by subdivision. factor < 2
// yields an empty plan. Exposed so the smoke verifies the
// ordering/plan math without a model.
std::vector<float> RifeInterpPlan(int factor);

// Padded H/W the model needs for a `dim` (next multiple of
// kRifeAlign; dim itself when already aligned).
int RifePadDim(int dim);

}  // namespace XER::VideoEditor::Ai
