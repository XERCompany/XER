// Neural/AI backend plumbing probe for the video editor — the single
// place that answers "which NN runtimes were compiled into this build".
//
// This is the seam for the future AI features (upscale, object detect,
// auto-caption): it does NOT implement any feature, it only records
// which backend SDKs the build linked and proves each one can execute a
// trivial computation. A future agent consumes the backends directly:
//
//   * OpenCV (XER_WITH_OPENCV, XER_VE_NN_OPENCV) — cv::Mat on decoded
//     frames: cv::dnn for object detection, cv::dnn_superres for
//     upscale, imgproc for resize/colorspace. Link comes via
//     XER_VideoEditor_Core; DLLs are delay-loaded, staged next to the
//     exe.
//   * ONNX Runtime (XER_WITH_ONNX, XER_VE_NN_ONNXRUNTIME) —
//     Ort::Session over an exported .onnx model (upscale nets like
//     Real-ESRGAN, detection nets like YOLO ONNX exports, or a
//     captioning encoder). Feed a decoded frame's RGB plane as an
//     Ort::Value input tensor.
//   * oneDNN (XER_WITH_DNNL, XER_VE_NN_DNNL) — dnnl::engine/primitives
//     for custom CPU kernels (layout reorder, eltwise, GEMM) around the
//     model runtimes.
//   * ggml (XER_WITH_GGML, XER_VE_NN_GGML) — ggml tensor graphs and the
//     llama.cpp/whisper.cpp model family; backend modules
//     (ggml-cpu-*.dll, ggml-blas.dll, ggml-vulkan.dll) are dlopen'd from
//     beside the exe via ggml_backend_load_all().
//   * MKL / OpenBLAS — never import-linked; lab/mkl_accel.cc
//     LoadLibrary-probes mkl_rt / libopenblas at run time (manual
//     Intel oneAPI install or the MSYS2 openblas package).
//   * CUDA / cuDNN — not available through MSYS2. The CUDAToolkit
//     module + the cmake/deps/configs/CUDNN shim probe NVIDIA install
//     dirs at configure time; src/wrapper/{cuda,cudnn} are the real
//     bridges and report "unavailable" without the SDKs.
//
// The smoke check "nn_backends" prints NnBackendsReport() (always
// passes — informational) and runs NnBackendSelfTest() for every
// compiled-in backend, so a broken link/load surfaces as a real FAIL.
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor {

// One row of the backend matrix.
struct NnBackend {
    const char* id;        // stable machine id: "opencv", "onnxruntime",
                           // "dnnl", "ggml", "openblas", "mkl",
                           // "cuda", "cudnn"
    bool compiled_in;      // SDK was present at build time / is linked
    std::string version;   // package/SDK version ("" when unknown)
    std::string detail;    // how it is wired (delay-load, runtime probe,
                           // manual-install note, ...)
};

// The full backend matrix: compiled-in backends first, then the
// runtime-probed / documented-absent rows (so the report always lists
// every backend the editor knows about, present or not).
std::vector<NnBackend> NnBackends();

// Human-readable matrix — one "  <id>  <status>" line per backend,
// newline-terminated. Cheap; safe to print to stdout from the smoke
// check. Does not run any backend code (DLLs stay unloaded).
std::string NnBackendsReport();

// Tiny end-to-end exercise of one compiled-in backend, e.g.:
//   "opencv"      — build a 64x64 cv::Mat, cv::resize to 32x32
//   "onnxruntime" — OrtApi GetVersionString + Env/SessionOptions create
//   "dnnl"        — dnnl::engine + a dnnl::sum primitive on 4 floats
//   "ggml"        — ggml_backend_load_all + a 4-float add graph on the
//                   first available backend device
// Returns an empty string on success or a one-line failure reason.
// Calling it for an id that is not compiled in returns a reason too —
// check NnBackend::compiled_in first.
std::string NnBackendSelfTest(const char* id);

}  // namespace XER::VideoEditor
