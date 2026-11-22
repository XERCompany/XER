# NN backend stack — wiring map

Subagent audit (fb49d9cc) verified against source, 2026-11-22.
Consumers and dead paths below; the broken-runtime story for
onnxruntime is in `progress/2026-11-21-gpu-ai-modules.md` and the
model/license choices in `research/2026-11-22-ai-model-stack.md`.

## Backend matrix (as of 2026-11-22)

| Backend | Status | Entry points |
|---|---|---|
| OpenCV `cv::dnn` | Working inference backend | `find_object.cc:539-596` (`readNetFromONNX`, CPU target); also ml loaders/trainer/augmentor |
| onnxruntime | Wired (delay-load-probed), MSYS2 dll broken → cv::dnn fallback | `find_object.cc:321-536` (`OrtSessionImpl`, ORT C API, CPU EP); probe `nn_probe.cc:175-213`; fix paths: `mingw-w64-clang-x86_64-onnxruntime` rebuild or vcpkg `onnxruntime[-gpu]` |
| ggml | `find_package` REQUIRED at root; real consumer | `predictor.cc:75-257` dense-forward; probe `nn_probe.cc:247-343` |
| whisper.cpp | Runtime-probed (`libwhisper-1.dll`), not linked | `tools/video_editor/ai/whisper_transcribe.cc:31-96`; models via `XER_WHISPER_MODEL`/`models/` |
| MKL | Wired + runtime-probed BLAS consumer | `dsp_fft_engine.cc` DFTI; `tools/video_editor/lab/mkl_accel.cc` (`cblas_sdot`, `PatchSsd`); shim probes oneAPI dirs |
| OpenBLAS | Runtime-probe only on msys2 preset (`CMAKE_DISABLE_FIND_PACKAGE_OpenBLAS=ON`) | `mkl_accel.cc:166-186` probes `libopenblas.dll` |
| oneDNN (dnnl) | Wired, probe self-test only — no inference consumer | `nn_probe.cc:215-245`; MSYS2 pkg installed |
| cuDNN | Stubbed | shim `cmake/deps/configs/CUDNN/`; availability-only bridge; dead `async_io/dnn_backends/` |
| CUDA toolkit | Stubbed; driver/nvenc/hwaccel path fully works w/o toolkit | `src/gpu_video/cuda_{decode,encode}`; toolkit would enable `XER_BridgeCuda`, `XER_AI_HAS_CUDA_LIB` |
| llama.cpp | Missing entirely | vcpkg `llama-cpp` / MSYS2 `llama.cpp` if wanted |

## Dead / broken paths (pit-of-failure list, 2026-11-22)

Items 1–6 and 9 fixed 2026-11-22 (see notes); 7–8 remain.

1. ~~`ml/cuda_ops/*.cu` can never link~~ — FIXED: the `.cu` glob still
   requires `enable_language(CUDA)` (absent), but `predictor.cc` now
   gates `CudaTensorKernel` calls on `XER_AI_HAS_CUDA_KERNELS`, which
   `src/content/ai/CMakeLists.txt` defines ONLY when the `.cu` objects
   are actually added to the target (`ENABLE_CUDA AND
   CMAKE_CUDA_COMPILER_LOADED`).
2. ~~`cudnn_bridge.cc:40` `LoadLibraryA("cudnn64_*.dll")` wildcard~~ —
   FIXED: probes concrete names `cudnn64_9/8/7.dll`, `cudnn.dll`
   (+ `libcudnn.so.9` on POSIX). `dnn_backends/` still has no callers —
   noted in-source.
3. ~~`predictor.cc` `__has_include(<cblas.h>)` gate~~ — FIXED: gated on
   the link-time defines `XER_AI_HAS_OPENBLAS` / `XER_AI_HAS_MKL`
   (with each provider's real header: `<cblas.h>` vs `<mkl_cblas.h>`).
4. ~~`XER_AI_HAS_ONNX` set by both onnx_proto and onnxruntime~~ — FIXED:
   the protobuf lib now publishes `XER_AI_HAS_ONNX_PROTO`;
   `XER_AI_HAS_ONNX` means onnxruntime only. `has_onnx`/`has_onnx_proto`
   split plumbed through `ai_runtime_features`, `SdRuntimeCapabilities`
   (`onnx_proto_available`), and `sd_pipeline.cc` metadata
   (`"onnx"` vs `"onnx_proto"`); `hw_onnx_exporter` checks
   `has_onnx_proto`.
5. ~~`onnx_graph_parser.cc` byte-scan stub~~ — FIXED: real
   `onnx::ModelProto` decode under `XER_AI_HAS_ONNX_PROTO` (MSYS2
   `ONNX::onnx_proto` is a real `libonnx_proto.dll`, not headers-only);
   byte scan remains as the no-protobuf fallback.
6. ~~`ENABLE_CUDNN` only reaches `async_io`; the `CuDNN::CuDNN` link in
   `XER_ContentAI` is unconditional → option is mostly decorative~~ —
   FIXED: made consistent with `ENABLE_CUDA`'s treatment. The option
   now gates only the cudnn-header-dependent surface in `async_io`
   (the `cudnn_*_engine.cc` sources and the `CuDNN::CuDNN`/`CUDA::cudart`
   links, each additionally requiring the SDK-found target — local
   `find_package` calls added since async_io configures before
   `wrapper/cudnn`/`content/ai`), while `XER_BridgeCudnn` and the
   `XER_ContentAI` link auto-detect via `find_package(CUDNN QUIET)`
   exactly like `XER_BridgeCuda`/`CUDAToolkit`. Side fix of the same
   wave: the nn_probe OpenCV delay-load list was a `;`-splice bug —
   `XER_VE_NN_OPENCV_DLLS` carried only `libopencv_calib-500.dll`
   (first `OpenCV_LIBS` entry), which is neither delay-imported nor
   staged → false "runtime unloadable" (Win32 126) while the dnn path
   worked. The probe now lists the modules the core actually
   delay-imports (`opencv_core`, `opencv_imgproc`).
7. `sd.exe` (`tools/SdGenerate/`): hand-rolled CPU UNet over
   safetensors; backend hint "cuda/cudnn" is a label only — no
   ORT/cuDNN call happens (`sd_gpu_dispatch.cc`,
   `sd_model_runtime.cc:219-252` `allow_stub_inference=true`).
8. `demo_app` JS AI/SD demos can't run: `modules/module_registry.h`
   absent → `ImportModule` throws (`flow_script_import_module.cc:6-13`).
9. ~~Typo'd YA names `YA.Extenstions`, `YA.VideoEditor-ToolsDesgin`~~ —
   FIXED: renamed to `YA.Extensions` / `YA.VideoEditor-ToolsDesign`
   (`tools/video_editor/CMakeLists.txt`); shipped DLL names updated.

## Enable paths (what's missing per backend)

- ORT healthy: rebuild MSYS2 pkg or vcpkg port — zero code changes
  (all three target spellings already handled in cmake).
- CUDA toolkit: install NVIDIA toolkit → auto-enables; `.cu` kernels
  additionally need `enable_language(CUDA)` (currently absent).
- cuDNN: NVIDIA zip → `CUDNN_ROOT`/toolkit dir (shim probes); vcpkg
  `cudnn`+`cudnn-frontend`; optionally `-DENABLE_CUDNN=ON`.
- MKL: oneAPI Base → shim probes `ONEAPI_ROOT`/`MKLROOT`; or vcpkg
  `intel-mkl`.
- oneDNN: fully wired — needs a consumer, not enablement.
- cv::dnn GPU/superres: OpenCV built with `dnn_superres`/`cuda*`
  modules + targets added to `_fo_cv` list (`content/ai/CMakeLists.txt:508`).
