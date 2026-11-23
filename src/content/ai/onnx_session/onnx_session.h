// YA.AIFindObject — shared dual-backend ONNX session (extracted from
// find_object.cc so the lab's matting / smart-select ops can reuse the
// exact same runtime story).
//
// Backend contract (identical to find_object.h):
//   * ONNX Runtime (XER_AI_ONNX_ONNXRUNTIME at build time, mapped from
//     XER_AI_FO_ONNXRUNTIME while this TU ships inside the
//     AIFindObject module) — ORT C API, CPU EP. The onnxruntime.dll
//     import is delay-loaded and LoadLibrary-probed before first use;
//     a missing/unloadable DLL reports loadable:false instead of
//     reaching the delay-load binder.
//   * OpenCV DNN (XER_AI_ONNX_OPENCV <- XER_AI_FO_OPENCV) —
//     cv::dnn::readNetFromONNX fallback, needs no ORT.
//   Session::Load picks the first loadable backend (ORT preferred) or
//   honors an explicit backend id.
//
// Unlike the detection-only session this one is model-shape agnostic:
// every declared input name is captured at Load, Run() feeds named
// f32 tensors of arbitrary dims and returns every f32 output as a
// borrowed TensorView. Thread-hostile like the runtimes it wraps:
// one Session per thread, no shared state (the lab serializes its
// cached sessions behind a mutex).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::AI::Onnx {

// One model input to feed: `name` matches a declared graph input —
// an EMPTY name binds by position (i-th provided input -> i-th
// declared input), which is what single-image models want. `dims`
// are the tensor shape fed this run; `data` is `dims`-sized f32.
// `data_i64` is the optional INT64 variant for models that declare
// integer graph inputs (e.g. silero-vad's scalar `sr`) — set it
// INSTEAD of `data`; when both are non-null the int64 buffer wins.
struct Input {
    std::string name;
    std::vector<std::int64_t> dims;
    const float* data = nullptr;
    const std::int64_t* data_i64 = nullptr;
};

// One f32 output tensor, borrowed from the session (valid until the
// next Run call on that session).
struct TensorView {
    std::string name;
    std::vector<std::int64_t> dims;
    const float* data = nullptr;
    std::size_t ElementCount() const {
        std::size_t n = 1;
        for (const std::int64_t d : dims) {
            n *= d > 0 ? static_cast<std::size_t>(d) : 0;
        }
        return n;
    }
};

// One row of the backend matrix — same shape as find_object's.
struct BackendInfo {
    const char* id;   // "onnxruntime" | "opencv_dnn"
    bool linked = false;
    bool loadable = false;
    std::string detail;
};

// Full backend matrix — always lists both backends. Cheap; probes
// LoadLibrary once and caches the verdict.
std::vector<BackendInfo> Backends();
std::string BackendReport();
// First loadable backend id ("onnxruntime" preferred), "" when none.
std::string ActiveBackendId();

// Reads the ONNX protobuf header (no runtime needed) and returns the
// square edge H (== W) of the first rank-4 [N,C,H,W] graph input whose
// H/W are static ints — 0 when the file cannot be parsed or every
// input dim is symbolic. Shared by callers whose models declare a
// fixed square input (BiRefNet/EfficientSAM 1024, detectors 384/640).
int ProbeInputSquare(const std::string& utf8_path);

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;

    // Loads `utf8_model_path` on `backend` ("" = first loadable, ORT
    // preferred). Returns false with error_out set when no backend is
    // loadable, the model file is missing/unreadable, or the runtime
    // rejects it.
    bool Load(const std::string& utf8_model_path,
              const std::string& backend = "",
              std::string* error_out = nullptr);
    bool IsLoaded() const;
    const std::string& BackendId() const;  // "" until Load succeeds

    const std::vector<std::string>& InputNames() const;
    const std::vector<std::string>& OutputNames() const;
    std::size_t OutputCount() const;
    // Declared dims for input `index` (dynamic/symbolic dims <= 0);
    // empty vector when unloaded or out of range. Best-effort on the
    // cv::dnn backend (it cannot report declared shapes portably).
    std::vector<std::int64_t> InputDims(std::size_t index) const;

    // Runs the model. `inputs` bind by name (empty name = positional).
    // Every output f32 tensor lands in `outs_out` as a borrowed view,
    // valid until the next Run on this session. False with error_out
    // on a runtime failure or non-f32-only outputs.
    bool Run(const std::vector<Input>& inputs,
             std::vector<TensorView>* outs_out,
             std::string* error_out = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::AI::Onnx
