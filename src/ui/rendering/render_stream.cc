#include "render_stream.h"

#include <bit>
#include <cstring>
#include <string>

#include "render_store.h"

namespace XER::UI::Rendering::Stream {
namespace {

void PutU32(std::vector<uint8_t>& out, std::uint32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + 4);
}
void PutFloat(std::vector<uint8_t>& out, float v) {
    PutU32(out, static_cast<std::uint32_t>(std::bit_cast<std::uint32_t>(v)));
}
void PutRect(std::vector<uint8_t>& out, const Layout::Rect& r) {
    PutFloat(out, r.x);
    PutFloat(out, r.y);
    PutFloat(out, r.width);
    PutFloat(out, r.height);
}
void PutPoint(std::vector<uint8_t>& out, const Layout::Point& p) {
    PutFloat(out, p.x);
    PutFloat(out, p.y);
}
void PutColor(std::vector<uint8_t>& out, const Theme::Color& c) {
    PutFloat(out, c.r);
    PutFloat(out, c.g);
    PutFloat(out, c.b);
    PutFloat(out, c.a);
}
void PutStyle(std::vector<uint8_t>& out, const TextStyle& s) {
    PutColor(out, s.color);
    PutFloat(out, s.size);
    PutU32(out, static_cast<std::uint32_t>(s.weight));
}
void PutString(std::vector<uint8_t>& out, std::string_view s) {
    PutU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}
void Begin(std::vector<uint8_t>& out, Op op) {
    out.clear();
    PutU32(out, static_cast<std::uint32_t>(op));
}

// ---- reader helpers ------------------------------------------------
struct Cursor {
    const uint8_t* data;
    uint32_t size;
    uint32_t pos = 0;

    bool U32(std::uint32_t* v) {
        if (pos + 4 > size) return false;
        std::memcpy(v, data + pos, 4);
        pos += 4;
        return true;
    }
    bool F32(float* v) {
        std::uint32_t raw = 0;
        if (!U32(&raw)) return false;
        *v = std::bit_cast<float>(raw);
        return true;
    }
    bool Rect(Layout::Rect* r) {
        return F32(&r->x) && F32(&r->y) && F32(&r->width) &&
               F32(&r->height);
    }
    bool Point(Layout::Point* p) { return F32(&p->x) && F32(&p->y); }
    bool Color(Theme::Color* c) {
        return F32(&c->r) && F32(&c->g) && F32(&c->b) && F32(&c->a);
    }
    bool Style(TextStyle* s) {
        std::uint32_t weight = 0;
        if (!Color(&s->color) || !F32(&s->size) || !U32(&weight)) {
            return false;
        }
        s->weight = static_cast<int>(weight);
        return true;
    }
    bool Str(std::string_view* s) {
        std::uint32_t len = 0;
        if (!U32(&len) || pos + len > size) return false;
        *s = std::string_view(reinterpret_cast<const char*>(data + pos),
                              len);
        pos += len;
        return true;
    }
    bool Bytes(const uint8_t** p, std::uint32_t n) {
        if (pos + n > size) return false;
        *p = data + pos;
        pos += n;
        return true;
    }
};

}  // namespace

void StreamWriter::BeginFrame(std::vector<uint8_t>& out,
                              Layout::Rect bounds) {
    Begin(out, Op::kBeginFrame);
    PutRect(out, bounds);
}
void StreamWriter::EndFrame(std::vector<uint8_t>& out) {
    Begin(out, Op::kEndFrame);
}
void StreamWriter::SetFrameDamage(std::vector<uint8_t>& out,
                                  Layout::Rect damage) {
    Begin(out, Op::kSetFrameDamage);
    PutRect(out, damage);
}
void StreamWriter::FillRect(std::vector<uint8_t>& out, Layout::Rect rect,
                            Theme::Color color, float radius) {
    Begin(out, Op::kFillRect);
    PutRect(out, rect);
    PutColor(out, color);
    PutFloat(out, radius);
}
void StreamWriter::StrokeRect(std::vector<uint8_t>& out,
                              Layout::Rect rect, Theme::Color color,
                              float width, float radius) {
    Begin(out, Op::kStrokeRect);
    PutRect(out, rect);
    PutColor(out, color);
    PutFloat(out, width);
    PutFloat(out, radius);
}
void StreamWriter::DrawLine(std::vector<uint8_t>& out, Layout::Point a,
                            Layout::Point b, Theme::Color color,
                            float width) {
    Begin(out, Op::kDrawLine);
    PutPoint(out, a);
    PutPoint(out, b);
    PutColor(out, color);
    PutFloat(out, width);
}
void StreamWriter::DrawText(std::vector<uint8_t>& out,
                            std::string_view text, Layout::Rect rect,
                            const TextStyle& style) {
    Begin(out, Op::kDrawText);
    PutRect(out, rect);
    PutStyle(out, style);
    PutString(out, text);
}
void StreamWriter::PushClip(std::vector<uint8_t>& out, Layout::Rect rect) {
    Begin(out, Op::kPushClip);
    PutRect(out, rect);
}
void StreamWriter::PopClip(std::vector<uint8_t>& out) {
    Begin(out, Op::kPopClip);
}
void StreamWriter::DrawBackdropBlur(std::vector<uint8_t>& out,
                                    Layout::Rect rect, float radius) {
    Begin(out, Op::kDrawBackdropBlur);
    PutRect(out, rect);
    PutFloat(out, radius);
}
void StreamWriter::DrawImage(std::vector<uint8_t>& out,
                             std::string_view path, Layout::Rect rect,
                             float opacity, float corner_radius) {
    Begin(out, Op::kDrawImage);
    PutRect(out, rect);
    PutFloat(out, opacity);
    PutFloat(out, corner_radius);
    PutString(out, path);
}
void StreamWriter::DrawFrameBgra(std::vector<uint8_t>& out,
                                 const void* bgra, int width, int height,
                                 Layout::Rect dst_rect, float opacity) {
    Begin(out, Op::kDrawFrameBgra);
    PutRect(out, dst_rect);
    PutFloat(out, opacity);
    PutU32(out, static_cast<std::uint32_t>(width));
    PutU32(out, static_cast<std::uint32_t>(height));
    const auto* p = static_cast<const uint8_t*>(bgra);
    const size_t n = static_cast<size_t>(width) * height * 4;
    out.insert(out.end(), p, p + n);
}
void StreamWriter::SetDpi(std::vector<uint8_t>& out, unsigned dpi) {
    Begin(out, Op::kSetDpi);
    PutU32(out, static_cast<std::uint32_t>(dpi));
}
void StreamWriter::SetClearColor(std::vector<uint8_t>& out,
                                 Theme::Color color) {
    Begin(out, Op::kSetClearColor);
    PutColor(out, color);
}
void StreamWriter::MeasureRequest(std::vector<uint8_t>& out,
                                  std::uint32_t request_id,
                                  std::string_view text,
                                  const TextStyle& style) {
    Begin(out, Op::kMeasureRequest);
    PutU32(out, request_id);
    PutStyle(out, style);
    PutString(out, text);
}
void StreamWriter::MeasureResponse(std::vector<uint8_t>& out,
                                   std::uint32_t request_id,
                                   Layout::Size size) {
    Begin(out, Op::kMeasureResponse);
    PutU32(out, request_id);
    PutFloat(out, size.width);
    PutFloat(out, size.height);
}
void StreamWriter::SetTargetHwnd(std::vector<uint8_t>& out,
                                 std::uint64_t hwnd) {
    Begin(out, Op::kSetTargetHwnd);
    PutU32(out, static_cast<std::uint32_t>(hwnd & 0xffffffffu));
    PutU32(out, static_cast<std::uint32_t>(hwnd >> 32));
}
void StreamWriter::Shutdown(std::vector<uint8_t>& out) {
    Begin(out, Op::kShutdown);
}
void StreamWriter::AttachStore(std::vector<uint8_t>& out,
                               std::string_view name) {
    Begin(out, Op::kAttachStore);
    PutString(out, name);
}
void StreamWriter::DrawResource(std::vector<uint8_t>& out,
                                std::uint32_t id, Layout::Rect dst,
                                float opacity, float corner_radius) {
    Begin(out, Op::kDrawResource);
    PutU32(out, id);
    PutRect(out, dst);
    PutFloat(out, opacity);
    PutFloat(out, corner_radius);
}
void StreamWriter::DrawVideoFrame(std::vector<uint8_t>& out,
                                  std::uint32_t id, Layout::Rect dst,
                                  float opacity, float corner_radius) {
    Begin(out, Op::kDrawVideoFrame);
    PutU32(out, id);
    PutRect(out, dst);
    PutFloat(out, opacity);
    PutFloat(out, corner_radius);
}
void StreamWriter::FreeResource(std::vector<uint8_t>& out,
                                std::uint32_t id) {
    Begin(out, Op::kFreeResource);
    PutU32(out, id);
}
void StreamWriter::PushLayer(std::vector<uint8_t>& out, float opacity,
                             std::uint32_t blend_mode, float blur) {
    Begin(out, Op::kPushLayer);
    PutFloat(out, opacity);
    PutU32(out, blend_mode);
    PutFloat(out, blur);
}
void StreamWriter::PopLayer(std::vector<uint8_t>& out) {
    Begin(out, Op::kPopLayer);
}
void StreamWriter::SetTransform(std::vector<uint8_t>& out, float a,
                                float b, float c, float d, float tx,
                                float ty) {
    Begin(out, Op::kSetTransform);
    PutFloat(out, a);
    PutFloat(out, b);
    PutFloat(out, c);
    PutFloat(out, d);
    PutFloat(out, tx);
    PutFloat(out, ty);
}
void StreamWriter::ResetTransform(std::vector<uint8_t>& out) {
    Begin(out, Op::kResetTransform);
}
void StreamWriter::DrawSpectrum(std::vector<uint8_t>& out,
                                std::uint32_t id, Layout::Rect dst,
                                Theme::Color color, std::uint32_t mode) {
    Begin(out, Op::kDrawSpectrum);
    PutU32(out, id);
    PutRect(out, dst);
    PutColor(out, color);
    PutU32(out, mode);
}

bool StreamReader::Dispatch(Renderer& renderer, const Handlers& handlers,
                            const uint8_t* message, uint32_t bytes) {
    if (message == nullptr || bytes < 4) {
        return false;
    }
    Cursor c{message, bytes, 0};
    std::uint32_t op_raw = 0;
    if (!c.U32(&op_raw)) {
        return false;
    }
    Layout::Rect r{};
    Layout::Point a{}, b{};
    Theme::Color color{};
    TextStyle style{};
    std::string_view str;
    float f = 0.0f, f2 = 0.0f, f3 = 0.0f;
    std::uint32_t u = 0;

    switch (static_cast<Op>(op_raw)) {
        case Op::kBeginFrame:
            if (!c.Rect(&r)) return false;
            renderer.BeginFrame(r);
            return true;
        case Op::kEndFrame:
            renderer.EndFrame();
            return true;
        case Op::kSetFrameDamage:
            if (!c.Rect(&r)) return false;
            renderer.SetFrameDamage(r);
            return true;
        case Op::kFillRect:
            if (!c.Rect(&r) || !c.Color(&color) || !c.F32(&f)) {
                return false;
            }
            renderer.FillRect(r, color, f);
            return true;
        case Op::kStrokeRect:
            if (!c.Rect(&r) || !c.Color(&color) || !c.F32(&f) ||
                !c.F32(&f2)) {
                return false;
            }
            renderer.StrokeRect(r, color, f, f2);
            return true;
        case Op::kDrawLine:
            if (!c.Point(&a) || !c.Point(&b) || !c.Color(&color) ||
                !c.F32(&f)) {
                return false;
            }
            renderer.DrawLine(a, b, color, f);
            return true;
        case Op::kDrawText:
            if (!c.Rect(&r) || !c.Style(&style) || !c.Str(&str)) {
                return false;
            }
            renderer.DrawText(str, r, style);
            return true;
        case Op::kPushClip:
            if (!c.Rect(&r)) return false;
            renderer.PushClip(r);
            return true;
        case Op::kPopClip:
            renderer.PopClip();
            return true;
        case Op::kDrawBackdropBlur:
            if (!c.Rect(&r) || !c.F32(&f)) return false;
            renderer.DrawBackdropBlur(r, f);
            return true;
        case Op::kDrawImage:
            if (!c.Rect(&r) || !c.F32(&f) || !c.F32(&f2) ||
                !c.Str(&str)) {
                return false;
            }
            renderer.DrawImage(str, r, f, f2);
            return true;
        case Op::kDrawFrameBgra: {
            if (!c.Rect(&r) || !c.F32(&f)) return false;
            std::uint32_t w = 0, h = 0;
            const uint8_t* pixels = nullptr;
            if (!c.U32(&w) || !c.U32(&h)) return false;
            const std::uint64_t n =
                static_cast<std::uint64_t>(w) * h * 4;
            if (n > bytes || !c.Bytes(&pixels,
                                      static_cast<std::uint32_t>(n))) {
                return false;
            }
            renderer.DrawFrameBgra(pixels, static_cast<int>(w),
                                   static_cast<int>(h), r, f);
            return true;
        }
        case Op::kSetDpi:
            if (!c.U32(&u)) return false;
            renderer.SetDpi(u);
            return true;
        case Op::kSetClearColor:
            if (!c.Color(&color)) return false;
            renderer.SetClearColor(color);
            return true;
        case Op::kMeasureRequest:
            // Handled by the worker loop (needs a reply channel).
            return false;
        case Op::kMeasureResponse:
            return false;  // only meaningful on the UI side
        case Op::kSetTargetHwnd: {
            std::uint32_t lo = 0, hi = 0;
            if (!c.U32(&lo) || !c.U32(&hi)) return false;
            if (handlers.on_retarget) {
                handlers.on_retarget(
                    static_cast<std::uint64_t>(lo) |
                    (static_cast<std::uint64_t>(hi) << 32));
            }
            return true;
        }
        case Op::kShutdown:
            if (handlers.on_shutdown) {
                handlers.on_shutdown();
            }
            return true;
        case Op::kAttachStore:
            if (!c.Str(&str)) return false;
            if (handlers.on_attach_store) {
                handlers.on_attach_store(str);
            }
            return true;
        case Op::kDrawResource:
        case Op::kDrawVideoFrame: {
            if (!c.U32(&u) || !c.Rect(&r) || !c.F32(&f) ||
                !c.F32(&f2)) {
                return false;
            }
            if (!handlers.on_resource) return false;
            StreamReader::Handlers::ResourceMeta meta{};
            const void* pixels = handlers.on_resource(u, &meta);
            if (pixels == nullptr) return true;  // id absent / in flight
            // Validate before the renderer copies meta.width*height*4
            // bytes out of the shared mapping: the slot kind must match
            // the op and the payload must cover a full BGRA frame. A
            // kind-mismatched or torn slot would otherwise read past
            // the extent — possibly past the end of the mapping.
            const std::uint32_t want_kind = static_cast<std::uint32_t>(
                op_raw == static_cast<std::uint32_t>(Op::kDrawResource)
                    ? RenderStore::Kind::kImageBgra
                    : RenderStore::Kind::kVideoFrame);
            const std::uint64_t needed =
                static_cast<std::uint64_t>(meta.width) * meta.height *
                4u;
            if (meta.kind != want_kind || meta.width == 0 ||
                meta.height == 0 || meta.payload < needed) {
                return true;  // malformed slot — drop the draw
            }
            if (op_raw == static_cast<std::uint32_t>(Op::kDrawResource)) {
                renderer.DrawResourcePixels(
                    pixels, u, static_cast<int>(meta.width),
                    static_cast<int>(meta.height), r, f, f2);
            } else {
                renderer.DrawVideoPixels(
                    pixels, u, static_cast<int>(meta.width),
                    static_cast<int>(meta.height), r, f, f2);
            }
            return true;
        }
        case Op::kFreeResource:
            if (!c.U32(&u)) return false;
            renderer.FreeResource(u);
            return true;
        case Op::kPushLayer:
            if (!c.F32(&f) || !c.U32(&u) || !c.F32(&f2)) return false;
            renderer.PushLayer(f, u, f2);
            return true;
        case Op::kPopLayer:
            renderer.PopLayer();
            return true;
        case Op::kSetTransform: {
            float a_, b_, c_, d_, tx_, ty_;
            if (!c.F32(&a_) || !c.F32(&b_) || !c.F32(&c_) ||
                !c.F32(&d_) || !c.F32(&tx_) || !c.F32(&ty_)) {
                return false;
            }
            renderer.SetTransform(a_, b_, c_, d_, tx_, ty_);
            return true;
        }
        case Op::kResetTransform:
            renderer.ResetTransform();
            return true;
        case Op::kDrawSpectrum: {
            std::uint32_t mode = 0;
            if (!c.U32(&u) || !c.Rect(&r) || !c.Color(&color) ||
                !c.U32(&mode)) {
                return false;
            }
            if (!handlers.on_resource) return false;
            StreamReader::Handlers::ResourceMeta meta{};
            const void* pixels = handlers.on_resource(u, &meta);
            if (pixels == nullptr) return true;
            // Same guard as the pixel draws: a non-spectrum slot (or a
            // torn read) must not be reinterpreted as float levels.
            if (meta.kind != static_cast<std::uint32_t>(
                                 RenderStore::Kind::kSpectrumF32) ||
                meta.payload < sizeof(float)) {
                return true;  // malformed slot — drop the draw
            }
            const std::uint32_t count = meta.payload / sizeof(float);
            renderer.DrawSpectrumPixels(
                static_cast<const float*>(pixels), count, r, color,
                mode);
            return true;
        }
        default:
            return false;
    }
    (void)f3;
    (void)u;
}

bool StreamReader::PeekOp(const uint8_t* message, uint32_t bytes,
                          Op* op) {
    if (message == nullptr || bytes < 4 || op == nullptr) {
        return false;
    }
    std::uint32_t raw = 0;
    std::memcpy(&raw, message, 4);
    *op = static_cast<Op>(raw);
    return true;
}

bool StreamReader::ParseMeasureRequest(const uint8_t* message,
                                       uint32_t bytes,
                                       std::uint32_t* request_id,
                                       std::string* text,
                                       TextStyle* style) {
    if (message == nullptr || bytes < 4) {
        return false;
    }
    Cursor c{message, bytes, 4};
    std::string_view sv;
    if (!c.U32(request_id) || !c.Style(style) || !c.Str(&sv)) {
        return false;
    }
    *text = std::string(sv);
    return true;
}

bool StreamReader::ParseMeasureResponse(const uint8_t* message,
                                        uint32_t bytes,
                                        std::uint32_t* request_id,
                                        Layout::Size* size) {
    if (message == nullptr || bytes < 4) {
        return false;
    }
    Cursor c{message, bytes, 4};
    return c.U32(request_id) && c.F32(&size->width) &&
           c.F32(&size->height);
}

}  // namespace XER::UI::Rendering::Stream

namespace XER::UI::Rendering {

StreamRenderer::StreamRenderer(SendFn send, RequestFn request,
                               ReleaseFn on_destroy)
    : send_(std::move(send)),
      request_(std::move(request)),
      on_destroy_(std::move(on_destroy)) {}

StreamRenderer::~StreamRenderer() {
    if (on_destroy_) {
        on_destroy_();
    }
}

bool StreamRenderer::Emit(std::vector<uint8_t>&& msg) {
    return send_ ? send_(std::move(msg)) : false;
}

Layout::Size StreamRenderer::Request(std::vector<uint8_t>&& msg,
                                     std::uint32_t id) const {
    if (!request_) {
        return {0.0f, 0.0f};
    }
    return request_(std::move(msg), id);
}

void StreamRenderer::BeginFrame(Layout::Rect bounds) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::BeginFrame(m, bounds);
    Emit(std::move(m));
}
void StreamRenderer::EndFrame() {
    std::vector<uint8_t> m;
    Stream::StreamWriter::EndFrame(m);
    Emit(std::move(m));
}
void StreamRenderer::SetFrameDamage(Layout::Rect damage) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::SetFrameDamage(m, damage);
    Emit(std::move(m));
}
void StreamRenderer::FillRect(Layout::Rect rect, Theme::Color color,
                              float radius) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::FillRect(m, rect, color, radius);
    Emit(std::move(m));
}
void StreamRenderer::StrokeRect(Layout::Rect rect, Theme::Color color,
                                float width, float radius) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::StrokeRect(m, rect, color, width, radius);
    Emit(std::move(m));
}
void StreamRenderer::DrawLine(Layout::Point start, Layout::Point end,
                              Theme::Color color, float width) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawLine(m, start, end, color, width);
    Emit(std::move(m));
}
void StreamRenderer::DrawText(std::string_view text, Layout::Rect rect,
                              TextStyle style) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawText(m, text, rect, style);
    Emit(std::move(m));
}
Layout::Size StreamRenderer::MeasureText(std::string_view text,
                                         TextStyle style) const {
    const std::uint32_t id = next_request_id_++;
    std::vector<uint8_t> m;
    Stream::StreamWriter::MeasureRequest(m, id, text, style);
    return Request(std::move(m), id);
}
void StreamRenderer::PushClip(Layout::Rect rect) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::PushClip(m, rect);
    Emit(std::move(m));
}
void StreamRenderer::PopClip() {
    std::vector<uint8_t> m;
    Stream::StreamWriter::PopClip(m);
    Emit(std::move(m));
}
void StreamRenderer::DrawBackdropBlur(Layout::Rect rect, float radius) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawBackdropBlur(m, rect, radius);
    Emit(std::move(m));
}
bool StreamRenderer::DrawImage(std::string_view path, Layout::Rect rect,
                               float opacity, float corner_radius) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawImage(m, path, rect, opacity,
                                    corner_radius);
    return Emit(std::move(m));
}
bool StreamRenderer::DrawFrameBgra(const void* bgra, int width,
                                   int height, Layout::Rect dst_rect,
                                   float opacity) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawFrameBgra(m, bgra, width, height, dst_rect,
                                        opacity);
    return Emit(std::move(m));
}
void StreamRenderer::SetDpi(unsigned dpi) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::SetDpi(m, dpi);
    Emit(std::move(m));
}
void StreamRenderer::SetClearColor(Theme::Color color) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::SetClearColor(m, color);
    Emit(std::move(m));
}
void StreamRenderer::PushLayer(float opacity, std::uint32_t blend_mode,
                               float blur) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::PushLayer(m, opacity, blend_mode, blur);
    Emit(std::move(m));
}
void StreamRenderer::PopLayer() {
    std::vector<uint8_t> m;
    Stream::StreamWriter::PopLayer(m);
    Emit(std::move(m));
}
void StreamRenderer::SetTransform(float a, float b, float c, float d,
                                  float tx, float ty) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::SetTransform(m, a, b, c, d, tx, ty);
    Emit(std::move(m));
}
void StreamRenderer::ResetTransform() {
    std::vector<uint8_t> m;
    Stream::StreamWriter::ResetTransform(m);
    Emit(std::move(m));
}
bool StreamRenderer::DrawResource(std::uint32_t id, Layout::Rect dst,
                                  float opacity, float corner_radius) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawResource(m, id, dst, opacity,
                                       corner_radius);
    return Emit(std::move(m));
}
bool StreamRenderer::DrawVideoFrame(std::uint32_t id, Layout::Rect dst,
                                    float opacity,
                                    float corner_radius) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawVideoFrame(m, id, dst, opacity,
                                         corner_radius);
    return Emit(std::move(m));
}
bool StreamRenderer::DrawSpectrum(std::uint32_t id, Layout::Rect dst,
                                  Theme::Color color,
                                  std::uint32_t mode) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::DrawSpectrum(m, id, dst, color, mode);
    return Emit(std::move(m));
}
void StreamRenderer::FreeResource(std::uint32_t id) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::FreeResource(m, id);
    Emit(std::move(m));
}
void StreamRenderer::AttachStore(std::string_view name) {
    std::vector<uint8_t> m;
    Stream::StreamWriter::AttachStore(m, name);
    Emit(std::move(m));
}

}  // namespace XER::UI::Rendering
