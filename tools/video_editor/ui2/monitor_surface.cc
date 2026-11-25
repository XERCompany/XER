#include "monitor_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "lab/object_ops.h"   // MaskToPolygon — mask-contour overlay
#include "rendering/renderer.h"
#include "ve_theme.h"
#include "widgets/surface.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;
namespace W = XER::UI::Widgets;

// Bottom mini-ruler strip height (old monitor_widget RulerRect
// parity): work-area brackets + loop band + marker flags + playhead.
constexpr float kRulerHeight = 14.0f;
// Drag-vs-click discrimination (DIP).
constexpr float kDragThreshold = 4.0f;
// Double-click window for zoom-fit reset (spec §6.6 — surfaces
// synthesize their own double clicks).
constexpr double kDoubleClickSeconds = 0.4;
// Wheel zoom: one notch steps the zoom by this factor.
constexpr float kZoomStep = 1.25f;

// ---- transform gizmo metrics ----
// Handle square half-size (DIP); hit slop is wider than the visual.
constexpr float kGizmoHandleHalf = 3.5f;
constexpr float kGizmoHitSlop = 7.0f;
// Rotation stem: the rotate handle rides this far above the box's
// top-edge midpoint (Premiere-style).
constexpr float kGizmoStem = 22.0f;
// Snap distance in client px (timeline snap threshold parity).
constexpr float kGizmoSnapPx = 6.0f;
// Snap indicator — the timeline's kAccentCyan magnet-guide token.
constexpr Color kGizmoSnapColor{0.13f, 0.83f, 0.93f, 1.0f};
// Rotation snap step when Shift is held (degrees).
constexpr float kGizmoRotSnapDeg = 15.0f;
// Scale drag clamps — keeps the warp's `scale > 0` contract and a
// recoverable minimum handle.
constexpr float kGizmoMinScale = 0.02f;
constexpr float kGizmoMaxScale = 100.0f;
constexpr double kGizmoPi = 3.14159265358979323846;

Color WithAlpha(Color c, float alpha) {
    c.a *= alpha;
    return c;
}

Color MarkerColor(const Marker& marker) {
    return {marker.color[0] / 255.0f, marker.color[1] / 255.0f,
            marker.color[2] / 255.0f, marker.color[3] / 255.0f};
}

}  // namespace

MonitorSurface::MonitorSurface(Id id) : id_(id) {
    surface_ = std::make_shared<W::Surface>();
    surface_->SetMinSize({240.0f, 160.0f});
    surface_->SetPaintCallback(
        [this](Rendering::Renderer& renderer, const Layout::Rect& c,
               const Theme::Tokens& tokens) {
            Paint(renderer, c, tokens);
        });
    surface_->SetEventCallback(
        [this](const W::Event& e) { return OnSurfaceEvent(e); });
    AddChild(surface_);
    SetFocusable(true);
}

MonitorSurface::~MonitorSurface() = default;

std::shared_ptr<MonitorSurface> MonitorSurface::Create(Id id) {
    // shared_from_this needs established ownership — construct via
    // make_shared and finish setup here (AddChild above is fine: the
    // kit queues pending children before ownership exists).
    return std::shared_ptr<MonitorSurface>(new MonitorSurface(id));
}

// ---- hooks -----------------------------------------------------------------

void MonitorSurface::SetHooks(MonitorHooks hooks) {
    hooks_ = std::move(hooks);
}
void MonitorSurface::SetOnActivate(std::function<void()> cb) {
    hooks_.on_activate = std::move(cb);
}
void MonitorSurface::SetOnTimeChanged(
    std::function<void(double)> cb) {
    hooks_.on_time_changed = std::move(cb);
}
void MonitorSurface::SetOnClickPoint(
    std::function<void(float, float)> cb) {
    hooks_.on_click_point = std::move(cb);
}
void MonitorSurface::SetOnTogglePlay(std::function<void()> cb) {
    hooks_.on_toggle_play = std::move(cb);
}
void MonitorSurface::SetOnStatus(
    std::function<void(const std::string&, bool)> cb) {
    hooks_.on_status = std::move(cb);
}
void MonitorSurface::SetClickPointsArmed(bool armed) {
    click_points_armed_ = armed;
}
void MonitorSurface::SetOnZoomChanged(
    std::function<void(float)> cb) {
    on_zoom_changed_ = std::move(cb);
}

void MonitorSurface::Activate() {
    if (hooks_.on_activate) hooks_.on_activate();
}

void MonitorSurface::EmitStatus(const std::string& text,
                                bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---- frame feed ------------------------------------------------------------

void MonitorSurface::SetFrame(const std::vector<std::uint8_t>& bgra,
                              int width, int height) {
    if (bgra.empty() || width <= 0 || height <= 0) return;
    // Malformed engine frame (buffer shorter than w*h*4) — drop it
    // instead of reading out of bounds in the paint path (old
    // SetExternalFrame parity; division form avoids w*h*4 overflow).
    if (bgra.size() / 4u <
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height)) {
        return;
    }
    video_frame_ = nullptr;   // legacy path — drop any VideoFrame lease
    frame_px_offset_ = 0;
    frame_ = std::make_shared<const std::vector<std::uint8_t>>(bgra);
    frame_width_ = width;
    frame_height_ = height;
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetVideoFrame(
    Render::scoped_refptr<Render::VideoFrame> frame) {
    // R9 zero-copy feed — same validity contract as SetFrame, plus
    // storage/format gates the CPU paint path needs. Unsupported
    // frames are dropped silently: callers keep their CPU fallback.
    if (!frame || !frame->IsMappable() || frame->planes() < 1 ||
        frame->data(0) == nullptr) {
        return;
    }
    const auto fmt = frame->format();
    if (fmt != Render::VideoPixelFormat::kBGRA8 &&
        fmt != Render::VideoPixelFormat::kRGBA8) {
        return;
    }
    const Render::VideoRect vis = frame->visible_rect();
    const Render::VideoSize coded = frame->coded_size();
    if (vis.IsEmpty() || coded.IsEmpty() ||
        !vis.FitsInside(coded)) {
        return;
    }
    // DrawFrameBgra reads packed rows — the visible rows must be
    // contiguous (stride == vis.width*4). Padded/unpacked sources
    // need a repack or the texture path; neither exists yet.
    if (frame->stride(0) != vis.width * 4) {
        return;
    }
    auto pixels = frame->SharePixels();
    if (!pixels || pixels->empty() ||
        pixels->size() / 4u <
            static_cast<std::size_t>(coded.width) *
                static_cast<std::size_t>(coded.height)) {
        return;
    }
    // Order matters for pooling: release the previous alias FIRST so
    // the old frame's storage is the sole owner when the lease dies
    // — that is the only case the pool recycles (a live alias means
    // the buffer frees with the reader instead).
    frame_ = std::move(pixels);
    video_frame_ = std::move(frame);
    frame_px_offset_ =
        static_cast<std::size_t>(vis.y) *
            static_cast<std::size_t>(frame->stride(0)) +
        static_cast<std::size_t>(vis.x) * 4;
    frame_width_ = vis.width;
    frame_height_ = vis.height;
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetFrameProvider(FrameProvider provider) {
    frame_provider_ = std::move(provider);
    Invalidate(VideoAreaRect());
}

bool MonitorSurface::HasFrame() const {
    return frame_width_ > 0 && frame_height_ > 0 &&
           ((frame_ != nullptr && !frame_->empty()) ||
            frame_provider_ != nullptr);
}

void MonitorSurface::SetStatusText(std::string text, bool is_error) {
    status_text_ = std::move(text);
    status_error_ = is_error;
    Invalidate(VideoAreaRect());
}

// ---- pushed document state --------------------------------------------------

void MonitorSurface::SetTime(double seconds) {
    if (time_ == seconds) return;
    time_ = seconds;
    // Playhead tick lives in the ruler strip — region invalidate only
    // (spec §6.5).
    Invalidate(RulerRect());
}

void MonitorSurface::SetDuration(double seconds) {
    duration_ = std::max(0.0, seconds);
    Invalidate(RulerRect());
}

void MonitorSurface::SetWorkArea(double in_seconds,
                                 double out_seconds) {
    work_in_ = in_seconds;
    work_out_ = out_seconds;
    Invalidate(RulerRect());
}

void MonitorSurface::SetMarkers(const std::vector<Marker>& markers) {
    markers_ = markers;
    Invalidate(RulerRect());
}

void MonitorSurface::SetMarks(double in_seconds, double out_seconds) {
    mark_in_ = in_seconds;
    mark_out_ = out_seconds;
    Invalidate(RulerRect());
}

void MonitorSurface::SetSpeedBadge(double rate) {
    if (speed_rate_ == rate) return;
    speed_rate_ = rate;
    Invalidate(VideoAreaRect());
}

// ---- overlay toggles --------------------------------------------------------

void MonitorSurface::SetSafeMargins(bool on) {
    safe_margins_ = on;
    Invalidate(VideoAreaRect());
}
void MonitorSurface::SetGrid(bool on) {
    grid_ = on;
    Invalidate(VideoAreaRect());
}
void MonitorSurface::SetLoop(bool on) {
    loop_ = on;
    Invalidate(RulerRect());
}
void MonitorSurface::SetLoopRange(double in_seconds,
                                  double out_seconds) {
    loop_in_ = in_seconds;
    loop_out_ = out_seconds;
    Invalidate(RulerRect());
}

// ---- zoom / pan --------------------------------------------------------------

void MonitorSurface::SetZoom(float zoom) {
    if (zoom <= 0.0f) zoom = 1.0f;
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom_ == zoom) return;
    zoom_ = zoom;
    if (zoom_ == 1.0f) {
        view_cx_ = 0.5f;
        view_cy_ = 0.5f;
    }
    if (on_zoom_changed_) on_zoom_changed_(zoom_);
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetViewCenter(float nx, float ny) {
    view_cx_ = std::clamp(nx, 0.0f, 1.0f);
    view_cy_ = std::clamp(ny, 0.0f, 1.0f);
    Invalidate(VideoAreaRect());
}

// ---- overlay inputs ----------------------------------------------------------

void MonitorSurface::SetMarkPoints(
    std::vector<std::pair<int, int>> frame_points) {
    if (mark_points_ == frame_points) return;
    mark_points_ = std::move(frame_points);
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetMaskContour(
    std::vector<Lab::IntPoint> frame_points) {
    mask_contour_in_ = std::move(frame_points);
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetMaskOverlay(
    std::shared_ptr<const std::vector<std::uint8_t>> mask, int width,
    int height) {
    mask_overlay_ = std::move(mask);
    mask_overlay_w_ = width;
    mask_overlay_h_ = height;
    // Cached tint/contour are rebuilt lazily on the next paint.
    Invalidate(VideoAreaRect());
}

void MonitorSurface::EnsureMaskOverlayBuilt(Color accent) {
    const void* key = mask_overlay_.get();
    if (key == mask_built_for_ && built_accent_[0] == accent.r &&
        built_accent_[1] == accent.g && built_accent_[2] == accent.b &&
        built_accent_[3] == accent.a) {
        return;
    }
    mask_built_for_ = key;
    built_accent_[0] = accent.r;
    built_accent_[1] = accent.g;
    built_accent_[2] = accent.b;
    built_accent_[3] = accent.a;
    mask_bgra_.clear();
    mask_contour_.clear();
    if (mask_overlay_ == nullptr || mask_overlay_w_ <= 0 ||
        mask_overlay_h_ <= 0) {
        return;
    }
    const std::size_t count =
        static_cast<std::size_t>(mask_overlay_w_) * mask_overlay_h_;
    if (mask_overlay_->size() < count) return;
    // ~40% coverage tint in premultiplied BGRA (old parity).
    mask_bgra_.resize(count * 4);
    constexpr float kFillAlpha = 0.40f;
    for (std::size_t i = 0; i < count; ++i) {
        const int a = static_cast<int>(
            static_cast<float>(mask_overlay_->at(i)) * kFillAlpha +
            0.5f);
        std::uint8_t* px = mask_bgra_.data() + i * 4;
        px[0] = static_cast<std::uint8_t>(
            accent.b * 255.0f * a / 255.0f);
        px[1] = static_cast<std::uint8_t>(
            accent.g * 255.0f * a / 255.0f);
        px[2] = static_cast<std::uint8_t>(
            accent.r * 255.0f * a / 255.0f);
        px[3] = static_cast<std::uint8_t>(a);
    }
    // Traced boundary for a crisp edge at any zoom; re-simplify
    // coarser when the raw contour would drown the paint in DrawLine
    // calls (a noisy model mask can produce thousands of vertices).
    for (const float tol : {1.5f, 4.0f, 10.0f}) {
        mask_contour_ = Lab::MaskToPolygon(*mask_overlay_,
                                           mask_overlay_w_,
                                           mask_overlay_h_, tol);
        if (mask_contour_.size() <= 512) break;
    }
}

// ---- geometry ----------------------------------------------------------------

XER::UI::Layout::Rect MonitorSurface::VideoAreaRect() const {
    const auto b = GetBounds();
    return {b.x, b.y, b.width,
            std::max(0.0f, b.height - kRulerHeight)};
}

XER::UI::Layout::Rect MonitorSurface::RulerRect() const {
    const auto b = GetBounds();
    return {b.x, b.y + std::max(0.0f, b.height - kRulerHeight),
            b.width, std::min(kRulerHeight, b.height)};
}

XER::UI::Layout::Rect MonitorSurface::FitRect() const {
    const auto video_area = VideoAreaRect();
    if (frame_width_ <= 0 || frame_height_ <= 0 ||
        video_area.width <= 0.0f || video_area.height <= 0.0f) {
        return video_area;
    }
    const float source_aspect =
        static_cast<float>(frame_width_) / frame_height_;
    const float area_aspect = video_area.width / video_area.height;
    float w = video_area.width;
    float h = video_area.height;
    if (source_aspect > area_aspect) {
        h = w / source_aspect;
    } else {
        w = h * source_aspect;
    }
    return {video_area.x + (video_area.width - w) * 0.5f,
            video_area.y + (video_area.height - h) * 0.5f, w, h};
}

XER::UI::Layout::Rect MonitorSurface::FittedFrameRect() const {
    const auto fit = FitRect();
    if (zoom_ <= 1.0f) return fit;
    // Zoomed: the fit rect scaled by zoom_, positioned so the
    // frame-normalized view center lands on the video-area center.
    const auto va = VideoAreaRect();
    const float w = fit.width * zoom_;
    const float h = fit.height * zoom_;
    return {va.x + va.width * 0.5f - view_cx_ * w,
            va.y + va.height * 0.5f - view_cy_ * h, w, h};
}

bool MonitorSurface::FrameNormPoint(XER::UI::Layout::Point p,
                                    float* nx, float* ny) const {
    const auto dest = FittedFrameRect();
    if (dest.width <= 0.0f || dest.height <= 0.0f ||
        !dest.Contains(p)) {
        return false;
    }
    const float u = (p.x - dest.x) / dest.width;
    const float v = (p.y - dest.y) / dest.height;
    if (nx != nullptr) *nx = std::clamp(u, 0.0f, 1.0f);
    if (ny != nullptr) *ny = std::clamp(v, 0.0f, 1.0f);
    return true;
}

double MonitorSurface::RulerTimeAtX(float x) const {
    const auto ruler = RulerRect();
    if (ruler.width <= 0.0f || duration_ <= 0.0) return 0.0;
    const float u =
        std::clamp((x - ruler.x) / ruler.width, 0.0f, 1.0f);
    return u * duration_;
}

std::pair<double, double> MonitorSurface::EffectiveLoopRange() const {
    if (loop_out_ > loop_in_) return {loop_in_, loop_out_};
    if (work_out_ > work_in_) return {work_in_, work_out_};
    return {0.0, 0.0};
}

const Marker* MonitorSurface::ActiveMarker() const {
    const Marker* active = nullptr;
    for (const Marker& marker : markers_) {
        if (marker.time <= time_ + 0.0005) {
            active = &marker;  // sorted by time — latest match wins
        } else {
            break;
        }
    }
    return active;
}

// ---- transform gizmo ----------------------------------------------------------
//
// Geometry mirrors WarpClipFrameRegion (compositor.cc) exactly:
//   dw = src_w * scale, ox = offset_x * canvas_w,
//   c  = (ox + dw/2, oy + dh/2)          — rotation center / anchor
//   corner_i = c + R(+theta) * (±dw/2, ±dh/2)
// in CANVAS px, then mapped through the current FittedFrameRect into
// client coords. The gizmo therefore boxes exactly what the
// compositor warps — including the live keyframe-resolved transform
// the shell pushes (preview writes come back through the same pull,
// so the box rides the drag without a feedback path of its own).

bool MonitorSurface::GizmoGeomValid(const MonitorGizmoTarget& t) const {
    const auto dest = FittedFrameRect();
    return !t.clip_id.empty() && t.src_w > 0 && t.src_h > 0 &&
           t.canvas_w > 0 && t.canvas_h > 0 &&
           t.transform.scale > 0.0f && dest.width > 0.0f &&
           dest.height > 0.0f;
}

MonitorSurface::GizmoGeom MonitorSurface::GizmoGeometry(
    const MonitorGizmoTarget& t) const {
    GizmoGeom g;
    const auto dest = FittedFrameRect();
    const double cw = t.canvas_w, ch = t.canvas_h;
    const double dw = t.src_w * static_cast<double>(t.transform.scale);
    const double dh = t.src_h * static_cast<double>(t.transform.scale);
    const double cx = t.transform.offset_x * cw + dw * 0.5;
    const double cy = t.transform.offset_y * ch + dh * 0.5;
    const double th =
        static_cast<double>(t.transform.rotation_deg) * kGizmoPi /
        180.0;
    const double cs = std::cos(th), sn = std::sin(th);
    const auto map_x = [&](double u) {
        return dest.x + static_cast<float>(u / cw) * dest.width;
    };
    const auto map_y = [&](double v) {
        return dest.y + static_cast<float>(v / ch) * dest.height;
    };
    g.c[0] = map_x(cx);
    g.c[1] = map_y(cy);
    // TL / TR / BR / BL local corners rotated +theta about c.
    const double lx[4] = {-dw * 0.5, dw * 0.5, dw * 0.5, -dw * 0.5};
    const double ly[4] = {-dh * 0.5, -dh * 0.5, dh * 0.5, dh * 0.5};
    float min_x = 1e30f, min_y = 1e30f;
    float max_x = -1e30f, max_y = -1e30f;
    for (int i = 0; i < 4; ++i) {
        const double vx = lx[i] * cs - ly[i] * sn;
        const double vy = lx[i] * sn + ly[i] * cs;
        g.pt[i][0] = map_x(cx + vx);
        g.pt[i][1] = map_y(cy + vy);
        min_x = std::min(min_x, g.pt[i][0]);
        min_y = std::min(min_y, g.pt[i][1]);
        max_x = std::max(max_x, g.pt[i][0]);
        max_y = std::max(max_y, g.pt[i][1]);
    }
    g.aabb = {min_x, min_y, max_x - min_x, max_y - min_y};
    // Rotation stem: box-local "up" is R(theta)*(0,-1) = (sn,-cs).
    const float tm_x = (g.pt[0][0] + g.pt[1][0]) * 0.5f;
    const float tm_y = (g.pt[0][1] + g.pt[1][1]) * 0.5f;
    g.rot[0] = tm_x + static_cast<float>(sn) * kGizmoStem;
    g.rot[1] = tm_y - static_cast<float>(cs) * kGizmoStem;
    // Pre-rotation half extents in client px — the local frame the
    // scale handles drag in.
    g.half_w = static_cast<float>(dw * 0.5 / cw) * dest.width;
    g.half_h = static_cast<float>(dh * 0.5 / ch) * dest.height;
    return g;
}

MonitorSurface::GizmoState MonitorSurface::ResolveGizmo() const {
    GizmoState s;
    if (id_ != Id::Program || hooks_.gizmo_target == nullptr ||
        !HasFrame()) {
        return s;
    }
    if (gizmo_mode_ != GizmoMode::None) {
        // Mid-gesture: the box draws from the live preview transform
        // (a dropped hook pull can't flicker the box mid-drag).
        s.target = gizmo_target_;
        s.target.transform = gizmo_live_;
    } else {
        // Idle and pending-select both pull fresh — the selection
        // round-trip has already landed, so the picked clip's box
        // appears under the cursor immediately.
        s.target = hooks_.gizmo_target();
    }
    s.valid = GizmoGeomValid(s.target);
    if (s.valid) {
        s.geom = GizmoGeometry(s.target);
    }
    return s;
}

bool MonitorSurface::GizmoVisible() const {
    return ResolveGizmo().valid;
}

XER::UI::Layout::Rect MonitorSurface::GizmoBounds() const {
    const GizmoState s = ResolveGizmo();
    return s.valid ? s.geom.aabb : Layout::Rect{};
}

MonitorSurface::GizmoMode MonitorSurface::GizmoHit(
    Layout::Point p, const GizmoState& s) const {
    const auto near_pt = [&](float x, float y, float r) {
        const float dx = p.x - x, dy = p.y - y;
        return dx * dx + dy * dy <= r * r;
    };
    // Rotate tip first — it sits off the box and must win over the
    // body hit below.
    if (near_pt(s.geom.rot[0], s.geom.rot[1], kGizmoHitSlop)) {
        return GizmoMode::Rotate;
    }
    // 8 handles: 4 corners + 4 edge midpoints of the rotated quad.
    static const GizmoMode kModes[8] = {
        GizmoMode::ScaleNW, GizmoMode::ScaleN,
        GizmoMode::ScaleNE, GizmoMode::ScaleE,
        GizmoMode::ScaleSE, GizmoMode::ScaleS,
        GizmoMode::ScaleSW, GizmoMode::ScaleW,
    };
    const float hx[8] = {
        s.geom.pt[0][0],
        (s.geom.pt[0][0] + s.geom.pt[1][0]) * 0.5f,
        s.geom.pt[1][0],
        (s.geom.pt[1][0] + s.geom.pt[2][0]) * 0.5f,
        s.geom.pt[2][0],
        (s.geom.pt[2][0] + s.geom.pt[3][0]) * 0.5f,
        s.geom.pt[3][0],
        (s.geom.pt[3][0] + s.geom.pt[0][0]) * 0.5f,
    };
    const float hy[8] = {
        s.geom.pt[0][1],
        (s.geom.pt[0][1] + s.geom.pt[1][1]) * 0.5f,
        s.geom.pt[1][1],
        (s.geom.pt[1][1] + s.geom.pt[2][1]) * 0.5f,
        s.geom.pt[2][1],
        (s.geom.pt[2][1] + s.geom.pt[3][1]) * 0.5f,
        s.geom.pt[3][1],
        (s.geom.pt[3][1] + s.geom.pt[0][1]) * 0.5f,
    };
    for (int i = 0; i < 8; ++i) {
        if (near_pt(hx[i], hy[i], kGizmoHitSlop)) return kModes[i];
    }
    // Body: inverse-rotate the point about the anchor and test the
    // unrotated extents (same rect the compositor fills).
    const double th =
        static_cast<double>(s.target.transform.rotation_deg) *
        kGizmoPi / 180.0;
    const double cs = std::cos(th), sn = std::sin(th);
    const double vx = p.x - s.geom.c[0], vy = p.y - s.geom.c[1];
    const double ux = vx * cs + vy * sn;
    const double uy = -vx * sn + vy * cs;
    if (std::fabs(ux) <= s.geom.half_w &&
        std::fabs(uy) <= s.geom.half_h) {
        return GizmoMode::Move;
    }
    return GizmoMode::None;
}

bool MonitorSurface::GizmoArmMove(const std::string& clip_id,
                                  Layout::Point grab) {
    if (hooks_.gizmo_target == nullptr) return false;
    MonitorGizmoTarget t = hooks_.gizmo_target();
    if (t.clip_id != clip_id || !GizmoGeomValid(t)) return false;
    gizmo_target_ = t;
    gizmo_mode_ = GizmoMode::Move;
    gizmo_orig_ = t.transform;
    gizmo_live_ = t.transform;
    gizmo_dirty_commit_ = false;
    const auto dest = FittedFrameRect();
    // Frame-normalized pointer -> center grab delta.
    const double npx = (grab.x - dest.x) / dest.width;
    const double npy = (grab.y - dest.y) / dest.height;
    const double ccx = t.transform.offset_x +
                       t.src_w * t.transform.scale / (2.0 * t.canvas_w);
    const double ccy = t.transform.offset_y +
                       t.src_h * t.transform.scale / (2.0 * t.canvas_h);
    gizmo_grab_dx_ = static_cast<float>(npx - ccx);
    gizmo_grab_dy_ = static_cast<float>(npy - ccy);
    return true;
}

// Candidate rotated-AABB in CANVAS-normalized coords for transform t.
static void GizmoAabbNorm(const MonitorGizmoTarget& target,
                          const Transform& t, float* x0, float* y0,
                          float* x1, float* y1) {
    const double dw = target.src_w * static_cast<double>(t.scale);
    const double dh = target.src_h * static_cast<double>(t.scale);
    const double cx = t.offset_x * target.canvas_w + dw * 0.5;
    const double cy = t.offset_y * target.canvas_h + dh * 0.5;
    const double th =
        static_cast<double>(t.rotation_deg) * kGizmoPi / 180.0;
    const double cs = std::cos(th), sn = std::sin(th);
    double ax0 = 1e30, ay0 = 1e30, ax1 = -1e30, ay1 = -1e30;
    const double lx[4] = {-dw * 0.5, dw * 0.5, dw * 0.5, -dw * 0.5};
    const double ly[4] = {-dh * 0.5, -dh * 0.5, dh * 0.5, dh * 0.5};
    for (int i = 0; i < 4; ++i) {
        const double px = cx + lx[i] * cs - ly[i] * sn;
        const double py = cy + lx[i] * sn + ly[i] * cs;
        ax0 = std::min(ax0, px);
        ay0 = std::min(ay0, py);
        ax1 = std::max(ax1, px);
        ay1 = std::max(ay1, py);
    }
    *x0 = static_cast<float>(ax0 / target.canvas_w);
    *y0 = static_cast<float>(ay0 / target.canvas_h);
    *x1 = static_cast<float>(ax1 / target.canvas_w);
    *y1 = static_cast<float>(ay1 / target.canvas_h);
}

void MonitorSurface::GizmoSnapMove(Transform* t) {
    gizmo_snap_x_ = -1.0f;
    gizmo_snap_y_ = -1.0f;
    if (hooks_.gizmo_snap_enabled == nullptr ||
        !hooks_.gizmo_snap_enabled()) {
        return;
    }
    const auto dest = FittedFrameRect();
    if (dest.width <= 0.0f || dest.height <= 0.0f) return;
    float x0, y0, x1, y1;
    GizmoAabbNorm(gizmo_target_, *t, &x0, &y0, &x1, &y1);
    // Frame edges + center + safe-margin lines (canvas-normalized).
    static const float kLines[7] = {0.0f, 0.05f, 0.10f, 0.5f,
                                    0.90f, 0.95f, 1.0f};
    const float thr_x = kGizmoSnapPx / dest.width;
    const float thr_y = kGizmoSnapPx / dest.height;
    const float cand_x[3] = {x0, (x0 + x1) * 0.5f, x1};
    const float cand_y[3] = {y0, (y0 + y1) * 0.5f, y1};
    float best_dx = thr_x, best_dy = thr_y;
    float line_x = -1.0f, line_y = -1.0f;
    for (const float line : kLines) {
        for (const float c : cand_x) {
            const float d = std::fabs(c - line);
            if (d <= best_dx + 1e-6f) {
                best_dx = d;
                line_x = line;
            }
        }
        for (const float c : cand_y) {
            const float d = std::fabs(c - line);
            if (d <= best_dy + 1e-6f) {
                best_dy = d;
                line_y = line;
            }
        }
    }
    if (line_x >= 0.0f) {
        // Apply the offset delta that lands the nearest candidate
        // edge on the line.
        float delta = 1e30f;
        for (const float c : cand_x) {
            if (std::fabs(c - line_x) == best_dx) {
                delta = line_x - c;
                break;
            }
        }
        if (delta < 1e29f) {
            t->offset_x += delta;
            gizmo_snap_x_ = dest.x + line_x * dest.width;
        }
    }
    if (line_y >= 0.0f) {
        float delta = 1e30f;
        for (const float c : cand_y) {
            if (std::fabs(c - line_y) == best_dy) {
                delta = line_y - c;
                break;
            }
        }
        if (delta < 1e29f) {
            t->offset_y += delta;
            gizmo_snap_y_ = dest.y + line_y * dest.height;
        }
    }
}

void MonitorSurface::GizmoSnapEdge(Transform* t, bool x_axis) {
    if (hooks_.gizmo_snap_enabled == nullptr ||
        !hooks_.gizmo_snap_enabled()) {
        return;
    }
    const auto dest = FittedFrameRect();
    if (dest.width <= 0.0f || dest.height <= 0.0f) return;
    float x0, y0, x1, y1;
    GizmoAabbNorm(gizmo_target_, *t, &x0, &y0, &x1, &y1);
    static const float kLines[7] = {0.0f, 0.05f, 0.10f, 0.5f,
                                    0.90f, 0.95f, 1.0f};
    // The dragged edge only: E/W drags snap the right/left edge,
    // N/S the bottom/top — sign picked by which side the handle is.
    const bool far_side =
        gizmo_mode_ == GizmoMode::ScaleE ||
        gizmo_mode_ == GizmoMode::ScaleS ||
        gizmo_mode_ == GizmoMode::ScaleSE ||
        gizmo_mode_ == GizmoMode::ScaleNE;
    const float edge = x_axis ? (far_side ? x1 : x0)
                              : (far_side ? y1 : y0);
    const float thr =
        (x_axis ? kGizmoSnapPx / dest.width
                : kGizmoSnapPx / dest.height);
    float best = thr, line = -1.0f;
    for (const float l : kLines) {
        const float d = std::fabs(edge - l);
        if (d <= best + 1e-6f) {
            best = d;
            line = l;
        }
    }
    if (line < 0.0f) return;
    if (x_axis) {
        t->offset_x += line - edge;
        gizmo_snap_x_ = dest.x + line * dest.width;
    } else {
        t->offset_y += line - edge;
        gizmo_snap_y_ = dest.y + line * dest.height;
    }
}

Transform MonitorSurface::GizmoApplyDrag(
    Layout::Point p, const W::Event& e) {
    Transform t = gizmo_orig_;
    const auto& target = gizmo_target_;
    const auto dest = FittedFrameRect();
    if (dest.width <= 0.0f || dest.height <= 0.0f ||
        target.canvas_w <= 0 || target.canvas_h <= 0) {
        return t;
    }
    const double cw = target.canvas_w, ch = target.canvas_h;
    gizmo_snap_x_ = -1.0f;
    gizmo_snap_y_ = -1.0f;

    if (gizmo_mode_ == GizmoMode::Move) {
        const double npx = (p.x - dest.x) / dest.width;
        const double npy = (p.y - dest.y) / dest.height;
        const double ccx = npx - gizmo_grab_dx_;
        const double ccy = npy - gizmo_grab_dy_;
        const double dw = target.src_w * t.scale;
        const double dh = target.src_h * t.scale;
        t.offset_x = static_cast<float>(ccx - dw / (2.0 * cw));
        t.offset_y = static_cast<float>(ccy - dh / (2.0 * ch));
        GizmoSnapMove(&t);
        return t;
    }

    const auto geom = GizmoGeometry(
        MonitorGizmoTarget{target.clip_id, gizmo_orig_, target.src_w,
                           target.src_h, target.canvas_w,
                           target.canvas_h});

    if (gizmo_mode_ == GizmoMode::Rotate) {
        const double ang = std::atan2(p.y - geom.c[1],
                                      p.x - geom.c[0]);
        float deg = gizmo_orig_.rotation_deg +
                    static_cast<float>(ang - gizmo_grab_dx_) *
                        static_cast<float>(180.0 / kGizmoPi);
        if (e.is_shift) {
            deg = std::round(deg / kGizmoRotSnapDeg) *
                  kGizmoRotSnapDeg;
        }
        t.rotation_deg = deg;
        return t;
    }

    // Scale: pointer distance to the anchor in the box's LOCAL
    // (pre-rotation) frame — corners scale by radial ratio, edges by
    // that axis's local component. The anchor (transform center)
    // stays fixed: offset is recomputed from the new dest size.
    const double th =
        static_cast<double>(gizmo_orig_.rotation_deg) * kGizmoPi /
        180.0;
    const double cs = std::cos(th), sn = std::sin(th);
    const auto to_local = [&](Layout::Point q, double* lx,
                              double* ly) {
        const double vx = q.x - geom.c[0], vy = q.y - geom.c[1];
        *lx = vx * cs + vy * sn;
        *ly = -vx * sn + vy * cs;
    };
    double lx_p = 0.0, ly_p = 0.0, lx_d = 0.0, ly_d = 0.0;
    to_local(p, &lx_p, &ly_p);
    to_local(down_pos_, &lx_d, &ly_d);
    double ratio = 1.0;
    switch (gizmo_mode_) {
        case GizmoMode::ScaleE:
        case GizmoMode::ScaleW:
            if (std::fabs(lx_d) > 1e-6) ratio = lx_p / lx_d;
            break;
        case GizmoMode::ScaleN:
        case GizmoMode::ScaleS:
            if (std::fabs(ly_d) > 1e-6) ratio = ly_p / ly_d;
            break;
        default: {   // corners — radial ratio
            const double rd = std::hypot(lx_d, ly_d);
            if (rd > 1e-6) {
                ratio = std::hypot(lx_p, ly_p) / rd;
            }
            break;
        }
    }
    const float scale = std::clamp(
        static_cast<float>(
            static_cast<double>(gizmo_orig_.scale) * ratio),
        kGizmoMinScale, kGizmoMaxScale);
    t.scale = scale;
    // Hold the anchor fixed in canvas space.
    const double ccx =
        gizmo_orig_.offset_x * cw +
        target.src_w * gizmo_orig_.scale * 0.5;
    const double ccy =
        gizmo_orig_.offset_y * ch +
        target.src_h * gizmo_orig_.scale * 0.5;
    t.offset_x = static_cast<float>(
        (ccx - target.src_w * scale * 0.5) / cw);
    t.offset_y = static_cast<float>(
        (ccy - target.src_h * scale * 0.5) / ch);
    // Edge midpoint drags additionally snap the dragged edge (the
    // model's uniform scale can't grow a side in place, so the snap
    // nudges the position to land the edge on the guide).
    if (gizmo_mode_ == GizmoMode::ScaleE ||
        gizmo_mode_ == GizmoMode::ScaleW) {
        GizmoSnapEdge(&t, /*x_axis=*/true);
    } else if (gizmo_mode_ == GizmoMode::ScaleN ||
               gizmo_mode_ == GizmoMode::ScaleS) {
        GizmoSnapEdge(&t, /*x_axis=*/false);
    }
    return t;
}

void MonitorSurface::GizmoCancel() {
    // Restore the pre-gesture preview, then close the macro — no
    // commit fires. A press that never crossed the drag threshold
    // never opened the gesture (begin/end stay paired).
    if (gizmo_gesture_started_) {
        if (hooks_.on_gizmo_preview) {
            hooks_.on_gizmo_preview(gizmo_target_.clip_id,
                                    gizmo_orig_);
        }
        if (hooks_.on_gizmo_end) hooks_.on_gizmo_end();
    }
    gizmo_mode_ = GizmoMode::None;
    gizmo_gesture_started_ = false;
    gizmo_pending_select_ = false;
    gizmo_pending_clip_id_.clear();
    gizmo_snap_x_ = gizmo_snap_y_ = -1.0f;
    gizmo_dirty_commit_ = false;
    Invalidate(VideoAreaRect());
}

void MonitorSurface::GizmoFinish(bool commit) {
    if (gizmo_gesture_started_) {
        if (commit && gizmo_dirty_commit_ &&
            hooks_.on_gizmo_commit) {
            hooks_.on_gizmo_commit(gizmo_target_.clip_id,
                                   gizmo_live_);
        }
        if (hooks_.on_gizmo_end) hooks_.on_gizmo_end();
    }
    gizmo_mode_ = GizmoMode::None;
    gizmo_gesture_started_ = false;
    gizmo_pending_select_ = false;
    gizmo_pending_clip_id_.clear();
    gizmo_snap_x_ = gizmo_snap_y_ = -1.0f;
    gizmo_dirty_commit_ = false;
    Invalidate(VideoAreaRect());
}

void MonitorSurface::DrawGizmo(Rendering::Renderer& renderer,
                               const Theme::Tokens& tokens,
                               const GizmoState& s) {
    const Color box = WithAlpha(tokens.accent, 0.95f);
    const Color handle_fill{1.0f, 1.0f, 1.0f, 0.96f};
    const auto line = [&](float x0, float y0, float x1, float y1,
                          Color c, float w) {
        renderer.DrawLine({x0, y0}, {x1, y1}, c, w);
    };
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        line(s.geom.pt[i][0], s.geom.pt[i][1], s.geom.pt[j][0],
             s.geom.pt[j][1], box, 1.5f);
    }
    // Rotation stem + tip handle.
    const float tm_x = (s.geom.pt[0][0] + s.geom.pt[1][0]) * 0.5f;
    const float tm_y = (s.geom.pt[0][1] + s.geom.pt[1][1]) * 0.5f;
    line(tm_x, tm_y, s.geom.rot[0], s.geom.rot[1], box, 1.5f);
    const auto handle = [&](float x, float y) {
        const Layout::Rect r{x - kGizmoHandleHalf,
                             y - kGizmoHandleHalf,
                             kGizmoHandleHalf * 2.0f,
                             kGizmoHandleHalf * 2.0f};
        renderer.FillRect(r, handle_fill, 1.0f);
        renderer.StrokeRect(r, tokens.accent, 1.0f, 0.0f);
    };
    handle(s.geom.rot[0], s.geom.rot[1]);
    // 8 scale handles.
    const float hx[8] = {
        s.geom.pt[0][0],
        (s.geom.pt[0][0] + s.geom.pt[1][0]) * 0.5f,
        s.geom.pt[1][0],
        (s.geom.pt[1][0] + s.geom.pt[2][0]) * 0.5f,
        s.geom.pt[2][0],
        (s.geom.pt[2][0] + s.geom.pt[3][0]) * 0.5f,
        s.geom.pt[3][0],
        (s.geom.pt[3][0] + s.geom.pt[0][0]) * 0.5f,
    };
    const float hy[8] = {
        s.geom.pt[0][1],
        (s.geom.pt[0][1] + s.geom.pt[1][1]) * 0.5f,
        s.geom.pt[1][1],
        (s.geom.pt[1][1] + s.geom.pt[2][1]) * 0.5f,
        s.geom.pt[2][1],
        (s.geom.pt[2][1] + s.geom.pt[3][1]) * 0.5f,
        s.geom.pt[3][1],
        (s.geom.pt[3][1] + s.geom.pt[0][1]) * 0.5f,
    };
    for (int i = 0; i < 8; ++i) handle(hx[i], hy[i]);
    // Anchor: the transform center the compositor rotates about —
    // small accent cross + center dot (AE anchor-point parity).
    const float cx = s.geom.c[0], cy = s.geom.c[1];
    constexpr float arm = 6.0f;
    line(cx - arm, cy, cx + arm, cy, box, 1.0f);
    line(cx, cy - arm, cx, cy + arm, box, 1.0f);
    renderer.FillRect({cx - 1.5f, cy - 1.5f, 3.0f, 3.0f}, handle_fill,
                      1.0f);
    // Snap guides — full-frame accent lines at the winning candidate
    // (timeline magnet-line parity).
    const auto dest = FittedFrameRect();
    if (gizmo_snap_x_ >= 0.0f) {
        renderer.DrawLine({gizmo_snap_x_, dest.y},
                          {gizmo_snap_x_, dest.y + dest.height},
                          kGizmoSnapColor, 1.5f);
    }
    if (gizmo_snap_y_ >= 0.0f) {
        renderer.DrawLine({dest.x, gizmo_snap_y_},
                          {dest.x + dest.width, gizmo_snap_y_},
                          kGizmoSnapColor, 1.5f);
    }
}

// ---- paint -------------------------------------------------------------------

void MonitorSurface::Paint(Rendering::Renderer& renderer,
                           const Layout::Rect& content,
                           const Theme::Tokens& tokens) {
    const auto video_area = VideoAreaRect();
    (void)content;  // == GetBounds() (no content size on the kit
                    // surface — the letterbox is ours)

    // Pull path wins when installed — the provider owns "what frame
    // now" (fake providers in smoke; push path otherwise).
    Frame provided;
    const std::vector<std::uint8_t>* pixels = frame_.get();
    if (frame_provider_ != nullptr) {
        provided = frame_provider_();
        if (provided.bgra != nullptr && provided.width > 0 &&
            provided.height > 0 &&
            provided.bgra->size() / 4u >=
                static_cast<std::size_t>(provided.width) *
                    static_cast<std::size_t>(provided.height)) {
            pixels = provided.bgra.get();
            frame_width_ = provided.width;
            frame_height_ = provided.height;
        }
    }

    renderer.FillRect(video_area, tokens.background,
                      tokens.radius_medium);
    renderer.StrokeRect(video_area, tokens.border, 1.0f,
                        tokens.radius_medium);

    if (pixels != nullptr && !pixels->empty() && frame_width_ > 0 &&
        frame_height_ > 0 &&
        frame_px_offset_ + static_cast<std::size_t>(frame_width_) *
                                   frame_height_ * 4 <=
                               pixels->size()) {
        const auto dest = FittedFrameRect();
        renderer.DrawFrameBgra(pixels->data() + frame_px_offset_,
                               frame_width_, frame_height_, dest,
                               1.0f);
        renderer.StrokeRect(dest, tokens.border, 1.0f, 0.0f);
        DrawOverlays(renderer, tokens, dest);
        // Transform gizmo — over the frame/overlays, inside the
        // video area. No-ops when the shell binds no gizmo_target
        // (the source monitor never does).
        if (const GizmoState gizmo = ResolveGizmo(); gizmo.valid) {
            DrawGizmo(renderer, tokens, gizmo);
        }
    } else {
        // Centered empty state — DrawText is top-left aligned, so
        // measure first and hand it a box centered in the video area.
        const std::string_view text =
            status_text_.empty()
                ? std::string_view{"No video"}
                : std::string_view{status_text_};
        const Rendering::TextStyle st{
            status_error_ ? tokens.danger : tokens.muted,
            tokens.font_body, 500};
        const auto ts = renderer.MeasureText(text, st);
        const Layout::Rect tr{
            video_area.x +
                std::max(0.0f, (video_area.width - ts.width) * 0.5f),
            video_area.y +
                std::max(0.0f, (video_area.height - ts.height) * 0.5f),
            std::min(ts.width, video_area.width),
            std::min(ts.height, video_area.height)};
        renderer.DrawText(text, tr, st);
    }

    // Speed badge — top-left chip while the applied rate is anything
    // but stopped or plain +1x (JKL shuttle, K-held slow shuttle,
    // reverse). The shell pushes the raw rate; 0/+1 hides.
    if (speed_rate_ != 0.0 && speed_rate_ != 1.0) {
        char badge[24];
        std::snprintf(badge, sizeof(badge), "%gx", speed_rate_);
        const Rendering::TextStyle badge_style{tokens.accent,
                                               tokens.font_body, 700};
        const auto bs = renderer.MeasureText(badge, badge_style);
        const Layout::Rect chip{video_area.x + 8.0f,
                                video_area.y + 8.0f,
                                bs.width + 14.0f, bs.height + 8.0f};
        renderer.FillRect(chip, WithAlpha(tokens.background, 0.72f),
                          tokens.radius_small);
        renderer.DrawText(badge,
                          {chip.x + 7.0f, chip.y + 4.0f, bs.width,
                           bs.height},
                          badge_style);
    }

    DrawRuler(renderer, tokens, RulerRect());
}

void MonitorSurface::DrawOverlays(Rendering::Renderer& renderer,
                                  const Theme::Tokens& tokens,
                                  const Layout::Rect& dest) {
    if (dest.width <= 0.0f || dest.height <= 0.0f) return;

    if (grid_) {
        const Color line = {1.0f, 1.0f, 1.0f, 0.30f};
        for (int i = 1; i <= 2; ++i) {
            const float x = dest.x + dest.width * (i / 3.0f);
            const float y = dest.y + dest.height * (i / 3.0f);
            renderer.DrawLine({x, dest.y}, {x, dest.y + dest.height},
                              line, 1.0f);
            renderer.DrawLine({dest.x, y}, {dest.x + dest.width, y},
                              line, 1.0f);
        }
    }

    if (safe_margins_) {
        // Action-safe 90% (outer) + title-safe 80% (inner) + cross.
        const Color action_color = {1.0f, 1.0f, 1.0f, 0.55f};
        const Color title_color = WithAlpha(tokens.accent, 0.8f);
        const Layout::Rect action{
            dest.x + dest.width * 0.05f, dest.y + dest.height * 0.05f,
            dest.width * 0.90f, dest.height * 0.90f};
        const Layout::Rect title{
            dest.x + dest.width * 0.10f,
            dest.y + dest.height * 0.10f,
            dest.width * 0.80f, dest.height * 0.80f};
        renderer.StrokeRect(action, action_color, 1.0f, 0.0f);
        renderer.StrokeRect(title, title_color, 1.0f, 0.0f);
        const float cx = dest.x + dest.width * 0.5f;
        const float cy = dest.y + dest.height * 0.5f;
        const float arm =
            std::min(dest.width, dest.height) * 0.02f + 6.0f;
        renderer.DrawLine({cx - arm, cy}, {cx + arm, cy}, title_color,
                          1.0f);
        renderer.DrawLine({cx, cy - arm}, {cx, cy + arm}, title_color,
                          1.0f);
    }

    // Stored AI selection mask — tint + traced boundary, only while
    // the stored dims match the shown frame (stale masks are skipped,
    // never misapplied — same rule as the old monitor).
    if (mask_overlay_ != nullptr && frame_width_ > 0 &&
        frame_height_ > 0 && mask_overlay_w_ == frame_width_ &&
        mask_overlay_h_ == frame_height_ &&
        mask_overlay_->size() >=
            static_cast<std::size_t>(frame_width_) * frame_height_) {
        EnsureMaskOverlayBuilt(tokens.accent);
        if (!mask_bgra_.empty()) {
            renderer.DrawFrameBgra(mask_bgra_.data(), mask_overlay_w_,
                                   mask_overlay_h_, dest, 1.0f);
        }
        if (mask_contour_.size() >= 2) {
            const Color edge = WithAlpha(tokens.accent, 0.9f);
            const float sx =
                dest.width / static_cast<float>(frame_width_);
            const float sy =
                dest.height / static_cast<float>(frame_height_);
            for (std::size_t i = 0; i < mask_contour_.size(); ++i) {
                const Lab::IntPoint& a = mask_contour_[i];
                const Lab::IntPoint& b =
                    mask_contour_[(i + 1) % mask_contour_.size()];
                renderer.DrawLine(
                    {dest.x + static_cast<float>(a.x) * sx,
                     dest.y + static_cast<float>(a.y) * sy},
                    {dest.x + static_cast<float>(b.x) * sx,
                     dest.y + static_cast<float>(b.y) * sy},
                    edge, 1.0f);
            }
        }
    }

    // Explicit mask contour (hook input — no coverage fill needed).
    if (mask_contour_in_.size() >= 2 && frame_width_ > 0 &&
        frame_height_ > 0) {
        const Color edge = WithAlpha(tokens.accent, 0.9f);
        const float sx =
            dest.width / static_cast<float>(frame_width_);
        const float sy =
            dest.height / static_cast<float>(frame_height_);
        for (std::size_t i = 0; i < mask_contour_in_.size(); ++i) {
            const Lab::IntPoint& a = mask_contour_in_[i];
            const Lab::IntPoint& b =
                mask_contour_in_[(i + 1) % mask_contour_in_.size()];
            renderer.DrawLine(
                {dest.x + static_cast<float>(a.x) * sx,
                 dest.y + static_cast<float>(a.y) * sy},
                {dest.x + static_cast<float>(b.x) * sx,
                 dest.y + static_cast<float>(b.y) * sy},
                edge, 1.0f);
        }
    }

    // AI click points: frame-px markers mapped through the (possibly
    // zoomed) dest rect — accent cross + white center dot.
    if (!mark_points_.empty() && frame_width_ > 0 &&
        frame_height_ > 0) {
        const Color ring = WithAlpha(tokens.accent, 0.95f);
        const Color dot = {1.0f, 1.0f, 1.0f, 0.95f};
        for (const auto& [px, py] : mark_points_) {
            const float sx =
                dest.x + (static_cast<float>(px) + 0.5f) /
                             frame_width_ * dest.width;
            const float sy =
                dest.y + (static_cast<float>(py) + 0.5f) /
                             frame_height_ * dest.height;
            constexpr float r = 6.0f;
            renderer.DrawLine({sx - r, sy}, {sx + r, sy}, ring, 2.0f);
            renderer.DrawLine({sx, sy - r}, {sx, sy + r}, ring, 2.0f);
            renderer.FillRect({sx - 1.5f, sy - 1.5f, 3.0f, 3.0f}, dot,
                              1.0f);
        }
    }
}

void MonitorSurface::DrawRuler(Rendering::Renderer& renderer,
                               const Theme::Tokens& tokens,
                               const Layout::Rect& ruler) {
    renderer.FillRect(ruler, tokens.background, tokens.radius_small);
    if (duration_ <= 0.0 || ruler.width <= 0.0f) return;
    const auto x_at = [&](double t) {
        return ruler.x +
               static_cast<float>(t / duration_) * ruler.width;
    };

    // Work-area in/out brackets.
    if (work_out_ > work_in_) {
        const float x0 = x_at(work_in_);
        const float x1 = x_at(work_out_);
        renderer.FillRect({x0, ruler.y + 4.0f, x1 - x0,
                           ruler.height - 8.0f},
                          WithAlpha(tokens.accent, 0.30f), 1.0f);
        renderer.DrawLine({x0, ruler.y + 1.0f},
                          {x0, ruler.y + ruler.height - 1.0f},
                          tokens.accent, 2.0f);
        renderer.DrawLine({x1, ruler.y + 1.0f},
                          {x1, ruler.y + ruler.height - 1.0f},
                          tokens.accent, 2.0f);
    }

    // Source marks (in/out brackets) — amber, distinct from the
    // accent work area (old source-monitor parity).
    if (mark_in_ >= 0.0) {
        const float x = x_at(std::clamp(mark_in_, 0.0, duration_));
        renderer.DrawLine({x, ruler.y + 1.0f},
                          {x, ruler.y + ruler.height - 1.0f},
                          tokens.warning_chip, 2.0f);
    }
    if (mark_out_ >= 0.0) {
        const float x = x_at(std::clamp(mark_out_, 0.0, duration_));
        renderer.DrawLine({x, ruler.y + 1.0f},
                          {x, ruler.y + ruler.height - 1.0f},
                          tokens.warning_chip, 2.0f);
    }

    // Loop range band (explicit range, else work area, else whole).
    if (loop_) {
        auto [li, lo] = EffectiveLoopRange();
        if (lo <= li) {
            li = 0.0;
            lo = duration_;
        }
        li = std::clamp(li, 0.0, duration_);
        lo = std::clamp(lo, 0.0, duration_);
        const float x0 = x_at(li);
        const float x1 = x_at(lo);
        renderer.FillRect(
            {x0, ruler.y + ruler.height - 5.0f, x1 - x0, 4.0f},
            WithAlpha(tokens.focus, 0.65f), 1.0f);
    }

    // Marker flags.
    for (const Marker& marker : markers_) {
        if (marker.time < 0.0 || marker.time > duration_) continue;
        const float x = x_at(marker.time);
        renderer.FillRect({x - 1.0f, ruler.y + 1.0f, 2.0f,
                           ruler.height - 2.0f},
                          MarkerColor(marker), 0.0f);
        renderer.FillRect({x, ruler.y + 1.0f, 4.0f, 4.0f},
                          MarkerColor(marker), 0.0f);
    }

    // Playhead tick.
    const float px = x_at(std::clamp(time_, 0.0, duration_));
    renderer.DrawLine({px, ruler.y}, {px, ruler.y + ruler.height},
                      tokens.playhead, 1.0f);

    // Active marker name at the right end of the strip.
    if (const Marker* active = ActiveMarker();
        active != nullptr && !active->name.empty()) {
        const Rendering::TextStyle style{MarkerColor(*active),
                                         tokens.font_small, 600};
        const auto size = renderer.MeasureText(active->name, style);
        const Layout::Rect rect{
            ruler.x + ruler.width - size.width - 4.0f,
            ruler.y + (ruler.height - size.height) * 0.5f,
            size.width + 4.0f, size.height};
        renderer.FillRect(rect, {0.0f, 0.0f, 0.0f, 0.5f},
                          tokens.radius_small);
        renderer.DrawText(active->name, rect, style);
    }
}

// ---- events -------------------------------------------------------------------

bool MonitorSurface::OnSurfaceEvent(const W::Event& event) {
    using Type = W::Event::Type;
    using Btn = W::MouseButton;
    const auto p = event.position;

    if (event.type == Type::MouseDown &&
        (event.mouse_button == Btn::Left ||
         event.mouse_button == Btn::Middle)) {
        Activate();  // last-active-viewer rule — any press claims the
                     // transport scope.
        left_down_ = event.mouse_button == Btn::Left;
        drag_moved_ = false;
        down_pos_ = p;
        last_pan_pos_ = p;
        if (RulerRect().Contains(p) && event.mouse_button == Btn::Left) {
            ruler_drag_ = true;
            if (hooks_.on_time_changed) {
                hooks_.on_time_changed(RulerTimeAtX(p.x));
            }
            return true;
        }
        // Transform gizmo (program only): armed click points keep
        // priority — the whole gizmo path is skipped while the
        // SmartSelect seam is armed.
        if (event.mouse_button == Btn::Left &&
            !click_points_armed_ && VideoAreaRect().Contains(p)) {
            const auto dest = FittedFrameRect();
            const GizmoState gizmo = ResolveGizmo();
            const GizmoMode mode =
                gizmo.valid ? GizmoHit(p, gizmo) : GizmoMode::None;
            if (mode != GizmoMode::None) {
                gizmo_target_ = gizmo.target;
                gizmo_mode_ = mode;
                gizmo_orig_ = gizmo.target.transform;
                gizmo_live_ = gizmo.target.transform;
                gizmo_dirty_commit_ = false;
                gizmo_click_ = true;
                // Esc-cancel routing: the kit Surface child isn't
                // focusable, so the drag's KeyDown would never reach
                // us — take focus ourselves (kPointer: no ring).
                SetFocused(true, W::FocusReason::kPointer);
                if (mode == GizmoMode::Move) {
                    // Track by the grabbed point, not the box
                    // center (frame-normalized grab delta).
                    const auto& t = gizmo_orig_;
                    gizmo_grab_dx_ = static_cast<float>(
                        (p.x - dest.x) / dest.width -
                        (t.offset_x +
                         gizmo_target_.src_w * t.scale /
                             (2.0 * gizmo_target_.canvas_w)));
                    gizmo_grab_dy_ = static_cast<float>(
                        (p.y - dest.y) / dest.height -
                        (t.offset_y +
                         gizmo_target_.src_h * t.scale /
                             (2.0 * gizmo_target_.canvas_h)));
                } else if (mode == GizmoMode::Rotate) {
                    gizmo_grab_dx_ = static_cast<float>(std::atan2(
                        p.y - gizmo.geom.c[1],
                        p.x - gizmo.geom.c[0]));
                }
                // on_gizmo_begin defers to the first post-threshold
                // move — a plain click on the box stays a click.
                panning_ = false;
                return true;
            }
            // No gizmo hit — press-to-select: the shell maps the
            // frame point to the topmost clip under the cursor at
            // the playhead ("" = nothing selectable there).
            if (hooks_.gizmo_pick != nullptr &&
                dest.width > 0.0f && dest.height > 0.0f) {
                const std::string picked = hooks_.gizmo_pick(
                    (p.x - dest.x) / dest.width,
                    (p.y - dest.y) / dest.height);
                if (!picked.empty()) {
                    gizmo_pending_select_ = true;
                    gizmo_pending_clip_id_ = picked;
                    gizmo_click_ = true;
                    // Same Esc routing as a direct handle grab —
                    // this press can still promote into a drag.
                    SetFocused(true, W::FocusReason::kPointer);
                    if ((!gizmo.valid ||
                         picked != gizmo.target.clip_id) &&
                        hooks_.on_gizmo_select) {
                        hooks_.on_gizmo_select(picked);
                    }
                    panning_ = false;
                    return true;
                }
            }
        }
        panning_ = VideoAreaRect().Contains(p) &&
                   (event.mouse_button == Btn::Middle || Zoomed());
        return true;
    }

    if (event.type == Type::MouseMove) {
        if (ruler_drag_ && left_down_) {
            if (hooks_.on_time_changed) {
                hooks_.on_time_changed(RulerTimeAtX(p.x));
            }
            return true;
        }
        // Gizmo drag — claims the move before pan/view handling.
        if (left_down_ &&
            (gizmo_mode_ != GizmoMode::None ||
             gizmo_pending_select_)) {
            const float dx = p.x - down_pos_.x;
            const float dy = p.y - down_pos_.y;
            if (std::fabs(dx) + std::fabs(dy) > kDragThreshold) {
                drag_moved_ = true;
            }
            if (gizmo_pending_select_ && drag_moved_) {
                // Press-to-select promotes into a Move once the
                // threshold is crossed (selection round-trip has
                // already landed via on_gizmo_select at press).
                if (!GizmoArmMove(gizmo_pending_clip_id_,
                                  down_pos_)) {
                    gizmo_pending_select_ = false;
                    gizmo_pending_clip_id_.clear();
                    gizmo_click_ = false;
                } else {
                    gizmo_pending_select_ = false;
                    gizmo_pending_clip_id_.clear();
                }
            }
            if (gizmo_mode_ != GizmoMode::None) {
                // The gesture begins (shell macro + preview
                // compositor) only once real motion accrues.
                if (!gizmo_gesture_started_ && drag_moved_) {
                    gizmo_gesture_started_ = true;
                    if (hooks_.on_gizmo_begin) {
                        hooks_.on_gizmo_begin("monitor transform");
                    }
                }
                if (!gizmo_gesture_started_) return true;
                const Transform t = GizmoApplyDrag(p, event);
                if (t.offset_x != gizmo_live_.offset_x ||
                    t.offset_y != gizmo_live_.offset_y ||
                    t.scale != gizmo_live_.scale ||
                    t.rotation_deg != gizmo_live_.rotation_deg) {
                    gizmo_live_ = t;
                    gizmo_dirty_commit_ =
                        t.offset_x != gizmo_orig_.offset_x ||
                        t.offset_y != gizmo_orig_.offset_y ||
                        t.scale != gizmo_orig_.scale ||
                        t.rotation_deg != gizmo_orig_.rotation_deg;
                    if (hooks_.on_gizmo_preview) {
                        hooks_.on_gizmo_preview(
                            gizmo_target_.clip_id, t);
                    }
                    Invalidate(VideoAreaRect());
                }
            }
            return true;
        }
        if (left_down_ || panning_) {
            const float dx = p.x - down_pos_.x;
            const float dy = p.y - down_pos_.y;
            if (std::fabs(dx) + std::fabs(dy) > kDragThreshold) {
                drag_moved_ = true;
            }
            if (panning_ && drag_moved_) {
                // Screen-px drag -> frame-normalized view-center shift.
                const auto fit = FitRect();
                if (fit.width > 0.0f && fit.height > 0.0f) {
                    const float denom_x = fit.width * zoom_;
                    const float denom_y = fit.height * zoom_;
                    SetViewCenter(
                        view_cx_ - (p.x - last_pan_pos_.x) / denom_x,
                        view_cy_ - (p.y - last_pan_pos_.y) / denom_y);
                }
                last_pan_pos_ = p;
            }
            return true;
        }
        return false;
    }

    if (event.type == Type::MouseUp) {
        if (event.mouse_button == Btn::Left) {
            left_down_ = false;
            ruler_drag_ = false;
            // Gizmo gesture close-out: exactly ONE commit on release
            // (skipped for a no-change drag), then the macro's end.
            if (gizmo_mode_ != GizmoMode::None ||
                gizmo_pending_select_) {
                GizmoFinish(/*commit=*/true);
            }
        }
        if (event.mouse_button == Btn::Middle ||
            event.mouse_button == Btn::Left) {
            panning_ = false;
        }
        return false;   // let the tree synthesize the Click
    }

    if (event.type == Type::MouseWheel) {
        if (!VideoAreaRect().Contains(p) || frame_width_ <= 0 ||
            frame_height_ <= 0) {
            return false;
        }
        // Zoom at the cursor: keep the frame-normalized point under
        // the cursor fixed across the zoom change.
        Activate();
        const auto dest = FittedFrameRect();
        const auto fit = FitRect();
        const float nx = dest.width > 0.0f
            ? (p.x - dest.x) / dest.width : 0.5f;
        const float ny = dest.height > 0.0f
            ? (p.y - dest.y) / dest.height : 0.5f;
        const int steps = event.wheel_delta / 120;
        float zoom = zoom_;
        for (int i = 0; i < std::abs(steps); ++i) {
            zoom *= steps > 0 ? kZoomStep : 1.0f / kZoomStep;
        }
        zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        if (zoom == zoom_) return true;
        zoom_ = zoom;
        if (zoom_ <= 1.0f) {
            view_cx_ = 0.5f;
            view_cy_ = 0.5f;
        } else {
            // Solve for the view center that keeps (nx,ny) under p.
            const auto va = VideoAreaRect();
            const float w = fit.width * zoom_;
            const float h = fit.height * zoom_;
            SetViewCenter(
                (va.x + va.width * 0.5f - p.x) / w + nx,
                (va.y + va.height * 0.5f - p.y) / h + ny);
        }
        if (on_zoom_changed_) on_zoom_changed_(zoom_);
        Invalidate(VideoAreaRect());
        return true;
    }

    if (event.type == Type::Click && event.mouse_button == Btn::Left) {
        if (drag_moved_) {
            drag_moved_ = false;   // a pan/ruler scrub, not a click
            gizmo_click_ = false;  // a completed gizmo drag consumes
                                   // its claim — the next click is
                                   // clean again
            return true;
        }
        if (RulerRect().Contains(p)) {
            if (hooks_.on_time_changed) {
                hooks_.on_time_changed(RulerTimeAtX(p.x));
            }
            return true;
        }
        // A press the gizmo path claimed never toggles playback;
        // an inside-box double-click resets the transform instead
        // (the in-frame double-click zoom reset still applies
        // outside the box).
        if (gizmo_click_) {
            gizmo_click_ = false;
            const GizmoState gizmo = ResolveGizmo();
            if (gizmo.valid &&
                GizmoHit(p, gizmo) != GizmoMode::None) {
                if (click_clock_ - last_gizmo_click_at_ <=
                    kDoubleClickSeconds) {
                    last_gizmo_click_at_ = -10.0;
                    if (hooks_.on_gizmo_reset) {
                        hooks_.on_gizmo_reset(gizmo.target.clip_id);
                    }
                } else {
                    last_gizmo_click_at_ = click_clock_;
                }
            }
            return true;
        }
        // Inside the fitted frame: double-click resets the zoom;
        // armed click points report normalized frame coords;
        // otherwise the click toggles playback (old parity).
        float nx = 0.0f, ny = 0.0f;
        const bool in_frame = FrameNormPoint(p, &nx, &ny);
        if (in_frame) {
            if (click_clock_ - last_frame_click_at_ <=
                kDoubleClickSeconds) {
                last_frame_click_at_ = -10.0;
                ZoomFit();
                return true;
            }
            last_frame_click_at_ = click_clock_;
            if (click_points_armed_ && hooks_.on_click_point) {
                hooks_.on_click_point(nx, ny);
                return true;
            }
            if (hooks_.on_toggle_play) {
                hooks_.on_toggle_play();
                return true;
            }
        }
        // Presses elsewhere in the video area still claim the widget.
        return GetBounds().Contains(p);
    }

    if (event.type == Type::Click &&
        event.mouse_button == Btn::Right) {
        // Chrome-level menus live on the panel — let the event bubble
        // so the tree's context-menu handler can take it.
        return false;
    }

    // Esc cancels an in-flight gizmo gesture: the original transform
    // is re-previewed, the macro closes, nothing commits.
    if (event.type == Type::KeyDown &&
        event.key == W::KeyCode::Escape &&
        (gizmo_mode_ != GizmoMode::None || gizmo_pending_select_)) {
        GizmoCancel();
        gizmo_click_ = false;
        return true;
    }

    return false;
}

// ---- widget contract -----------------------------------------------------------

void MonitorSurface::Render(Rendering::Renderer& renderer,
                            const Theme::Tokens& tokens) {
    // The kit Surface child paints edge-to-edge (frame + overlays +
    // ruler); nothing of ours sits behind it.
    (void)renderer;
    (void)tokens;
}

void MonitorSurface::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (surface_ != nullptr) {
        surface_->Layout(GetBounds(), CachedLayoutTokens());
    }
}

bool MonitorSurface::HandleEvent(const W::Event& event) {
    // The Surface child covers the whole bounds, so its event
    // callback sees every hit; presses landing on us directly still
    // claim the viewer scope (belt & suspenders — a press can arrive
    // before the child exists in edge layouts).
    if (event.type == W::Event::Type::MouseDown) {
        Activate();
        return false;   // let the child's callback handle geometry
    }
    // KeyDown routes to the focused widget (us, not the Surface
    // child) — Esc cancels an in-flight gizmo gesture.
    if (event.type == W::Event::Type::KeyDown &&
        event.key == W::KeyCode::Escape &&
        (gizmo_mode_ != GizmoMode::None || gizmo_pending_select_)) {
        GizmoCancel();
        gizmo_click_ = false;
        return true;
    }
    return false;
}

void MonitorSurface::Tick(double dt_seconds) {
    click_clock_ += dt_seconds;
}

}  // namespace XER::VideoEditor::Ui2
