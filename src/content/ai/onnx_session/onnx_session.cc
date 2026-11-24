// Implementation of onnx_session.h — the dual-backend ONNX session
// shared by find_object (detection) and the video-editor lab ops
// (matting / promptable segmentation). The delay-load probe story is
// identical to find_object's: EVERY code path that can reach a
// backend symbol first passes through ProbeDlls(), which
// LoadLibrary's the configured runtime-DLL list — a first call into a
// missing DLL hard-kills the process under MSYS2 (the uncatchable GNU
// delay-load helper), so the probe must run first.
//
// Backend defines: this TU ships inside the AIFindObject module,
// which injects XER_AI_FO_* defines — map them onto the generic
// XER_AI_ONNX_* names so a future standalone target can inject the
// new names directly.
#if !defined(XER_AI_ONNX_ONNXRUNTIME) && defined(XER_AI_FO_ONNXRUNTIME)
#define XER_AI_ONNX_ONNXRUNTIME 1
#endif
#if !defined(XER_AI_ONNX_OPENCV) && defined(XER_AI_FO_OPENCV)
#define XER_AI_ONNX_OPENCV 1
#endif
#if !defined(XER_AI_ONNX_ORT_DLLS) && defined(XER_AI_FO_ORT_DLLS)
#define XER_AI_ONNX_ORT_DLLS XER_AI_FO_ORT_DLLS
#endif
#if !defined(XER_AI_ONNX_OPENCV_DLLS) && defined(XER_AI_FO_OPENCV_DLLS)
#define XER_AI_ONNX_OPENCV_DLLS XER_AI_FO_OPENCV_DLLS
#endif

#include "onnx_session.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(XER_AI_ONNX_ONNXRUNTIME)
#include <onnxruntime_c_api.h>
#endif

#if defined(XER_AI_ONNX_OPENCV)
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace XER::AI::Onnx {
namespace {

// ---- runtime-DLL probes ----------------------------------------------

// '|'-separated delay-load target names resolved at configure time by
// xer_collect_delayload_dlls() and injected as compile defines.
#ifndef XER_AI_ONNX_ORT_DLLS
#define XER_AI_ONNX_ORT_DLLS ""
#endif
#ifndef XER_AI_ONNX_OPENCV_DLLS
#define XER_AI_ONNX_OPENCV_DLLS ""
#endif

#if defined(_WIN32)
// UTF-8 -> wide path for the Win32/ORT APIs. std::filesystem::path
// char-ctor converts via the ANSI codepage on Windows, which would
// mangle non-ASCII model paths — all XER paths are UTF-8.
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        utf8.data(),
                                        static_cast<int>(utf8.size()),
                                        nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                          static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

// Returns "" when every listed module loaded, else the first failing
// name. Loaded modules stay resident on purpose.
std::string ProbeDlls(const std::string& names) {
    std::size_t pos = 0;
    while (true) {
        const std::size_t bar = names.find('|', pos);
        const std::string name =
            names.substr(pos, bar == std::string::npos
                                  ? std::string::npos
                                  : bar - pos);
        if (!name.empty()) {
            const std::wstring wname(name.begin(), name.end());
            if (::LoadLibraryW(wname.c_str()) == nullptr) {
                return name;
            }
        }
        if (bar == std::string::npos) break;
        pos = bar + 1;
    }
    return {};
}
#endif

// loadable flag for one backend — cached: the probe outcome does not
// change mid-session (a DLL that appeared later is picked up anyway
// since LoadLibrary re-runs per call is cheap enough to leave live).
bool BackendLoadable(const char* dlls) {
#if defined(_WIN32)
    static std::map<std::string, bool> cache;
    const std::string key(dlls != nullptr ? dlls : "");
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    if (key.empty()) return (cache[key] = true);   // nothing to probe
    const bool ok = ProbeDlls(key).empty();
    return (cache[key] = ok);
#else
    (void)dlls;
    return true;   // ELF imports resolve at load — linked == loadable
#endif
}

std::string FirstProbeFailure(const char* dlls) {
#if defined(_WIN32)
    return ProbeDlls(dlls != nullptr ? dlls : "");
#else
    (void)dlls;
    return {};
#endif
}

// ---- ONNX model header probe -------------------------------------------
//
// Minimal protobuf-wire walk of the .onnx header to recover the
// declared input shape: ModelProto.graph (field 7) ->
// GraphProto.input (field 11) -> ValueInfoProto.type.tensor_type.shape
// -> dim_value entries. cv::dnn cannot report the declared shape
// portably, and ORT reports symbolic dims as unusable values, so both
// backends share this probe.

bool PbReadVarint(const std::uint8_t*& p, const std::uint8_t* end,
                  std::uint64_t* out) {
    std::uint64_t v = 0;
    int shift = 0;
    while (p < end && shift < 64) {
        const std::uint8_t b = *p++;
        v |= static_cast<std::uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            *out = v;
            return true;
        }
        shift += 7;
    }
    return false;
}

// Iterates top-level fields of a protobuf message [b,e). cb(field_no,
// wire_type, payload_ptr, payload_len) — wire_type 0 payloads are the
// varint bytes themselves (not a sub-message); callers re-decode.
template <typename F>
void PbForEachField(const std::uint8_t* b, const std::uint8_t* e,
                    F&& cb) {
    const std::uint8_t* p = b;
    while (p < e) {
        std::uint64_t key = 0;
        if (!PbReadVarint(p, e, &key)) return;
        const int field = static_cast<int>(key >> 3);
        const int wt = static_cast<int>(key & 7);
        if (wt == 0) {
            const std::uint8_t* start = p;
            std::uint64_t v = 0;
            if (!PbReadVarint(p, e, &v)) return;
            cb(field, wt, start, p - start);
        } else if (wt == 1) {
            if (e - p < 8) return;
            cb(field, wt, p, 8);
            p += 8;
        } else if (wt == 2) {
            std::uint64_t len = 0;
            if (!PbReadVarint(p, e, &len) ||
                len > static_cast<std::uint64_t>(e - p)) {
                return;
            }
            cb(field, wt, p, static_cast<std::size_t>(len));
            p += len;
        } else if (wt == 5) {
            if (e - p < 4) return;
            cb(field, wt, p, 4);
            p += 4;
        } else {
            return;   // groups/wire types we do not meet in ONNX
        }
    }
}

// Parses a TensorShapeProto into dim ints; -1 marks a symbolic/
// dynamic dim. shape = TensorShapeProto message bytes.
std::vector<std::int64_t> PbParseShape(const std::uint8_t* b,
                                       const std::uint8_t* e) {
    std::vector<std::int64_t> dims;
    PbForEachField(b, e, [&](int field, int wt, const std::uint8_t* d,
                             std::size_t len) {
        if (field != 1 || wt != 2) return;   // dim
        std::int64_t dim = -1;
        PbForEachField(d, d + len,
                       [&](int df, int dwt, const std::uint8_t* dd,
                           std::size_t dlen) {
                           if (df == 1 && dwt == 0) {   // dim_value
                               const std::uint8_t* q = dd;
                               std::uint64_t v = 0;
                               if (PbReadVarint(q, dd + dlen, &v)) {
                                   dim = static_cast<std::int64_t>(v);
                               }
                           }
                       });
        dims.push_back(dim);
    });
    return dims;
}

// value_info -> (dims vector) via ValueInfoProto.type (2) ->
// TypeProto.tensor_type (1) -> Tensor.shape (2).
std::vector<std::int64_t> PbValueInfoDims(const std::uint8_t* b,
                                          const std::uint8_t* e) {
    std::vector<std::int64_t> dims;
    PbForEachField(b, e, [&](int field, int wt, const std::uint8_t* d,
                             std::size_t len) {
        if (field != 2 || wt != 2) return;   // type
        PbForEachField(d, d + len,
                       [&](int tf, int twt, const std::uint8_t* td,
                           std::size_t tlen) {
                           if (tf != 1 || twt != 2) return;  // tensor_type
                           PbForEachField(
                               td, td + tlen,
                               [&](int ef, int ewt, const std::uint8_t* ed,
                                   std::size_t elen) {
                                   if (ef == 2 && ewt == 2) {   // shape
                                       dims = PbParseShape(ed, ed + elen);
                                   }
                               });
                       });
    });
    return dims;
}

// ---- backend sessions ---------------------------------------------------

// One loaded model on one backend. Run() takes named f32 input
// tensors and fills `outs_out` with every f32 output tensor
// (borrowed — valid until the next Run or destruction).
struct SessionImpl {
    virtual ~SessionImpl() = default;
    virtual bool Run(const std::vector<Input>& inputs,
                     std::vector<TensorView>* outs_out,
                     std::string* error_out) = 0;
    std::vector<std::string> input_names;
    std::vector<std::vector<std::int64_t>> input_dims;
    std::vector<std::string> output_names;
};

// Resolves the binding for Run(): the i-th provided input takes the
// declared name at position i when its `name` is empty. Returns the
// resolved names (same order as `inputs`) or false with an error when
// a name cannot be resolved.
bool ResolveInputNames(const SessionImpl& s,
                       const std::vector<Input>& inputs,
                       std::vector<std::string>* resolved,
                       std::string* error_out) {
    resolved->clear();
    resolved->reserve(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!inputs[i].name.empty()) {
            resolved->push_back(inputs[i].name);
        } else if (i < s.input_names.size()) {
            resolved->push_back(s.input_names[i]);
        } else {
            if (error_out) {
                *error_out =
                    "input " + std::to_string(i) +
                    " has no name and the session cannot position-bind";
            }
            return false;
        }
    }
    return true;
}

#if defined(XER_AI_ONNX_ONNXRUNTIME)
// NOTE: the wrapper is named OrtSessionImpl — calling it OrtSession
// would shadow the real ::OrtSession C type inside its own scope
// (injected-class-name), turning `OrtSession* session` into a pointer
// to the wrapper and breaking every api->*Session* call.
struct OrtSessionImpl final : SessionImpl {
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtSessionOptions* opts = nullptr;
    OrtAllocator* alloc = nullptr;
    OrtMemoryInfo* meminfo = nullptr;
    std::vector<const char*> output_name_ptrs;
    std::vector<OrtValue*> outputs;
    std::vector<char*> owned_names;
    ~OrtSessionImpl() override {
        if (api == nullptr) return;
        for (OrtValue* v : outputs) api->ReleaseValue(v);
        for (char* p : owned_names) alloc->Free(alloc, p);
        if (meminfo) api->ReleaseMemoryInfo(meminfo);
        if (session) api->ReleaseSession(session);
        if (opts) api->ReleaseSessionOptions(opts);
        if (env) api->ReleaseEnv(env);
    }
    // Keep ORT status text alive for the caller.
    std::string Check(OrtStatus* st, const char* what,
                      std::string* error_out) {
        if (st == nullptr) return {};
        std::string msg =
            std::string(what) + ": " + api->GetErrorMessage(st);
        api->ReleaseStatus(st);
        if (error_out && error_out->empty()) *error_out = msg;
        return msg;
    }

    bool Run(const std::vector<Input>& inputs,
             std::vector<TensorView>* outs_out,
             std::string* error_out) override {
        if (inputs.empty()) {
            if (error_out) *error_out = "Run called with no inputs";
            return false;
        }
        std::vector<std::string> resolved;
        if (!ResolveInputNames(*this, inputs, &resolved, error_out)) {
            return false;
        }
        std::vector<const char*> in_name_ptrs(inputs.size());
        std::vector<OrtValue*> in_vals(inputs.size(), nullptr);
        const auto release_inputs = [&] {
            for (OrtValue* v : in_vals) {
                if (v != nullptr) api->ReleaseValue(v);
            }
        };
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const Input& in = inputs[i];
            std::size_t count = 1;
            for (const std::int64_t d : in.dims) {
                // Zero-size dims bind as EMPTY tensors — legal ONNX
                // (SAM's decoder contract feeds empty input_points /
                // input_boxes for the prompt kind not in use).
                if (d < 0) {
                    if (error_out) {
                        *error_out = "input '" + resolved[i] +
                                     "' has a negative dim";
                    }
                    release_inputs();
                    return false;
                }
                count *= static_cast<std::size_t>(d);
            }
            if (in.data == nullptr && in.data_i64 == nullptr) {
                if (error_out) {
                    *error_out =
                        "input '" + resolved[i] + "' has null data";
                }
                release_inputs();
                return false;
            }
            in_name_ptrs[i] = resolved[i].c_str();
            // INT64 inputs (silero's scalar `sr`, tokenized ids)
            // take the integer binding; everything else is f32.
            const bool i64 = in.data_i64 != nullptr;
            void* const tensor_data =
                i64 ? static_cast<void*>(
                          const_cast<std::int64_t*>(in.data_i64))
                    : static_cast<void*>(
                          const_cast<float*>(in.data));
            OrtStatus* st = api->CreateTensorWithDataAsOrtValue(
                meminfo, tensor_data,
                (i64 ? sizeof(std::int64_t) : sizeof(float)) * count,
                in.dims.data(), in.dims.size(),
                i64 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
                    : ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &in_vals[i]);
            if (!Check(st, "CreateTensorWithDataAsOrtValue", error_out)
                     .empty() ||
                in_vals[i] == nullptr) {
                release_inputs();
                return false;
            }
        }
        for (OrtValue* v : outputs) api->ReleaseValue(v);
        outputs.assign(output_name_ptrs.size(), nullptr);
        OrtStatus* st = api->Run(
            session, nullptr, in_name_ptrs.data(), in_vals.data(),
            in_vals.size(), output_name_ptrs.data(),
            output_name_ptrs.size(), outputs.data());
        release_inputs();
        if (!Check(st, "Run", error_out).empty()) return false;
        if (outputs.empty() || outputs[0] == nullptr) {
            if (error_out) *error_out = "Run returned no outputs";
            return false;
        }
        outs_out->clear();
        outs_out->reserve(outputs.size());
        for (std::size_t oi = 0; oi < outputs.size(); ++oi) {
            if (outputs[oi] == nullptr) continue;
            OrtTensorTypeAndShapeInfo* info = nullptr;
            st = api->GetTensorTypeAndShape(outputs[oi], &info);
            if (!Check(st, "GetTensorTypeAndShapeInfo", error_out)
                     .empty() ||
                info == nullptr) {
                return false;
            }
            ONNXTensorElementDataType elem =
                ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
            std::size_t ndim = 0;
            TensorView view;
            view.name =
                oi < output_names.size() ? output_names[oi]
                                         : std::string();
            bool bad = false;
            bad = bad ||
                  !Check(api->GetTensorElementType(info, &elem),
                         "GetTensorElementType", error_out)
                      .empty();
            bad = bad ||
                  !Check(api->GetDimensionsCount(info, &ndim),
                         "GetDimensionsCount", error_out)
                      .empty();
            view.dims.assign(ndim, 0);
            bad = bad ||
                  !Check(api->GetDimensions(info, view.dims.data(),
                                            ndim),
                         "GetDimensions", error_out)
                      .empty();
            api->ReleaseTensorTypeAndShapeInfo(info);
            if (bad) return false;
            if (elem != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                continue;   // consumers only read f32 outputs
            }
            void* out_ptr = nullptr;
            st = api->GetTensorMutableData(outputs[oi], &out_ptr);
            if (!Check(st, "GetTensorMutableData", error_out).empty() ||
                out_ptr == nullptr) {
                return false;
            }
            view.data = static_cast<const float*>(out_ptr);
            outs_out->push_back(std::move(view));
        }
        return !outs_out->empty();
    }
};

std::unique_ptr<SessionImpl> MakeOrtSession(
    const std::string& model_path, std::string* error_out) {
    auto s = std::make_unique<OrtSessionImpl>();
    const OrtApiBase* base = OrtGetApiBase();
    if (base == nullptr) {
        if (error_out) *error_out = "OrtGetApiBase returned null";
        return nullptr;
    }
    s->api = base->GetApi(ORT_API_VERSION);
    if (s->api == nullptr) {
        if (error_out) *error_out = "OrtApi unavailable";
        return nullptr;
    }
    const OrtApi* api = s->api;
    OrtStatus* st = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                                   "xer_ai_onnx_session", &s->env);
    if (!s->Check(st, "CreateEnv", error_out).empty() ||
        s->env == nullptr) {
        return nullptr;
    }
    st = api->CreateSessionOptions(&s->opts);
    if (!s->Check(st, "CreateSessionOptions", error_out).empty() ||
        s->opts == nullptr) {
        return nullptr;
    }
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    // Non-fatal tuning knobs: route through Check (releases the
    // status) but keep going with runtime defaults when they fail.
    (void)s->Check(api->SetIntraOpNumThreads(
                       s->opts, static_cast<int>(std::min(4u, hw))),
                   "SetIntraOpNumThreads", nullptr);
    (void)s->Check(api->SetSessionGraphOptimizationLevel(
                       s->opts, ORT_ENABLE_ALL),
                   "SetSessionGraphOptimizationLevel", nullptr);
#if defined(_WIN32)
    const std::wstring wpath = Utf8ToWide(model_path);
    const ORTCHAR_T* session_path = wpath.c_str();
#else
    const ORTCHAR_T* session_path = model_path.c_str();
#endif
    st = api->CreateSession(s->env, session_path, s->opts, &s->session);
    if (!s->Check(st, "CreateSession", error_out).empty() ||
        s->session == nullptr) {
        return nullptr;
    }
    st = api->GetAllocatorWithDefaultOptions(&s->alloc);
    if (!s->Check(st, "GetAllocatorWithDefaultOptions", error_out)
             .empty() ||
        s->alloc == nullptr) {
        return nullptr;
    }
    st = api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                  &s->meminfo);
    if (!s->Check(st, "CreateCpuMemoryInfo", error_out).empty() ||
        s->meminfo == nullptr) {
        return nullptr;
    }
    // ALL input names + declared dims (dynamic dims report <= 0).
    std::size_t n_in = 0, n_out = 0;
    if (!s->Check(api->SessionGetInputCount(s->session, &n_in),
                  "SessionGetInputCount", error_out)
             .empty() ||
        n_in < 1) {
        if (error_out && error_out->empty())
            *error_out = "model has no inputs";
        return nullptr;
    }
    for (std::size_t i = 0; i < n_in; ++i) {
        char* name = nullptr;
        st = api->SessionGetInputName(s->session, i, s->alloc, &name);
        if (!s->Check(st, "SessionGetInputName", error_out).empty() ||
            name == nullptr) {
            return nullptr;
        }
        s->input_names.emplace_back(name);
        s->owned_names.push_back(name);
        std::vector<std::int64_t> dims;
        OrtTypeInfo* ti = nullptr;
        st = api->SessionGetInputTypeInfo(s->session, i, &ti);
        if (s->Check(st, "SessionGetInputTypeInfo", error_out).empty() &&
            ti != nullptr) {
            const OrtTensorTypeAndShapeInfo* info = nullptr;
            if (api->CastTypeInfoToTensorInfo(ti, &info) == nullptr &&
                info != nullptr) {
                std::size_t nd = 0;
                if (api->GetDimensionsCount(info, &nd) == nullptr &&
                    nd > 0) {
                    dims.assign(nd, -1);
                    (void)s->Check(
                        api->GetDimensions(info, dims.data(), nd),
                        "GetDimensions", nullptr);
                }
            }
            api->ReleaseTypeInfo(ti);
        }
        s->input_dims.push_back(std::move(dims));
    }
    if (!s->Check(api->SessionGetOutputCount(s->session, &n_out),
                  "SessionGetOutputCount", error_out)
             .empty() ||
        n_out < 1) {
        if (error_out && error_out->empty())
            *error_out = "model has no outputs";
        return nullptr;
    }
    for (std::size_t i = 0; i < n_out; ++i) {
        char* oname = nullptr;
        st = api->SessionGetOutputName(s->session, i, s->alloc, &oname);
        if (!s->Check(st, "SessionGetOutputName", error_out).empty() ||
            oname == nullptr) {
            return nullptr;
        }
        s->output_names.emplace_back(oname);
        s->owned_names.push_back(oname);
    }
    for (const std::string& o : s->output_names) {
        s->output_name_ptrs.push_back(o.c_str());
    }
    return s;
}
#endif  // XER_AI_ONNX_ONNXRUNTIME

#if defined(XER_AI_ONNX_OPENCV)
struct CvSession final : SessionImpl {
    cv::dnn::Net net;
    std::vector<std::string> out_names;
    std::vector<cv::Mat> outs;
    bool Run(const std::vector<Input>& inputs,
             std::vector<TensorView>* outs_out,
             std::string* error_out) override {
        try {
            std::vector<std::string> resolved;
            if (!ResolveInputNames(*this, inputs, &resolved,
                                   error_out)) {
                return false;
            }
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                const Input& in = inputs[i];
                if ((in.data == nullptr && in.data_i64 == nullptr) ||
                    in.dims.empty()) {
                    if (error_out) *error_out = "bad input tensor";
                    return false;
                }
                std::vector<int> sz(in.dims.size());
                for (std::size_t d = 0; d < in.dims.size(); ++d) {
                    sz[d] = static_cast<int>(in.dims[d]);
                }
                cv::Mat blob;
                if (in.data_i64 != nullptr) {
                    // Integer graph inputs (silero `sr` and friends)
                    // feed as CV_64S; cv::dnn layers that cannot take
                    // integers report a normal forward() error.
                    blob = cv::Mat(static_cast<int>(sz.size()),
                                   sz.data(), CV_64S,
                                   const_cast<std::int64_t*>(
                                       in.data_i64));
                } else {
                    blob = cv::Mat(static_cast<int>(sz.size()),
                                   sz.data(), CV_32F,
                                   const_cast<float*>(in.data));
                }
                if (inputs.size() == 1) {
                    net.setInput(blob);
                } else {
                    net.setInput(blob, resolved[i]);
                }
            }
            outs.clear();
            net.forward(outs, out_names);
            outs_out->clear();
            outs_out->reserve(outs.size());
            for (std::size_t i = 0; i < outs.size(); ++i) {
                const cv::Mat& o = outs[i];
                if (o.empty() || o.type() != CV_32F) continue;
                TensorView view;
                view.name =
                    i < out_names.size() ? out_names[i] : std::string();
                view.dims.assign(o.dims, 0);
                for (int d = 0; d < o.dims; ++d) {
                    view.dims[static_cast<std::size_t>(d)] = o.size[d];
                }
                view.data = reinterpret_cast<const float*>(o.data);
                outs_out->push_back(std::move(view));
            }
            if (outs_out->empty()) {
                if (error_out) *error_out = "forward produced no f32 output";
                return false;
            }
            return true;
        } catch (const cv::Exception& e) {
            if (error_out)
                *error_out = std::string("cv::dnn: ") + e.what();
            return false;
        }
    }
};

std::unique_ptr<SessionImpl> MakeCvSession(
    const std::string& model_path, std::string* error_out) {
    auto s = std::make_unique<CvSession>();
    try {
        s->net = cv::dnn::readNetFromONNX(model_path);
        s->net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        s->net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        s->out_names = s->net.getUnconnectedOutLayersNames();
        if (s->out_names.empty()) {
            if (error_out) *error_out = "model exposes no output layers";
            return nullptr;
        }
        // Input names are best-effort: cv::dnn has no portable
        // declared-shape API, so input_dims stays empty (callers fall
        // back to ProbeInputSquare / model contracts).
#if CV_VERSION_MAJOR < 5
        // OpenCV 5 removed Net::getInputNames outright — a compile-time
        // absence no runtime try/catch can cover.
        try {
            for (const cv::String& n : s->net.getInputNames()) {
                s->input_names.emplace_back(n);
            }
        } catch (const cv::Exception&) {
            // Older dnn builds lack getInputNames — single-input
            // models still work via the unnamed setInput path.
        }
#endif
        s->output_names = s->out_names;
        return s;
    } catch (const cv::Exception& e) {
        if (error_out)
            *error_out = std::string("readNetFromONNX: ") + e.what();
        return nullptr;
    }
}
#endif  // XER_AI_ONNX_OPENCV

}  // namespace

std::vector<BackendInfo> Backends() {
    std::vector<BackendInfo> rows;
    rows.reserve(2);
    {
        BackendInfo b;
        b.id = "onnxruntime";
#if defined(XER_AI_ONNX_ONNXRUNTIME)
        b.linked = true;
        b.loadable = BackendLoadable(XER_AI_ONNX_ORT_DLLS);
        if (b.loadable) {
            b.detail = "ORT C API; onnxruntime.dll delay-loaded + "
                       "LoadLibrary-probed; CPU EP (MSYS2 ORT has no "
                       "DirectML provider)";
        } else {
            const std::string miss =
                FirstProbeFailure(XER_AI_ONNX_ORT_DLLS);
            b.detail = "runtime probe failed" +
                       (miss.empty() ? std::string()
                                     : ": cannot load " + miss +
                                           " (missing or broken "
                                           "dependency closure)");
        }
#else
        b.detail = "not linked — onnxruntime not found at configure time";
#endif
        rows.push_back(std::move(b));
    }
    {
        BackendInfo b;
        b.id = "opencv_dnn";
#if defined(XER_AI_ONNX_OPENCV)
        b.linked = true;
        b.loadable = BackendLoadable(XER_AI_ONNX_OPENCV_DLLS);
        if (b.loadable) {
            b.detail = "cv::dnn::readNetFromONNX; libopencv_*-*.dll "
                       "delay-loaded + LoadLibrary-probed";
        } else {
            const std::string miss =
                FirstProbeFailure(XER_AI_ONNX_OPENCV_DLLS);
            b.detail = "runtime probe failed" +
                       (miss.empty() ? std::string()
                                     : ": cannot load " + miss);
        }
#else
        b.detail = "not linked — OpenCV dnn module not found at "
                   "configure time";
#endif
        rows.push_back(std::move(b));
    }
    return rows;
}

std::string BackendReport() {
    std::ostringstream out;
    out << "ONNX session backends (YA.AIFindObject):\n";
    for (const BackendInfo& b : Backends()) {
        out << "  " << b.id;
        for (std::size_t n = std::strlen(b.id); n < 14; ++n) out << ' ';
        if (!b.linked) {
            out << ": not linked";
        } else {
            out << (b.loadable ? ": available" : ": linked, unloadable");
        }
        if (!b.detail.empty()) out << " — " << b.detail;
        out << '\n';
    }
    return out.str();
}

std::string ActiveBackendId() {
    for (const BackendInfo& b : Backends()) {
        if (b.linked && b.loadable) return b.id;
    }
    return {};
}

int ProbeInputSquare(const std::string& utf8_path) {
    // Only the header matters, but the graph is a single length-
    // delimited field so the whole file is read. Called once per
    // Session consumer load.
    std::ifstream in{std::filesystem::path(utf8_path),
                     std::ios::binary};
    if (!in) return 0;
    std::vector<std::uint8_t> buf(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    const std::uint8_t* b = buf.data();
    const std::uint8_t* e = b + buf.size();
    const std::uint8_t* graph = nullptr;
    std::size_t graph_len = 0;
    PbForEachField(b, e, [&](int field, int wt, const std::uint8_t* d,
                             std::size_t len) {
        if (field == 7 && wt == 2) {   // ModelProto.graph
            graph = d;
            graph_len = len;
        }
    });
    if (graph == nullptr) return 0;
    std::vector<std::vector<std::int64_t>> input_dims;
    PbForEachField(graph, graph + graph_len,
                   [&](int field, int wt, const std::uint8_t* d,
                       std::size_t len) {
                       if (field == 11 && wt == 2) {   // graph.input
                           input_dims.push_back(
                               PbValueInfoDims(d, d + len));
                       }
                   });
    int fallback = 0;
    for (const auto& dims : input_dims) {
        if (dims.size() != 4 || dims[2] <= 0 || dims[2] != dims[3]) {
            continue;
        }
        if (dims[1] == 3) {   // prefer a 3-channel image input
            return static_cast<int>(dims[2]);
        }
        if (fallback == 0) fallback = static_cast<int>(dims[2]);
    }
    return fallback;
}

struct Session::Impl {
    std::unique_ptr<SessionImpl> session;
    std::string backend_id;
    bool ready = false;
};

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;
Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

bool Session::Load(const std::string& utf8_model_path,
                   const std::string& backend,
                   std::string* error_out) {
    impl_->session.reset();
    impl_->ready = false;
    impl_->backend_id.clear();

    std::error_code ec;
    if (utf8_model_path.empty() ||
        !std::filesystem::is_regular_file(
            std::filesystem::path(utf8_model_path), ec)) {
        if (error_out) {
            *error_out = "model file not found: " +
                         (utf8_model_path.empty()
                              ? std::string("<empty path>")
                              : utf8_model_path);
        }
        return false;
    }

    const auto try_make = [&](const char* id, std::string* try_err)
        -> std::unique_ptr<SessionImpl> {
#if defined(XER_AI_ONNX_ONNXRUNTIME)
        if (std::strcmp(id, "onnxruntime") == 0) {
            return MakeOrtSession(utf8_model_path, try_err);
        }
#endif
#if defined(XER_AI_ONNX_OPENCV)
        if (std::strcmp(id, "opencv_dnn") == 0) {
            return MakeCvSession(utf8_model_path, try_err);
        }
#endif
        (void)id;
        return nullptr;
    };
    const auto can_use = [](const BackendInfo& b) {
        return b.linked && b.loadable;
    };

    std::string last_err;
    bool named_backend_seen = false;
    for (const BackendInfo& b : Backends()) {
        if (!backend.empty() && backend != b.id) continue;
        if (!can_use(b)) {
            if (backend == b.id) {
                named_backend_seen = true;
                last_err = std::string("backend '") + b.id +
                           "' not usable: " + b.detail;
            }
            continue;
        }
        named_backend_seen = true;
        std::string try_err;
        impl_->session = try_make(b.id, &try_err);
        if (impl_->session != nullptr) {
            impl_->backend_id = b.id;
            break;
        }
        last_err = try_err.empty() ? std::string("model rejected by ") +
                                         b.id
                                   : try_err;
    }
    if (impl_->session == nullptr) {
        if (error_out != nullptr) {
            if (!named_backend_seen) {
                *error_out = "no ONNX backend available "
                             "(ORT/OpenCV runtimes missing)";
            } else {
                *error_out =
                    last_err.empty()
                        ? "model rejected by every backend"
                        : last_err;
            }
        }
        return false;
    }
    impl_->ready = true;
    return true;
}

bool Session::IsLoaded() const { return impl_->ready; }
const std::string& Session::BackendId() const {
    return impl_->backend_id;
}

const std::vector<std::string>& Session::InputNames() const {
    static const std::vector<std::string> kEmpty;
    return impl_->session ? impl_->session->input_names : kEmpty;
}

const std::vector<std::string>& Session::OutputNames() const {
    static const std::vector<std::string> kEmpty;
    return impl_->session ? impl_->session->output_names : kEmpty;
}

std::size_t Session::OutputCount() const {
    return impl_->session ? impl_->session->output_names.size() : 0;
}

std::vector<std::int64_t> Session::InputDims(std::size_t index) const {
    if (impl_->session && index < impl_->session->input_dims.size()) {
        return impl_->session->input_dims[index];
    }
    return {};
}

bool Session::Run(const std::vector<Input>& inputs,
                  std::vector<TensorView>* outs_out,
                  std::string* error_out) {
    if (!impl_->ready || impl_->session == nullptr ||
        outs_out == nullptr) {
        if (error_out) *error_out = "session not loaded";
        return false;
    }
    return impl_->session->Run(inputs, outs_out, error_out);
}

}  // namespace XER::AI::Onnx
