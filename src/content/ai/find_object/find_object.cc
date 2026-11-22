// Implementation of find_object.h — see the header for the backend and
// model contracts. Every backend block is compile-gated on its
// XER_AI_FO_<NAME> define (injected by src/content/ai/CMakeLists.txt
// when the package was found), so this TU always compiles.
//
// Delay-load safety: ORT/OpenCV imports in this module are linked
// --delayload. A first call into a missing DLL hard-kills the process
// under MSYS2 (exit 127 — the uncatchable GNU delay-load helper), so
// EVERY code path that can reach a backend symbol first passes through
// ProbeDlls(), which LoadLibrary's the configured runtime-DLL list.
// A DLL that loads stays resident; the delay-import binder then binds
// to it directly. An unloadable DLL (absent deps — e.g. a stale
// onnxruntime.dll built against a since-bumped libabsl) just fails the
// probe and the backend reports loadable:false.
#include "find_object.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(XER_AI_FO_ONNXRUNTIME)
#include <onnxruntime_c_api.h>
#endif

#if defined(XER_AI_FO_OPENCV)
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include "video/frame.h"

namespace XER::AI::FindObject {
namespace {

// ---- runtime-DLL probes ----------------------------------------------

// '|'-separated delay-load target names resolved at configure time by
// xer_collect_delayload_dlls() and injected as compile defines.
#ifndef XER_AI_FO_ORT_DLLS
#define XER_AI_FO_ORT_DLLS ""
#endif
#ifndef XER_AI_FO_OPENCV_DLLS
#define XER_AI_FO_OPENCV_DLLS ""
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
// since LoadLibrary re-runs per call is cheap enough to leave live;
// keep it live — a model dir can be dropped in at run time and the DLL
// set is per-build).
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

// ---- preprocessing ----------------------------------------------------

// Letterbox + BGRA->RGB + /255 into a NCHW f32 buffer sized
// 3*in_w*in_h. Returns the letterbox scale and x/y padding in input
// pixels so detections map back to source pixels.
struct Letterbox {
    float scale = 1.0f;   // source -> input scale
    float pad_x = 0.0f;
    float pad_y = 0.0f;
};

// out must already be sized 3*in_w*in_h. Fills with 114/255 (YOLO's
// letterbox gray), then writes the scaled image centered.
Letterbox BgraToNchw(const std::uint8_t* bgra, int w, int h, int stride,
                     int in_w, int in_h, float* out) {
    Letterbox lb;
    const float kPad = 114.0f / 255.0f;
    std::fill(out, out + static_cast<std::size_t>(3) * in_w * in_h, kPad);
    lb.scale = std::min(static_cast<float>(in_w) / w,
                        static_cast<float>(in_h) / h);
    const int rw = std::max(1, static_cast<int>(std::lround(w * lb.scale)));
    const int rh = std::max(1, static_cast<int>(std::lround(h * lb.scale)));
    lb.pad_x = static_cast<float>(in_w - rw) * 0.5f;
    lb.pad_y = static_cast<float>(in_h - rh) * 0.5f;
    const int ox = static_cast<int>(std::lround(lb.pad_x));
    const int oy = static_cast<int>(std::lround(lb.pad_y));
    // Bilinear resize straight into the NCHW planes — the module has no
    // SIMD helpers, and detection is a background (AsyncOp) feature, so
    // a scalar resample is plenty.
    for (int y = 0; y < rh; ++y) {
        const float sy = (y + 0.5f) / lb.scale - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, h - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float fy = std::clamp(sy - y0, 0.0f, 1.0f);
        for (int x = 0; x < rw; ++x) {
            const float sx = (x + 0.5f) / lb.scale - 0.5f;
            const int x0 =
                std::clamp(static_cast<int>(std::floor(sx)), 0, w - 1);
            const int x1 = std::min(x0 + 1, w - 1);
            const float fx = std::clamp(sx - x0, 0.0f, 1.0f);
            const std::uint8_t* p00 = bgra + y0 * stride + x0 * 4;
            const std::uint8_t* p10 = bgra + y0 * stride + x1 * 4;
            const std::uint8_t* p01 = bgra + y1 * stride + x0 * 4;
            const std::uint8_t* p11 = bgra + y1 * stride + x1 * 4;
            // p[0]=B p[1]=G p[2]=R
            for (int c = 0; c < 3; ++c) {
                const int ch = 2 - c;   // out is R,G,B
                const float v =
                    (p00[ch] * (1.0f - fx) + p10[ch] * fx) * (1.0f - fy) +
                    (p01[ch] * (1.0f - fx) + p11[ch] * fx) * fy;
                out[static_cast<std::size_t>(c) * in_w * in_h +
                    (oy + y) * in_w + (ox + x)] = v / 255.0f;
            }
        }
    }
    return lb;
}

// ---- ONNX model header probe -------------------------------------------
//
// Minimal protobuf-wire walk of the .onnx header to recover the
// declared input shape: ModelProto.graph (field 7) ->
// GraphProto.input (field 11) -> ValueInfoProto.type.tensor_type.shape
// -> dim_value entries. cv::dnn cannot report the declared shape
// portably, and ORT reports symbolic dims as unusable values, so both
// backends share this probe. Returns the square edge H (== W) of the
// first rank-4 [N,C,H,W] graph input whose H/W are static ints — 0
// when the file cannot be parsed or every input dim is symbolic.
// Weight tensors can also appear in graph.input on IR<4 exports;
// rank-4 inputs are filtered to prefer a 3-channel one, which picks
// the image input in practice.

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

int ProbeOnnxInputSquare(const std::string& utf8_path) {
    // Only the header matters, but the graph is a single length-
    // delimited field so the whole file is read. Detector-sized files
    // only (~100 MB); called once per Detector::Load.
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

// ---- output decode + NMS ----------------------------------------------

struct RawBox {
    float cx, cy, w, h;
    int class_id;
    float score;
};

// One model output tensor, borrowed from the backend session (valid
// until the next Forward call on that session).
struct TensorView {
    std::string name;
    std::vector<std::int64_t> dims;
    const float* data = nullptr;
};

// YOLO v8-style decode: output dims [1,A,N] or [1,N,A] (a leading batch
// dim of 1 may also be absent). A = 4 + C. Boxes are cx,cy,w,h in
// INPUT pixels; scores are per-class maxes.
std::vector<RawBox> DecodeYolo(const float* data,
                               const std::vector<std::int64_t>& dims,
                               float conf_thresh) {
    std::vector<RawBox> boxes;
    if (data == nullptr || dims.size() < 2 || dims.size() > 3) {
        return boxes;
    }
    std::int64_t a = dims[dims.size() - 2];
    std::int64_t n = dims[dims.size() - 1];
    bool attr_first = true;   // [1,A,N]
    if (a > n) {
        // [1,N,A] — rows are boxes, columns attrs.
        std::swap(a, n);
        attr_first = false;
    }
    if (a < 5 || n < 1) return boxes;
    const std::int64_t classes = a - 4;
    const auto at = [&](std::int64_t attr, std::int64_t i) -> float {
        return attr_first ? data[attr * n + i] : data[i * a + attr];
    };
    boxes.reserve(static_cast<std::size_t>(std::min<std::int64_t>(n, 4096)));
    for (std::int64_t i = 0; i < n; ++i) {
        const float cx = at(0, i), cy = at(1, i);
        const float w = at(2, i), h = at(3, i);
        if (!(w > 0.0f) || !(h > 0.0f)) continue;   // NaN-safe reject
        int best = -1;
        float best_score = conf_thresh;
        for (std::int64_t c = 0; c < classes; ++c) {
            const float s = at(4 + c, i);
            if (s > best_score) {
                best_score = s;
                best = static_cast<int>(c);
            }
        }
        if (best < 0) continue;
        boxes.push_back({cx, cy, w, h, best, best_score});
    }
    return boxes;
}

// DETR-family decode (RF-DETR / RT-DETR exports, e.g.
// onnx-community/rfdetr_*-ONNX): TWO outputs instead of YOLO's one —
// a boxes tensor [1,N,4] in cx,cy,w,h and a class-logits tensor
// [1,N,C]. Boxes may be normalized to 0..1 (RF-DETR) or already in
// input pixels (some RT-DETR exports) — detected by range. Logits are
// un-activated; scores are per-class sigmoids. DETR is NMS-free by
// design but the shared NMS tail is harmless (near-zero duplicate
// overlap) and stays enabled.
//
// Pairing rule: the output whose last dim == 4 is boxes, the one with
// last dim > 4 is logits; when names are present they break ties /
// confirm ("box"/"det" vs "logit"/"label"/"score"). Returns empty on
// ambiguity — better a silent miss than garbage boxes.
std::vector<RawBox> DecodeDetr(const std::vector<TensorView>& outs,
                                 float conf_thresh, int input_size) {
    const TensorView* boxes = nullptr;
    const TensorView* logits = nullptr;
    const auto last_dim = [](const TensorView& t) -> std::int64_t {
        return t.dims.empty() ? -1 : t.dims.back();
    };
    const auto rank = [](const TensorView& t) -> std::size_t {
        return t.dims.size();
    };
    const auto name_has = [](const TensorView& t, const char* sub) {
        std::string low = t.name;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return low.find(sub) != std::string::npos;
    };
    for (const TensorView& t : outs) {
        const std::int64_t last = last_dim(t);
        const std::size_t r = rank(t);
        if (t.data == nullptr || (r != 2 && r != 3)) continue;
        if (last == 4) {
            if (boxes == nullptr) boxes = &t;
        } else if (last > 4) {
            if (logits == nullptr || name_has(t, "logit") ||
                name_has(t, "label") || name_has(t, "score") ||
                name_has(t, "cls")) {
                logits = &t;
            }
        }
    }
    // Name-guided disambiguation for the pathological C == 4 case
    // (Roboflow warns shape-matching swaps outputs when num_classes+1
    // == 4): prefer explicit name matches when both candidates exist.
    for (const TensorView& t : outs) {
        if (name_has(t, "logit") || name_has(t, "label") ||
            name_has(t, "score")) {
            logits = &t;
        } else if ((name_has(t, "box") || name_has(t, "det")) &&
                   last_dim(t) == 4) {
            boxes = &t;
        }
    }
    if (boxes == nullptr || logits == nullptr || boxes == logits) {
        return {};
    }
    const std::int64_t n = boxes->dims[rank(*boxes) - 2];
    const std::int64_t classes =
        logits->dims[rank(*logits) - 1];
    const std::int64_t n_logits =
        rank(*logits) >= 2 ? logits->dims[rank(*logits) - 2] : 0;
    if (n < 1 || classes < 1 || n_logits != n ||
        boxes->data == nullptr || logits->data == nullptr) {
        return {};
    }
    // Normalized vs input-pixel boxes: normalized cxcywh stays in
    // [0,~1.05]; anything past ~2 input px is already pixels.
    float max_coord = 0.0f;
    for (std::int64_t i = 0; i < n * 4; ++i) {
        const float v = std::fabs(boxes->data[i]);
        if (v > max_coord) max_coord = v;
    }
    const float box_scale =
        max_coord <= 2.0f ? static_cast<float>(input_size) : 1.0f;

    const auto sigmoid = [](float x) {
        return 1.0f / (1.0f + std::exp(-x));
    };
    std::vector<RawBox> out;
    out.reserve(static_cast<std::size_t>(std::min<std::int64_t>(n, 4096)));
    for (std::int64_t i = 0; i < n; ++i) {
        const float* row = logits->data + i * classes;
        int best = -1;
        float best_score = conf_thresh;
        for (std::int64_t c = 0; c < classes; ++c) {
            const float s = sigmoid(row[c]);
            if (s > best_score) {
                best_score = s;
                best = static_cast<int>(c);
            }
        }
        if (best < 0) continue;
        const float cx = boxes->data[i * 4 + 0] * box_scale;
        const float cy = boxes->data[i * 4 + 1] * box_scale;
        const float w = boxes->data[i * 4 + 2] * box_scale;
        const float h = boxes->data[i * 4 + 3] * box_scale;
        if (!(w > 0.0f) || !(h > 0.0f)) continue;
        out.push_back({cx, cy, w, h, best, best_score});
    }
    return out;
}

float IoU(const Detection& a, const Detection& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w);
    const float y1 = std::min(a.y + a.h, b.y + b.h);
    const float inter = std::max(0.0f, x1 - x0) * std::max(0.0f, y1 - y0);
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

// Shared tail: un-letterbox, clamp to source, per-class NMS, label.
std::vector<Detection> Postprocess(std::vector<RawBox> boxes,
                                   const Letterbox& lb, int src_w,
                                   int src_h,
                                   const std::vector<std::string>& labels,
                                   const Options& options) {
    std::vector<Detection> dets;
    dets.reserve(boxes.size());
    for (const RawBox& b : boxes) {
        Detection d;
        d.class_id = b.class_id;
        d.confidence = b.score;
        const float x0 = (b.cx - b.w * 0.5f - lb.pad_x) / lb.scale;
        const float y0 = (b.cy - b.h * 0.5f - lb.pad_y) / lb.scale;
        const float x1 = (b.cx + b.w * 0.5f - lb.pad_x) / lb.scale;
        const float y1 = (b.cy + b.h * 0.5f - lb.pad_y) / lb.scale;
        d.x = std::clamp(x0, 0.0f, static_cast<float>(src_w));
        d.y = std::clamp(y0, 0.0f, static_cast<float>(src_h));
        d.w = std::clamp(x1, 0.0f, static_cast<float>(src_w)) - d.x;
        d.h = std::clamp(y1, 0.0f, static_cast<float>(src_h)) - d.y;
        if (d.w <= 0.0f || d.h <= 0.0f) continue;
        d.label = b.class_id >= 0 &&
                          b.class_id < static_cast<int>(labels.size())
                      ? labels[static_cast<std::size_t>(b.class_id)]
                      : "class_" + std::to_string(b.class_id);
        dets.push_back(std::move(d));
    }
    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) {
                  return a.confidence > b.confidence;
              });
    if (options.nms_threshold > 0.0f) {
        std::vector<Detection> kept;
        kept.reserve(dets.size());
        std::vector<bool> dead(dets.size(), false);
        for (std::size_t i = 0; i < dets.size(); ++i) {
            if (dead[i]) continue;
            kept.push_back(dets[i]);
            for (std::size_t j = i + 1; j < dets.size(); ++j) {
                if (!dead[j] &&
                    dets[j].class_id == dets[i].class_id &&
                    IoU(dets[i], dets[j]) > options.nms_threshold) {
                    dead[j] = true;
                }
            }
        }
        dets.swap(kept);
    }
    if (options.max_detections > 0 &&
        dets.size() > static_cast<std::size_t>(options.max_detections)) {
        dets.resize(static_cast<std::size_t>(options.max_detections));
    }
    return dets;
}

// ---- backend sessions ---------------------------------------------------

// One loaded model on one backend. Detect() takes the letterboxed NCHW
// input and must fill `outs_out` with every f32 output tensor
// (borrowed — valid until the next Forward or destruction). Single-
// output models are YOLO-decoded; multi-output models go through the
// DETR boxes+logits decoder.
struct Session {
    virtual ~Session() = default;
    virtual bool Forward(const float* nchw, int in_w, int in_h,
                         std::vector<TensorView>* outs_out,
                         std::string* error_out) = 0;
    // Declared output count — used at Load to pick an input-size
    // fallback when the model dims are symbolic.
    virtual int OutputCount() const = 0;
};

#if defined(XER_AI_FO_ONNXRUNTIME)
// NOTE: the wrapper is named OrtSessionImpl — calling it OrtSession
// would shadow the real ::OrtSession C type inside its own scope
// (injected-class-name), turning `OrtSession* session` into a pointer
// to the wrapper and breaking every api->*Session* call.
struct OrtSessionImpl final : Session {
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtSessionOptions* opts = nullptr;
    OrtAllocator* alloc = nullptr;
    OrtMemoryInfo* meminfo = nullptr;
    std::string input_name;
    std::vector<std::string> output_names;
    std::vector<const char*> output_name_ptrs;
    std::vector<OrtValue*> outputs;
    ~OrtSessionImpl() override {
        if (api == nullptr) return;
        for (OrtValue* v : outputs) api->ReleaseValue(v);
        for (char* p : owned_names) alloc->Free(alloc, p);
        if (meminfo) api->ReleaseMemoryInfo(meminfo);
        if (session) api->ReleaseSession(session);
        if (opts) api->ReleaseSessionOptions(opts);
        if (env) api->ReleaseEnv(env);
    }
    std::vector<char*> owned_names;
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

    int OutputCount() const override {
        return static_cast<int>(output_names.size());
    }

    bool Forward(const float* nchw, int in_w, int in_h,
                 std::vector<TensorView>* outs_out,
                 std::string* error_out) override {
        const std::int64_t dims[4] = {1, 3, in_h, in_w};
        OrtValue* input = nullptr;
        OrtStatus* st = api->CreateTensorWithDataAsOrtValue(
            meminfo, const_cast<float*>(nchw),
            sizeof(float) * static_cast<std::size_t>(3) * in_w * in_h,
            dims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);
        if (!Check(st, "CreateTensorWithDataAsOrtValue", error_out)
                 .empty() ||
            input == nullptr) {
            return false;
        }
        for (OrtValue* v : outputs) api->ReleaseValue(v);
        outputs.assign(output_name_ptrs.size(), nullptr);
        const char* in_name = input_name.c_str();
        st = api->Run(session, nullptr, &in_name, &input, 1,
                      output_name_ptrs.data(), output_name_ptrs.size(),
                      outputs.data());
        api->ReleaseValue(input);
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
            // GetTensorTypeAndShape is the stable name across ORT C-API
            // revisions (ORT_API_VERSION 29 dropped the *Info-suffixed
            // alias the code was first written against).
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
                continue;   // decode paths only read f32 outputs
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

std::unique_ptr<Session> MakeOrtSession(const std::string& model_path,
                                        int* input_size,
                                        std::string* error_out) {
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
    OrtStatus* st =
        api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ya_ai_find_object",
                       &s->env);
    if (s->Check(st, "CreateEnv", error_out).empty() == false ||
        s->env == nullptr) {
        return nullptr;
    }
    st = api->CreateSessionOptions(&s->opts);
    if (!s->Check(st, "CreateSessionOptions", error_out).empty() ||
        s->opts == nullptr) {
        return nullptr;
    }
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    api->SetIntraOpNumThreads(s->opts,
                              static_cast<int>(std::min(4u, hw)));
    api->SetSessionGraphOptimizationLevel(s->opts, ORT_ENABLE_ALL);
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
    // Input name + shape.
    std::size_t n_in = 0, n_out = 0;
    if (!s->Check(api->SessionGetInputCount(s->session, &n_in),
                  "SessionGetInputCount", error_out)
             .empty() ||
        n_in < 1) {
        if (error_out && error_out->empty())
            *error_out = "model has no inputs";
        return nullptr;
    }
    char* name = nullptr;
    st = api->SessionGetInputName(s->session, 0, s->alloc, &name);
    if (!s->Check(st, "SessionGetInputName", error_out).empty() ||
        name == nullptr) {
        return nullptr;
    }
    s->input_name = name;
    s->owned_names.push_back(name);

    OrtTypeInfo* ti = nullptr;
    int detected_size = 0;
    st = api->SessionGetInputTypeInfo(s->session, 0, &ti);
    if (s->Check(st, "SessionGetInputTypeInfo", error_out).empty() &&
        ti != nullptr) {
        // ORT C-API >= ~v21: out-parameter form returning OrtStatus*
        // (the borrowed info lives as long as `ti` — freed below).
        const OrtTensorTypeAndShapeInfo* info = nullptr;
        if (api->CastTypeInfoToTensorInfo(ti, &info) == nullptr &&
            info != nullptr) {
            std::size_t nd = 0;
            if (api->GetDimensionsCount(info, &nd) == nullptr &&
                nd == 4) {
                std::int64_t d[4] = {0, 0, 0, 0};
                if (api->GetDimensions(info, d, 4) == nullptr &&
                    d[2] == d[3] && d[2] > 0) {
                    detected_size = static_cast<int>(d[2]);
                }
            }
        }
        api->ReleaseTypeInfo(ti);
    }
    // Output names.
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
    // Dynamic/symbolic dims report <= 0; Detector::Load resolves the
    // real fallback (graph probe, then a per-family default).
    *input_size = detected_size;
    return s;
}
#endif  // XER_AI_FO_ONNXRUNTIME

#if defined(XER_AI_FO_OPENCV)
struct CvSession final : Session {
    cv::dnn::Net net;
    std::vector<std::string> out_names;
    std::vector<cv::Mat> outs;
    int OutputCount() const override {
        return static_cast<int>(out_names.size());
    }
    bool Forward(const float* nchw, int in_w, int in_h,
                 std::vector<TensorView>* outs_out,
                 std::string* error_out) override {
        try {
            const int sz[4] = {1, 3, in_h, in_w};
            cv::Mat blob(4, sz, CV_32F, const_cast<float*>(nchw));
            net.setInput(blob);
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

std::unique_ptr<Session> MakeCvSession(const std::string& model_path,
                                       int* input_size,
                                       std::string* error_out) {
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
        // cv::dnn does not expose the declared input shape portably —
        // Detector::Load resolves it via ProbeOnnxInputSquare.
        *input_size = 0;
        return s;
    } catch (const cv::Exception& e) {
        if (error_out)
            *error_out = std::string("readNetFromONNX: ") + e.what();
        return nullptr;
    }
}
#endif  // XER_AI_FO_OPENCV

}  // namespace

std::vector<BackendInfo> Backends() {
    std::vector<BackendInfo> rows;
    rows.reserve(2);
    {
        BackendInfo b;
        b.id = "onnxruntime";
#if defined(XER_AI_FO_ONNXRUNTIME)
        b.linked = true;
        b.loadable = BackendLoadable(XER_AI_FO_ORT_DLLS);
        if (b.loadable) {
            b.detail = "ORT C API; onnxruntime.dll delay-loaded + "
                       "LoadLibrary-probed; CPU EP (MSYS2 ORT has no "
                       "DirectML provider)";
        } else {
            const std::string miss = FirstProbeFailure(XER_AI_FO_ORT_DLLS);
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
#if defined(XER_AI_FO_OPENCV)
        b.linked = true;
        b.loadable = BackendLoadable(XER_AI_FO_OPENCV_DLLS);
        if (b.loadable) {
            b.detail = "cv::dnn::readNetFromONNX; libopencv_*-*.dll "
                       "delay-loaded + LoadLibrary-probed";
        } else {
            const std::string miss =
                FirstProbeFailure(XER_AI_FO_OPENCV_DLLS);
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
    out << "FindObject backends (YA.AIFindObject):\n";
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

std::vector<std::string> LoadLabels(const std::string& utf8_path) {
    std::vector<std::string> labels;
    std::ifstream in{std::filesystem::path(utf8_path)};
    if (!in) return labels;
    std::string line;
    while (std::getline(in, line)) {
        const auto b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos || line[b] == '#') continue;
        const auto e = line.find_last_not_of(" \t\r\n");
        labels.push_back(line.substr(b, e - b + 1));
    }
    return labels;
}

struct Detector::Impl {
    std::unique_ptr<Session> session;
    std::vector<std::string> labels;
    std::string backend_id;
    int input_size = 0;
    std::vector<float> nchw;
    bool ready = false;
};

Detector::Detector() : impl_(std::make_unique<Impl>()) {}
Detector::~Detector() = default;

bool Detector::Load(const std::string& utf8_model_path,
                    const std::string& utf8_labels_path,
                    const std::string& backend, std::string* error_out) {
    impl_->session.reset();
    impl_->ready = false;
    impl_->backend_id.clear();
    impl_->input_size = 0;
    impl_->labels.clear();

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

    // Pick the backend: explicit id honored when loadable, otherwise
    // the first loadable (ORT preferred).
    const auto try_make = [&](const char* id, std::string* try_err)
        -> std::unique_ptr<Session> {
#if defined(XER_AI_FO_ONNXRUNTIME)
        if (std::strcmp(id, "onnxruntime") == 0) {
            return MakeOrtSession(utf8_model_path, &impl_->input_size,
                                  try_err);
        }
#endif
#if defined(XER_AI_FO_OPENCV)
        if (std::strcmp(id, "opencv_dnn") == 0) {
            return MakeCvSession(utf8_model_path, &impl_->input_size,
                                 try_err);
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
        // Model rejected on this backend — fall through to the next
        // one; report the last failure when every backend declines.
        last_err = try_err.empty() ? std::string("model rejected by ") +
                                         b.id
                                   : try_err;
    }
    if (impl_->session != nullptr && impl_->input_size <= 0) {
        // The backend could not recover a static input square (cv::dnn
        // never can; ORT sees symbolic dims). Read it from the ONNX
        // header directly; when the model itself declares dynamic
        // dims, fall back per family: multi-output DETR exports feed
        // their trained resolution (RF-DETR-N 384; larger RF-DETR /
        // RT-DETR still run — dynamic input is valid), single-output
        // YOLO exports keep the classic 640.
        const int probed = ProbeOnnxInputSquare(utf8_model_path);
        impl_->input_size =
            probed > 0
                ? probed
                : (impl_->session->OutputCount() >= 2 ? 384 : 640);
    }
    if (impl_->session == nullptr) {
        if (error_out != nullptr) {
            if (!named_backend_seen) {
                *error_out = "no detection backend available "
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
    if (!utf8_labels_path.empty()) {
        impl_->labels = LoadLabels(utf8_labels_path);
    }
    impl_->nchw.assign(static_cast<std::size_t>(3) * impl_->input_size *
                           impl_->input_size,
                       0.0f);
    impl_->ready = true;
    return true;
}

bool Detector::IsLoaded() const { return impl_->ready; }
const std::string& Detector::BackendId() const {
    return impl_->backend_id;
}
int Detector::InputSize() const { return impl_->input_size; }

std::vector<Detection> Detector::Detect(const std::uint8_t* bgra,
                                        int width, int height,
                                        int stride,
                                        const Options& options) {
    if (!impl_->ready || impl_->session == nullptr || bgra == nullptr ||
        width <= 0 || height <= 0 || stride < width * 4) {
        return {};
    }
    const int in = impl_->input_size;
    const Letterbox lb =
        BgraToNchw(bgra, width, height, stride, in, in,
                   impl_->nchw.data());
    std::vector<TensorView> outs;
    std::string err;
    if (!impl_->session->Forward(impl_->nchw.data(), in, in, &outs,
                                 &err)) {
        return {};
    }
    std::vector<RawBox> boxes;
    if (outs.size() >= 2) {
        // DETR-family export (RF-DETR / RT-DETR): separate boxes +
        // logits tensors, no NMS in graph.
        boxes = DecodeDetr(outs, options.confidence_threshold, in);
    }
    if (boxes.empty() && !outs.empty()) {
        // YOLO-style single output (or unrecognized layout — decode
        // output 0 anyway; a non-YOLO shape just yields nothing).
        boxes = DecodeYolo(outs[0].data, outs[0].dims,
                           options.confidence_threshold);
    }
    return Postprocess(std::move(boxes), lb, width, height,
                       impl_->labels, options);
}

std::vector<Detection> Detector::Detect(const video::Frame& frame,
                                        const Options& options) {
    if (frame.Format() != video::PixelFormat::BGRA8 ||
        !frame.IsValid()) {
        return {};
    }
    return Detect(frame.Data(), frame.Width(), frame.Height(),
                  frame.Width() * 4, options);
}

}  // namespace XER::AI::FindObject
