// Real-ESRGAN realesr-general-x4v3 media upscale for the
// VideoEditor — the wave-2 "AI Upscale 4x" bin bounce
// (docs/wiki/research/2026-11-23-ai-features-wave2.md §2). Wraps
// the staged realesr-general-x4v3.onnx on the shared
// XER::AI::Onnx session — same runtime story as the rest of ai/:
// a staged file under assets/models (Lab::FindAiModel) plus
// whatever backend loads it (SRVGG is plain convs + pixelshuffle —
// expected to run on cv::dnn as well as ORT; the smoke's
// constant-gradient check validates whichever backend binds).
//
// Contract (verified 2026-11-23 against Samo629/real-esrgan-onnx
// realesr-general-x4v3-dynfix.onnx, fp32, 4.9 MB —
// assets/models/README.md):
//   input  `input`  f32 [B,3,H,W] RGB 0..1 — DYNAMIC H/W
//   output `output` f32 [B,3,4H,4W] RGB 0..1 — exactly 4x
// No mean/std normalization, no fixed size — tiles feed at their
// native rect.
//
// Productization seam: Media Bin "AI Upscale 4x" -> ai/ai_bounce.h
// decode->runner->encode worker -> <stem>.xer.upscaled.mp4 imported
// back into the catalog. Chosen over an export_engine upscale pass:
// a bounced file is reusable media (bin preview, re-trim, normal
// export) while an export flag would re-run the model on every
// render and buys nothing the file doesn't.
//
// UpscaleX4v3 is one session = one instance; thread-hostile like
// the runtimes it wraps — bounce on the worker, never the UI
// thread.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai/img2img_runner.h"

namespace XER::AI::Onnx {
class Session;
}

namespace XER::VideoEditor::Ai {

inline constexpr const char* kX4v3File = "realesr-general-x4v3.onnx";
// Output px per input px — the x4v3 contract.
inline constexpr int kX4v3Scale = 4;

// Resolves the staged model — empty when realesr-general-x4v3.onnx
// is not installed (XER_AI_MODELS_DIR / assets/models / models).
std::string UpscaleModel();
const char* UpscaleModelHint();
// Model staged AND an ONNX backend loadable — the bin menu's gate.
bool UpscaleAvailable();

// One upscaler = one ONNX session (thread-hostile). RunTile does a
// single model pass on an arbitrary BGRA block -> 4x BGRA block;
// UpscaleFrame drives the whole frame through the tiled runner
// (ai/img2img_runner.h) with progress + cancel.
class UpscaleX4v3 {
public:
    UpscaleX4v3();
    ~UpscaleX4v3();
    UpscaleX4v3(const UpscaleX4v3&) = delete;
    UpscaleX4v3& operator=(const UpscaleX4v3&) = delete;

    // Loads `model_path` on `backend` ("" = first loadable, ORT
    // preferred — same contract as Onnx::Session::Load).
    bool Load(const std::string& model_path,
              const std::string& backend = "",
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;

    // Single inference on one BGRA block -> `out` receives the
    // (w*4) x (h*4) BGRA result. False with error_out when
    // unloaded, the args are degenerate, the run fails, or the
    // output contract mismatches. Used by the runner's tile fn AND
    // directly by the smoke (whole small frames).
    bool RunTile(const std::uint8_t* bgra, int w, int h, int stride,
                 std::vector<std::uint8_t>* out,
                 std::string* error_out = nullptr);

    // Whole-frame upscale through RunImageToImage — `params.scale`
    // is overridden to kX4v3Scale; tile/overlap come from `params`
    // (defaults 256/16 when {} is passed). `out` gets the 4x BGRA
    // frame; progress/cancel as the runner documents.
    bool UpscaleFrame(const std::uint8_t* bgra, int w, int h,
                      int stride, const Img2ImgParams& params,
                      const std::function<void(float)>& progress,
                      const std::function<bool()>& should_cancel,
                      std::vector<std::uint8_t>* out, int* out_w,
                      int* out_h, std::string* error_out = nullptr);

private:
    std::unique_ptr<XER::AI::Onnx::Session> session_;
    std::vector<float> nchw_;       // 3*tile*tile scratch
    std::string input_name_;        // declared graph input (usually "input")
    std::string output_name_;       // declared first output ("output")
};

}  // namespace XER::VideoEditor::Ai
