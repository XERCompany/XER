// xer_transition_bridge.cc — see xer_transition_bridge.h for the
// graph contract. FFmpeg headers stay confined to this TU (the
// header only exposes video::Frame + std types), same rule as
// src/wrapper/ffmpeg.
#include "xer_transition_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

#if defined(ENGINE_HAS_FFMPEG_BRIDGE) && defined(ENGINE_HAS_FFMPEG_AVFILTER)
#define XER_VE_XFADE 1
#else
#define XER_VE_XFADE 0
#endif

#if XER_VE_XFADE
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace XER::VideoEditor {
namespace {

void SetError(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
}

// Picker table — one row per exposed xfade transition, ordered
// group-first so a flat context menu reads as sections. `group`
// drives the menu's separators/headers.
const std::vector<XfadeTransitionInfo> kXfadeTypes = {
    // Basic — the dissolve/fade family editors reach for first.
    {"fade",       "Fade",            "Basic"},
    {"fadefast",   "Fade Fast",       "Basic"},
    {"fadeslow",   "Fade Slow",       "Basic"},
    {"fadeblack",  "Fade to Black",   "Basic"},
    {"fadewhite",  "Fade to White",   "Basic"},
    {"fadegrays",  "Fade Grays",      "Basic"},
    {"dissolve",   "Dissolve",        "Basic"},
    {"hblur",      "Horizontal Blur", "Basic"},
    // Wipe — hard-edge reveals.
    {"wipeleft",   "Wipe Left",       "Wipe"},
    {"wiperight",  "Wipe Right",      "Wipe"},
    {"wipeup",     "Wipe Up",         "Wipe"},
    {"wipedown",   "Wipe Down",       "Wipe"},
    {"wipetl",     "Wipe Top-Left",   "Wipe"},
    {"wipetr",     "Wipe Top-Right",  "Wipe"},
    {"wipebl",     "Wipe Bottom-Left","Wipe"},
    {"wipebr",     "Wipe Bottom-Right","Wipe"},
    // Slide — whole-frame translations (over AND under move).
    {"slideleft",  "Slide Left",      "Slide"},
    {"slideright", "Slide Right",     "Slide"},
    {"slideup",    "Slide Up",        "Slide"},
    {"slidedown",  "Slide Down",      "Slide"},
    {"coverleft",  "Cover Left",      "Slide"},
    {"coverright", "Cover Right",     "Slide"},
    {"coverup",    "Cover Up",        "Slide"},
    {"coverdown",  "Cover Down",      "Slide"},
    {"revealleft", "Reveal Left",     "Slide"},
    {"revealright","Reveal Right",    "Slide"},
    {"revealup",   "Reveal Up",       "Slide"},
    {"revealdown", "Reveal Down",     "Slide"},
    // Geometric — shaped/structured reveals.
    {"circlecrop", "Circle Crop",     "Geometric"},
    {"rectcrop",   "Rect Crop",       "Geometric"},
    {"circleopen", "Circle Open",     "Geometric"},
    {"circleclose","Circle Close",    "Geometric"},
    {"vertopen",   "Vertical Open",   "Geometric"},
    {"vertclose",  "Vertical Close",  "Geometric"},
    {"horzopen",   "Horizontal Open", "Geometric"},
    {"horzclose",  "Horizontal Close","Geometric"},
    {"diagtl",     "Diagonal TL",     "Geometric"},
    {"diagtr",     "Diagonal TR",     "Geometric"},
    {"diagbl",     "Diagonal BL",     "Geometric"},
    {"diagbr",     "Diagonal BR",     "Geometric"},
    {"squeezeh",   "Squeeze H",       "Geometric"},
    {"squeezev",   "Squeeze V",       "Geometric"},
    {"distance",   "Distance",        "Geometric"},
    {"radial",     "Radial",          "Geometric"},
    {"zoomin",     "Zoom In",         "Geometric"},
    // Fancy — stylized motion/grain transitions.
    {"smoothleft", "Smooth Left",     "Fancy"},
    {"smoothright","Smooth Right",    "Fancy"},
    {"smoothup",   "Smooth Up",       "Fancy"},
    {"smoothdown", "Smooth Down",     "Fancy"},
    {"hlslice",    "HL Slice",        "Fancy"},
    {"hrslice",    "HR Slice",        "Fancy"},
    {"vuslice",    "VU Slice",        "Fancy"},
    {"vdslice",    "VD Slice",        "Fancy"},
    {"hlwind",     "HL Wind",         "Fancy"},
    {"hrwind",     "HR Wind",         "Fancy"},
    {"vuwind",     "VU Wind",         "Fancy"},
    {"vdwind",     "VD Wind",         "Fancy"},
    {"pixelize",   "Pixelize",        "Fancy"},
};

const XfadeTransitionInfo* FindType(const std::string& name) {
    for (const XfadeTransitionInfo& info : kXfadeTypes) {
        if (name == info.name) return &info;
    }
    return nullptr;
}

#if XER_VE_XFADE

// One cached (type,w,h) graph. Constructed lazily; the priming pair
// at pts=0 pins xfade's internal start_pts + inputs_offset_pts so
// every subsequent eval maps weight -> pts linearly.
struct XfadeGraph {
    AVFilterGraph* graph = nullptr;
    AVFilterContext* src_a = nullptr;
    AVFilterContext* src_b = nullptr;
    AVFilterContext* sink = nullptr;
    bool primed = false;

    ~XfadeGraph() { avfilter_graph_free(&graph); }
    XfadeGraph() = default;
    XfadeGraph(const XfadeGraph&) = delete;
    XfadeGraph& operator=(const XfadeGraph&) = delete;
};

constexpr int64_t kPtsBase = 1000000;   // time_base = 1/1e6 seconds

void FreeFrame(AVFrame* f) { av_frame_free(&f); }

// BGRA8 packed -> refcounted AVFrame (one copy — the gbrp hop
// dominates anyway). pts in 1/kPtsBase units.
AVFrame* WrapBgra(const video::Frame& src, int64_t pts) {
    AVFrame* f = av_frame_alloc();
    if (f == nullptr) return nullptr;
    f->format = AV_PIX_FMT_BGRA;
    f->width = src.Width();
    f->height = src.Height();
    f->pts = pts;
    if (av_frame_get_buffer(f, 32) < 0) {
        av_frame_free(&f);
        return nullptr;
    }
    const int row = src.Width() * 4;
    for (int y = 0; y < src.Height(); ++y) {
        std::memcpy(f->data[0] + static_cast<size_t>(y) * f->linesize[0],
                    src.Data() + static_cast<size_t>(y) * row,
                    static_cast<size_t>(row));
    }
    return f;
}

// buffersrc(BGRA) -> format=gbrp -> xfade -> format=bgra -> sink.
// The gbrp hop is required: xfade's pix_fmts list is planar-only
// (GBRP/GBRAP, YUV444P*, GRAY*); BGRA stays at the edges.
bool BuildGraph(const char* transition, int w, int h,
                XfadeGraph* out, std::string* error_out) {
    AVFilterGraph* graph = avfilter_graph_alloc();
    if (graph == nullptr) {
        SetError(error_out, "avfilter_graph_alloc failed");
        return false;
    }
    auto fail = [&](const char* what) {
        avfilter_graph_free(&graph);
        SetError(error_out, what);
        return false;
    };
    char args[160];
    // frame_rate must be nonzero: xfade's config_output rejects
    // variable-rate inputs. The fabricated pts carry the real
    // timing (1/kPtsBase); the declared rate is only a contract
    // marker, so 1e6/1 matches the time_base tick.
    std::snprintf(args, sizeof(args),
                  "video_size=%dx%d:pix_fmt=%d:time_base=1/%lld"
                  ":frame_rate=%lld/1",
                  w, h, static_cast<int>(AV_PIX_FMT_BGRA),
                  static_cast<long long>(kPtsBase),
                  static_cast<long long>(kPtsBase));
    AVFilterContext *src_a = nullptr, *src_b = nullptr,
                    *sink = nullptr, *xf = nullptr,
                    *fmt_a = nullptr, *fmt_b = nullptr,
                    *fmt_out = nullptr;
    const AVFilter* buffer = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    const AVFilter* xfade = avfilter_get_by_name("xfade");
    const AVFilter* format = avfilter_get_by_name("format");
    if (buffer == nullptr || buffersink == nullptr ||
        xfade == nullptr || format == nullptr) {
        return fail("avfilter buffer/xfade/format missing");
    }
    if (avfilter_graph_create_filter(&src_a, buffer, "xa", args,
                                     nullptr, graph) < 0 ||
        avfilter_graph_create_filter(&src_b, buffer, "xb", args,
                                     nullptr, graph) < 0) {
        return fail("buffersrc create failed");
    }
    if (avfilter_graph_create_filter(&sink, buffersink, "xs", nullptr,
                                     nullptr, graph) < 0) {
        return fail("buffersink create failed");
    }
    char xargs[128];
    std::snprintf(xargs, sizeof(xargs),
                  "transition=%s:duration=1:offset=0", transition);
    if (avfilter_graph_create_filter(&xf, xfade, "xf", xargs,
                                     nullptr, graph) < 0) {
        return fail("xfade create failed");
    }
    if (avfilter_graph_create_filter(&fmt_a, format, "fa",
                                     "pix_fmts=gbrp", nullptr,
                                     graph) < 0 ||
        avfilter_graph_create_filter(&fmt_b, format, "fb",
                                     "pix_fmts=gbrp", nullptr,
                                     graph) < 0 ||
        avfilter_graph_create_filter(&fmt_out, format, "fo",
                                     "pix_fmts=bgra", nullptr,
                                     graph) < 0) {
        return fail("format filter create failed");
    }
    if (avfilter_link(src_a, 0, fmt_a, 0) < 0 ||
        avfilter_link(fmt_a, 0, xf, 0) < 0 ||
        avfilter_link(src_b, 0, fmt_b, 0) < 0 ||
        avfilter_link(fmt_b, 0, xf, 1) < 0 ||
        avfilter_link(xf, 0, fmt_out, 0) < 0 ||
        avfilter_link(fmt_out, 0, sink, 0) < 0) {
        return fail("avfilter_link failed");
    }
    if (avfilter_graph_config(graph, nullptr) < 0) {
        return fail("avfilter_graph_config failed");
    }
    out->graph = graph;
    out->src_a = src_a;
    out->src_b = src_b;
    out->sink = sink;
    out->primed = false;
    return true;
}

// Pushes the pair at `pts`, drains the sink, returns the LAST output
// frame (there is exactly one per contract; extras would be stale).
AVFrame* PushPull(XfadeGraph* g, const video::Frame& a,
                  const video::Frame& b, int64_t pts) {
    AVFrame* fa = WrapBgra(a, pts);
    AVFrame* fb = WrapBgra(b, pts);
    if (fa == nullptr || fb == nullptr) {
        FreeFrame(fa);
        FreeFrame(fb);
        return nullptr;
    }
    if (av_buffersrc_add_frame(g->src_a, fa) < 0 ||
        av_buffersrc_add_frame(g->src_b, fb) < 0) {
        FreeFrame(fa);
        FreeFrame(fb);
        return nullptr;
    }
    AVFrame* out = nullptr;
    for (;;) {
        AVFrame* got = av_frame_alloc();
        if (got == nullptr) break;
        const int ret = av_buffersink_get_frame(g->sink, got);
        if (ret < 0) {          // EAGAIN/EOF — sink drained
            av_frame_free(&got);
            break;
        }
        av_frame_free(&out);    // keep only the freshest
        out = got;
    }
    return out;
}

// Lazy per-(name,w,h) graph cache. Keyed map + mutex: the compositor
// is single-consumer-thread, but the smoke suite and any future
// parallel renderer share the cache.
std::mutex g_cache_mu;
std::map<std::string, std::unique_ptr<XfadeGraph>> g_cache;

XfadeGraph* GraphFor(const char* name, int w, int h) {
    const std::string key =
        std::string(name) + "|" + std::to_string(w) + "x" +
        std::to_string(h);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second.get();
    auto graph = std::make_unique<XfadeGraph>();
    std::string error;
    if (!BuildGraph(name, w, h, graph.get(), &error)) {
        return nullptr;
    }
    XfadeGraph* raw = graph.get();
    g_cache.emplace(key, std::move(graph));
    return raw;
}

#endif  // XER_VE_XFADE

}  // namespace

const std::vector<XfadeTransitionInfo>& XfadeTransitions() {
    return kXfadeTypes;
}

bool XfadeAvailable() {
#if XER_VE_XFADE
    static const bool ok = avfilter_get_by_name("xfade") != nullptr;
    return ok;
#else
    return false;
#endif
}

bool XfadeTypeAvailable(const std::string& name) {
    return FindType(name) != nullptr && XfadeAvailable();
}

bool ApplyXfadeTransition(const std::string& name, double weight,
                          const video::Frame& under,
                          const video::Frame& over, video::Frame* out,
                          std::string* error_out) {
    if (out == nullptr) {
        SetError(error_out, "null out frame");
        return false;
    }
    if (under.Format() != video::PixelFormat::BGRA8 ||
        over.Format() != video::PixelFormat::BGRA8 ||
        !under.IsValid() || !over.IsValid() ||
        under.Width() != over.Width() ||
        under.Height() != over.Height()) {
        SetError(error_out, "frame contract violated");
        return false;
    }
    const int w = under.Width(), h = under.Height();
    const double weight_c = std::clamp(weight, 0.0, 1.0);
    auto copy_out = [&](const video::Frame& src) {
        if (!out->IsValid() || out->Width() != w || out->Height() != h ||
            out->Format() != video::PixelFormat::BGRA8) {
            out->Release();
            if (!out->Allocate(w, h, video::PixelFormat::BGRA8)) {
                return false;
            }
        }
        std::memcpy(out->Data(), src.Data(), src.DataSize());
        return true;
    };
    // Exact endpoints — also dodges xfade's end-of-transition
    // passthrough semantics entirely.
    if (weight_c <= 0.0) return copy_out(under);
    if (weight_c >= 1.0) return copy_out(over);
    if (FindType(name) == nullptr) {
        SetError(error_out, "unknown xfade type '" + name + "'");
        return false;
    }
#if !XER_VE_XFADE
    SetError(error_out, "avfilter not compiled in");
    return false;
#else
    if (!XfadeAvailable()) {
        SetError(error_out, "xfade filter not in libavfilter");
        return false;
    }
    std::lock_guard<std::mutex> lock(g_cache_mu);
    const int64_t pts =
        static_cast<int64_t>(std::llround(weight_c * kPtsBase));
    for (int attempt = 0; attempt < 2; ++attempt) {
        XfadeGraph* g = GraphFor(name.c_str(), w, h);
        if (g == nullptr) {
            SetError(error_out, "xfade graph build failed");
            return false;
        }
        if (!g->primed) {
            // Pin start_pts/inputs_offset_pts at 0 so pts -> weight
            // stays linear for the graph's whole life. The priming
            // output (progress 0 -> pure `under`) is discarded.
            AVFrame* primed = PushPull(g, under, over, 0);
            if (primed == nullptr) {
                g_cache.erase(std::string(name) + "|" +
                              std::to_string(w) + "x" +
                              std::to_string(h));
                continue;   // rebuild once
            }
            av_frame_free(&primed);
            g->primed = true;
        }
        AVFrame* result = PushPull(g, under, over, pts);
        if (result == nullptr) {
            // Queue drift / unproductive graph — rebuild once.
            g_cache.erase(std::string(name) + "|" + std::to_string(w) +
                          "x" + std::to_string(h));
            continue;
        }
        if (result->pts != pts ||
            result->format != AV_PIX_FMT_BGRA ||
            result->width != w || result->height != h) {
            av_frame_free(&result);
            g_cache.erase(std::string(name) + "|" + std::to_string(w) +
                          "x" + std::to_string(h));
            continue;   // stale/mismatched output — rebuild once
        }
        if (!out->IsValid() || out->Width() != w || out->Height() != h ||
            out->Format() != video::PixelFormat::BGRA8) {
            out->Release();
            if (!out->Allocate(w, h, video::PixelFormat::BGRA8)) {
                av_frame_free(&result);
                SetError(error_out, "out alloc failed");
                return false;
            }
        }
        const int row = w * 4;
        for (int y = 0; y < h; ++y) {
            std::memcpy(
                out->Data() + static_cast<size_t>(y) * row,
                result->data[0] +
                    static_cast<size_t>(y) * result->linesize[0],
                static_cast<size_t>(row));
        }
        av_frame_free(&result);
        return true;
    }
    SetError(error_out, "xfade graph did not produce a frame");
    return false;
#endif
}

void XfadeGraphCacheClear() {
#if XER_VE_XFADE
    std::lock_guard<std::mutex> lock(g_cache_mu);
    g_cache.clear();
#endif
}

}  // namespace XER::VideoEditor
