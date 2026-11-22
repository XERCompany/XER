// Implementation of nn_probe.h — see the header for the seam contract.
// Every backend block is compile-gated on its XER_VE_NN_<NAME> define
// (set in CMakeLists.txt when the package was found and XER_WITH_<name>
// is ON), so this TU always compiles regardless of which SDKs exist.
//
// Keep the self-tests TINY and side-effect free: they exist to prove
// the delay-loaded DLLs actually bind and the APIs execute, not to
// measure anything.
#include "nn_probe.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#    include <windows.h>   // LoadLibraryExA — delay-load pre-probe
#endif

#include "lab/mkl_accel.h"  // runtime-probed BLAS (MKL / OpenBLAS / scalar)

// Build-time feature gates — injected by tools/video_editor/CMakeLists.txt.
#ifndef XER_VE_NN_OPENCV_VERSION
#    define XER_VE_NN_OPENCV_VERSION ""
#endif
#ifndef XER_VE_NN_ONNXRUNTIME_VERSION
#    define XER_VE_NN_ONNXRUNTIME_VERSION ""
#endif
#ifndef XER_VE_NN_DNNL_VERSION
#    define XER_VE_NN_DNNL_VERSION ""
#endif
#ifndef XER_VE_NN_GGML_VERSION
#    define XER_VE_NN_GGML_VERSION ""
#endif
#ifndef XER_VE_NN_CUDA_VERSION
#    define XER_VE_NN_CUDA_VERSION ""
#endif

#if defined(XER_VE_NN_OPENCV)
#    include <opencv2/core.hpp>
#    include <opencv2/imgproc.hpp>
#endif

#if defined(XER_VE_NN_ONNXRUNTIME)
#    include <onnxruntime_c_api.h>
#endif

#if defined(XER_VE_NN_DNNL)
#    include <dnnl.hpp>
#    include <dnnl_version.h>
#endif

#if defined(XER_VE_NN_GGML)
#    include <ggml-backend.h>
#    include <ggml.h>
#endif

namespace XER::VideoEditor {
namespace {

// The matrix is rebuilt per call — the BLAS backend row reflects the
// live lab/mkl_accel probe, which can change if a DLL appears later.
std::vector<NnBackend> BuildMatrix() {
    std::vector<NnBackend> rows;
    rows.reserve(8);

#if defined(XER_VE_NN_OPENCV)
    rows.push_back({"opencv", true, XER_VE_NN_OPENCV_VERSION,
                    "linked; libopencv_*-*.dll delay-loaded, staged "
                    "next to the exe"});
#else
    rows.push_back({"opencv", false, "",
                    "not found — install mingw-w64-clang-x86_64-opencv "
                    "(MSYS2) or the vcpkg opencv port"});
#endif

#if defined(XER_VE_NN_ONNXRUNTIME)
    rows.push_back({"onnxruntime", true, XER_VE_NN_ONNXRUNTIME_VERSION,
                    "linked; onnxruntime.dll delay-loaded, staged "
                    "next to the exe"});
#else
    rows.push_back({"onnxruntime", false, "",
                    "not found — install "
                    "mingw-w64-clang-x86_64-onnxruntime (MSYS2) or the "
                    "vcpkg onnxruntime port"});
#endif

#if defined(XER_VE_NN_DNNL)
    rows.push_back({"dnnl", true, XER_VE_NN_DNNL_VERSION,
                    "linked; libdnnl.dll delay-loaded, staged next to "
                    "the exe"});
#else
    rows.push_back({"dnnl", false, "",
                    "not found — install mingw-w64-clang-x86_64-onednn "
                    "(MSYS2) or the vcpkg dnnl port"});
#endif

#if defined(XER_VE_NN_GGML)
    rows.push_back({"ggml", true, XER_VE_NN_GGML_VERSION,
                    "linked; ggml-1.dll delay-loaded, ggml-cpu-*/"
                    "ggml-blas/ggml-vulkan backends dlopen'd from "
                    "beside the exe"});
#else
    rows.push_back({"ggml", false, "",
                    "not found — install mingw-w64-clang-x86_64-ggml "
                    "(MSYS2) or the vcpkg ggml port"});
#endif

    // BLAS row: never import-linked — lab/mkl_accel.cc resolves the
    // provider at run time and can never fail (Scalar fallback).
    const Lab::AccelBackend blas = Lab::ActiveBackend();
    rows.push_back({"openblas", blas == Lab::AccelBackend::OpenBLAS, "",
                    "runtime probe via lab/mkl_accel — active backend: " +
                        std::string(Lab::BackendName())});

#if defined(XER_VE_NN_MKL)
    rows.push_back({"mkl", true, "",
                    "oneAPI install detected at configure time; "
                    "lab/mkl_accel probes mkl_rt*.dll at run time"});
#else
    rows.push_back({"mkl", false, "",
                    "not installed — no MSYS2 package; install the "
                    "Intel oneAPI Base Toolkit (MKL component) or the "
                    "vcpkg mkl port; lab/mkl_accel then probes "
                    "mkl_rt*.dll at run time"});
#endif

#if defined(XER_VE_NN_CUDA)
    rows.push_back({"cuda", true, XER_VE_NN_CUDA_VERSION,
                    "toolkit detected at configure time; "
                    "src/wrapper/cuda bridge (ENGINE_HAS_CUDA_BRIDGE)"});
#else
    rows.push_back({"cuda", false, "",
                    "not installed — no MSYS2 package; install the "
                    "NVIDIA CUDA Toolkit, then ENABLE_CUDA=ON / the "
                    "src/wrapper/cuda bridge picks it up"});
#endif

#if defined(XER_VE_NN_CUDNN)
    rows.push_back({"cudnn", true, "",
                    "cuDNN install detected at configure time; "
                    "src/wrapper/cudnn bridge (ENGINE_HAS_CUDNN_BRIDGE)"});
#else
    rows.push_back({"cudnn", false, "",
                    "not installed — no MSYS2 package; unzip NVIDIA "
                    "cuDNN into the CUDA toolkit dir or set CUDNN_ROOT; "
                    "the configs/CUDNN shim then reports it"});
#endif

    return rows;
}

#if defined(XER_VE_NN_OPENCV)
std::string TestOpenCV() {
    // Generated 64x64 BGR frame -> 32x32 resize: the exact shape of a
    // "decoded frame -> model input" pre-process step.
    cv::Mat src(64, 64, CV_8UC3);
    src.setTo(cv::Scalar(30, 60, 90));
    cv::rectangle(src, cv::Point(8, 8), cv::Point(56, 56),
                  cv::Scalar(200, 100, 50), cv::FILLED);
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(32, 32));
    if (dst.empty() || dst.rows != 32 || dst.cols != 32) {
        return "cv::resize produced a bad image";
    }
    const cv::Scalar s = cv::sum(dst);
    if (s[0] + s[1] + s[2] <= 0.0) {
        return "cv::resize output is all zeros";
    }
    return {};
}
#endif

#if defined(XER_VE_NN_ONNXRUNTIME)
std::string TestOnnxRuntime() {
    // C API surface: api handle + Env + SessionOptions. Proves the DLL
    // bound and the runtime can initialize — a Session additionally
    // needs a model file, which the smoke environment does not carry.
    const OrtApiBase* base = OrtGetApiBase();
    if (base == nullptr) {
        return "OrtGetApiBase returned null";
    }
    const OrtApi* api = base->GetApi(ORT_API_VERSION);
    // GetVersionString lives on OrtApiBase (not OrtApi) since ORT 1.17.
    const char* ort_version =
        base->GetVersionString != nullptr ? base->GetVersionString()
                                          : nullptr;
    if (api == nullptr || ort_version == nullptr ||
        ort_version[0] == '\0') {
        return "OrtApi unavailable / empty version string";
    }
    OrtEnv* env = nullptr;
    if (api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "xer_nn_probe",
                       &env) != nullptr ||
        env == nullptr) {
        return "CreateEnv failed";
    }
    OrtSessionOptions* opts = nullptr;
    const OrtStatus* st = api->CreateSessionOptions(&opts);
    if (st != nullptr) {
        api->ReleaseStatus(const_cast<OrtStatus*>(st));
    }
    if (opts != nullptr) {
        api->ReleaseSessionOptions(opts);
    }
    api->ReleaseEnv(env);
    if (st != nullptr || opts == nullptr) {
        return "CreateSessionOptions failed";
    }
    return {};
}
#endif

#if defined(XER_VE_NN_DNNL)
std::string TestDnnl() {
    try {
        // engine + a real sum primitive: [1,1,1,1] + [2,2,2,2] = [3,...].
        dnnl::engine eng(dnnl::engine::kind::cpu, 0);
        dnnl::stream strm(eng);
        const dnnl::memory::dims dims{4};
        const dnnl::memory::desc md(dims, dnnl::memory::data_type::f32,
                                    dnnl::memory::format_tag::a);
        float a[4] = {1.f, 1.f, 1.f, 1.f};
        float b[4] = {2.f, 2.f, 2.f, 2.f};
        float out[4] = {0.f, 0.f, 0.f, 0.f};
        dnnl::memory m1(md, eng, a);
        dnnl::memory m2(md, eng, b);
        dnnl::memory dst(md, eng, out);
        dnnl::sum prim(dnnl::sum::primitive_desc(
            eng, {1.f, 1.f}, {md, md}));
        prim.execute(strm, {{DNNL_ARG_DST, dst},
                            {DNNL_ARG_MULTIPLE_SRC + 0, m1},
                            {DNNL_ARG_MULTIPLE_SRC + 1, m2}});
        strm.wait();
        if (std::fabs(out[0] - 3.f) > 1e-6f ||
            std::fabs(out[3] - 3.f) > 1e-6f) {
            return "dnnl::sum returned wrong values";
        }
        return {};
    } catch (const dnnl::error& e) {
        return std::string("dnnl::error: ") + e.what();
    }
}
#endif

#if defined(XER_VE_NN_GGML)
std::string TestGgml() {
    // ggml_backend_load_all() dlopens backend modules (ggml-cpu-*.dll
    // & friends) from beside the exe — the build stages them into the
    // bin dir. With none reachable, there is nothing to compute on.
    ggml_backend_load_all();
    const int dev_count = ggml_backend_dev_count();
    if (dev_count == 0) {
        return "no ggml backend modules found "
               "(ggml-cpu-*.dll not beside the exe)";
    }

    // [1,1,1,1] + [2,2,2,2] = [3,...] — a 4-float add graph. The
    // cgraph lives in the context pool too: ggml_new_graph allocates
    // GGML_DEFAULT_GRAPH_SIZE nodes' worth of storage up front, so
    // the pool must cover tensor overhead AND ggml_graph_overhead() —
    // an undersized pool returns a null cgraph which
    // ggml_build_forward_expand then dereferences.
    const ggml_init_params ip = {
        /*.mem_size   =*/ 16 * 1024 + ggml_graph_overhead(),
        /*.mem_buffer =*/ nullptr,
        /*.no_alloc   =*/ true};
    ggml_context* ctx = ggml_init(ip);
    if (ctx == nullptr) {
        return "ggml_init failed";
    }
    ggml_tensor* ta = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 4);
    ggml_tensor* tb = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 4);
    ggml_tensor* tc =
        (ta != nullptr && tb != nullptr) ? ggml_add(ctx, ta, tb)
                                         : nullptr;
    ggml_cgraph* gf =
        tc != nullptr ? ggml_new_graph(ctx) : nullptr;
    if (gf == nullptr) {
        ggml_free(ctx);
        return "ggml context could not hold a tiny add graph";
    }
    ggml_build_forward_expand(gf, tc);

    // Device 0 is NOT a safe default — backend load order is
    // arbitrary and accelerator backends do not implement every op
    // (ggml-blas aborts inside graph_compute on elementwise ADD —
    // GGML_ABORT, uncatchable). Prefer a CPU device for universal
    // coverage; otherwise take the first device that reports op
    // support via ggml_backend_dev_supports_op.
    ggml_backend_dev_t dev = ggml_backend_dev_by_type(
        GGML_BACKEND_DEVICE_TYPE_CPU);
    for (int i = 0; dev == nullptr && i < dev_count; ++i) {
        ggml_backend_dev_t candidate = ggml_backend_dev_get(i);
        if (candidate != nullptr &&
            ggml_backend_dev_supports_op(candidate, tc)) {
            dev = candidate;
        }
    }
    if (dev == nullptr) {
        ggml_free(ctx);
        return "no ggml device supports the add op";
    }
    ggml_backend_t backend = ggml_backend_dev_init(dev, nullptr);
    if (backend == nullptr) {
        ggml_free(ctx);
        return "ggml_backend_dev_init failed";
    }
    if (!ggml_backend_supports_op(backend, tc)) {
        ggml_free(ctx);
        ggml_backend_free(backend);
        return "picked ggml backend cannot run elementwise add";
    }

    std::string fail;
    ggml_backend_buffer_t buf =
        ggml_backend_alloc_ctx_tensors(ctx, backend);
    if (buf == nullptr) {
        fail = "ggml_backend_alloc_ctx_tensors failed";
    } else {
        const float av[4] = {1.f, 1.f, 1.f, 1.f};
        const float bv[4] = {2.f, 2.f, 2.f, 2.f};
        ggml_backend_tensor_set(ta, av, 0, sizeof(av));
        ggml_backend_tensor_set(tb, bv, 0, sizeof(bv));
        if (ggml_backend_graph_compute(backend, gf) != GGML_STATUS_SUCCESS) {
            fail = "ggml_backend_graph_compute failed";
        } else {
            float cv[4] = {0.f, 0.f, 0.f, 0.f};
            ggml_backend_tensor_get(tc, cv, 0, sizeof(cv));
            if (std::fabs(cv[0] - 3.f) > 1e-6f ||
                std::fabs(cv[3] - 3.f) > 1e-6f) {
                fail = "ggml add returned wrong values";
            }
        }
    }

    if (buf != nullptr) ggml_backend_buffer_free(buf);
    ggml_free(ctx);
    ggml_backend_free(backend);
    return fail;
}
#endif

#if defined(_WIN32)
// LoadLibrary-probe each "|"-separated delay-import DLL before the
// first call into it. The delay-load binder raises 0xc06d007e
// (FACILITY_VISUALCPP / ERROR_MOD_NOT_FOUND) on a missing or
// unloadable module — structured, not a C++ exception, so the smoke
// cannot catch it. Probing converts "link-time present, runtime
// broken" into a reportable verdict. LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
// resolves the exe dir first, exactly like the binder.
std::string ProbeDelayloadDlls(const char* pipe_list) {
    if (pipe_list == nullptr || *pipe_list == '\0') return {};
    std::string rest(pipe_list);
    while (true) {
        const std::size_t sep = rest.find('|');
        const std::string name = rest.substr(0, sep);
        const HMODULE h = LoadLibraryExA(
            name.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (h == nullptr) {
            return name + " will not load (Win32 error " +
                   std::to_string(GetLastError()) + ")";
        }
        FreeLibrary(h);   // probe only — the binder resolves for real
        if (sep == std::string::npos) break;
        rest = rest.substr(sep + 1);
    }
    return {};
}
#endif

}  // namespace

std::vector<NnBackend> NnBackends() {
    return BuildMatrix();
}

std::string NnBackendsReport() {
    std::ostringstream out;
    out << "NN backends (XER_WITH_* / XER_VE_NN_* build probe):\n";
    for (const NnBackend& b : BuildMatrix()) {
        out << "  " << b.id;
        // Pad short ids so the status column lines up.
        for (std::size_t n = std::strlen(b.id); n < 12; ++n) out << ' ';
        if (b.compiled_in) {
            out << ": available";
            if (!b.version.empty()) out << " " << b.version;
        } else {
            out << ": unavailable";
        }
        if (!b.detail.empty()) out << " — " << b.detail;
        out << '\n';
    }
    return out.str();
}

std::string NnBackendSelfTest(const char* id) {
    if (id == nullptr) return "null id";
    const std::string s(id);
#if defined(XER_VE_NN_OPENCV)
    if (s == "opencv") return TestOpenCV();
#endif
#if defined(XER_VE_NN_ONNXRUNTIME)
    if (s == "onnxruntime") return TestOnnxRuntime();
#endif
#if defined(XER_VE_NN_DNNL)
    if (s == "dnnl") return TestDnnl();
#endif
#if defined(XER_VE_NN_GGML)
    if (s == "ggml") return TestGgml();
#endif
    return s + " not compiled into this build";
}

std::string NnBackendRuntimeProbe(const char* id) {
#if defined(_WIN32)
    if (id == nullptr) return "null id";
    const std::string s(id);
    // The XER_VE_NN_*_DLLS lists are the exact delay-import names
    // CMake collected from the linked targets — same names the binder
    // will try, probed the safe way first.
#if defined(XER_VE_NN_OPENCV) && defined(XER_VE_NN_OPENCV_DLLS)
    if (s == "opencv") return ProbeDelayloadDlls(XER_VE_NN_OPENCV_DLLS);
#endif
#if defined(XER_VE_NN_ONNXRUNTIME) && \
    defined(XER_VE_NN_ONNXRUNTIME_DLLS)
    if (s == "onnxruntime") {
        return ProbeDelayloadDlls(XER_VE_NN_ONNXRUNTIME_DLLS);
    }
#endif
#if defined(XER_VE_NN_DNNL) && defined(XER_VE_NN_DNNL_DLLS)
    if (s == "dnnl") return ProbeDelayloadDlls(XER_VE_NN_DNNL_DLLS);
#endif
#if defined(XER_VE_NN_GGML) && defined(XER_VE_NN_GGML_DLLS)
    if (s == "ggml") return ProbeDelayloadDlls(XER_VE_NN_GGML_DLLS);
#endif
#else
    static_cast<void>(id);
#endif
    return {};   // nothing to probe: loadable or not delay-imported
}

}  // namespace XER::VideoEditor
