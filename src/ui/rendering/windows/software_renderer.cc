#include "rendering/windows/software_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "rendering/windows/raster_util.h"

namespace XER::UI::Rendering::Windows {

namespace {

Layout::Rect NormalizeRect(Layout::Rect rect) {
    rect.width = std::max(0.0f, rect.width);
    rect.height = std::max(0.0f, rect.height);
    return rect;
}

// BGRA channel order on disk (same as DrawFrameBgra's input).
inline std::uint32_t PackBgra(std::uint8_t b, std::uint8_t g,
                              std::uint8_t r, std::uint8_t a) {
    return static_cast<std::uint32_t>(b) |
           (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
}

} // namespace

SoftwareRenderer::SoftwareRenderer(HWND hwnd)
    : hwnd_(hwnd),
      dpi_(hwnd != nullptr ? GetDpiForWindow(hwnd)
                           : USER_DEFAULT_SCREEN_DPI) {
    if (dpi_ == 0) {
        dpi_ = USER_DEFAULT_SCREEN_DPI;
    }
}

void SoftwareRenderer::SetDpi(unsigned dpi) {
    if (dpi == 0) {
        throw std::invalid_argument("DPI must be greater than zero");
    }
    dpi_ = dpi;
    buffer_valid_ = false;  // DIP<->px ratio changed; repaint everything
    targets_recreated_ = true;
}

void SoftwareRenderer::SetClearColor(Theme::Color color) {
    clear_color_ = color;
}

void SoftwareRenderer::SetFrameDamage(Layout::Rect damage) {
    // The retained buffer keeps previous pixels outside the damage — the
    // widget tree already culls draw calls to it, nothing else to do.
    (void)damage;
}

void SoftwareRenderer::RequireFrame() const {
    if (!frame_started_) {
        throw std::logic_error(
            "SoftwareRenderer drawing requires an active frame");
    }
}

void SoftwareRenderer::BeginFrame(Layout::Rect bounds) {
    if (frame_started_) {
        throw std::logic_error("A software frame is already active");
    }
    if (bounds.width < 0.0f || bounds.height < 0.0f) {
        throw std::invalid_argument(
            "Frame bounds cannot have negative dimensions");
    }
    targets_recreated_ = false;
    bounds_dip_ = bounds;

    const float dip_to_px = static_cast<float>(dpi_) / 96.0f;
    int w = 0, h = 0;
    if (hwnd_ != nullptr && IsWindow(hwnd_)) {
        RECT client{};
        if (GetClientRect(hwnd_, &client)) {
            w = std::max<LONG>(1, client.right);
            h = std::max<LONG>(1, client.bottom);
        }
    }
    if (w <= 0 || h <= 0) {
        // Headless (or an empty window): rasterize at bounds * dpi.
        w = std::max(1, static_cast<int>(bounds.width * dip_to_px + 0.5f));
        h = std::max(1, static_cast<int>(bounds.height * dip_to_px + 0.5f));
    }
    if (w != width_px_ || h != height_px_) {
        buffer_.assign(static_cast<std::size_t>(w) * h, 0);
        width_px_ = w;
        height_px_ = h;
        buffer_valid_ = false;
        targets_recreated_ = true;
    }
    if (!buffer_valid_) {
        const auto c = clear_color_;
        const std::uint32_t clear = PackBgra(
            static_cast<std::uint8_t>(
                std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<std::uint8_t>(
                std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<std::uint8_t>(
                std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f),
            static_cast<std::uint8_t>(
                std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f));
        std::fill(buffer_.begin(), buffer_.end(), clear);
    }
    clip_stack_px_.clear();
    layer_opacity_.clear();
    transform_ = Mat2x3{};
    frame_started_ = true;
}

void SoftwareRenderer::EndFrame() {
    RequireFrame();
    frame_started_ = false;
    buffer_valid_ = true;
    if (hwnd_ == nullptr || !IsWindow(hwnd_) || buffer_.empty()) {
        return;
    }
    HDC dc = GetDC(hwnd_);
    if (dc == nullptr) {
        return;
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_px_;
    bmi.bmiHeader.biHeight = -height_px_;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, width_px_, height_px_, 0, 0, 0,
                      height_px_, buffer_.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(hwnd_, dc);
}

bool SoftwareRenderer::TakeTargetsRecreated() {
    return targets_recreated_;
}

// Maps a DIP rect through the current 2x3 transform and the DIP->px
// scale into a pixel-space quad (corners TL,TR,BL,BR as x,y pairs).
Layout::Rect SoftwareRenderer::DipRectToPixelQuad(Layout::Rect rect,
                                                float* corners) const {
    const float s = static_cast<float>(dpi_) / 96.0f;
    const float xs[4] = {rect.x, rect.x + rect.width, rect.x,
                         rect.x + rect.width};
    const float ys[4] = {rect.y, rect.y, rect.y + rect.height,
                         rect.y + rect.height};
    Layout::Rect aabb{1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 4; ++i) {
        const float dx = transform_.a * xs[i] + transform_.c * ys[i] +
                         transform_.tx;
        const float dy = transform_.b * xs[i] + transform_.d * ys[i] +
                         transform_.ty;
        corners[i * 2] = dx * s;
        corners[i * 2 + 1] = dy * s;
        aabb.x = std::min(aabb.x, corners[i * 2]);
        aabb.y = std::min(aabb.y, corners[i * 2 + 1]);
        aabb.width = std::max(aabb.width, corners[i * 2]);
        aabb.height = std::max(aabb.height, corners[i * 2 + 1]);
    }
    aabb.width -= aabb.x;
    aabb.height -= aabb.y;
    return aabb;
}

void SoftwareRenderer::ClipTo(const float corners[8]) {
    Layout::Rect quad{1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 4; ++i) {
        quad.x = std::min(quad.x, corners[i * 2]);
        quad.y = std::min(quad.y, corners[i * 2 + 1]);
        quad.width = std::max(quad.width, corners[i * 2]);
        quad.height = std::max(quad.height, corners[i * 2 + 1]);
    }
    quad.width -= quad.x;
    quad.height -= quad.y;
    if (clip_stack_px_.empty()) {
        clip_stack_px_.push_back(quad);
        return;
    }
    const Layout::Rect& top = clip_stack_px_.back();
    const float x0 = std::max(quad.x, top.x);
    const float y0 = std::max(quad.y, top.y);
    const float x1 = std::min(quad.x + quad.width, top.x + top.width);
    const float y1 = std::min(quad.y + quad.height, top.y + top.height);
    clip_stack_px_.push_back(
        {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)});
}

void SoftwareRenderer::PushClip(Layout::Rect rect) {
    RequireFrame();
    rect = NormalizeRect(rect);
    float corners[8];
    DipRectToPixelQuad(rect, corners);
    ClipTo(corners);
}

void SoftwareRenderer::PopClip() {
    RequireFrame();
    if (!clip_stack_px_.empty()) {
        clip_stack_px_.pop_back();
    }
}

void SoftwareRenderer::BlendQuadPx(const float corners[8],
                                   Theme::Color color, float radius_dip) {
    if (buffer_.empty()) {
        return;
    }
    float opacity = 1.0f;
    for (const float o : layer_opacity_) {
        opacity *= o;
    }
    const float a_f = std::clamp(color.a * opacity, 0.0f, 1.0f);
    if (a_f <= 0.0f) {
        return;
    }
    const int sr = static_cast<int>(
        std::clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    const int sg = static_cast<int>(
        std::clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    const int sb = static_cast<int>(
        std::clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f);

    Layout::Rect clip{0.0f, 0.0f, static_cast<float>(width_px_),
                      static_cast<float>(height_px_)};
    if (!clip_stack_px_.empty()) {
        const Layout::Rect& top = clip_stack_px_.back();
        const float x1 = std::min(clip.width, top.x + top.width);
        const float y1 = std::min(clip.height, top.y + top.height);
        clip.x = std::max(clip.x, top.x);
        clip.y = std::max(clip.y, top.y);
        clip.width = std::max(0.0f, x1 - clip.x);
        clip.height = std::max(0.0f, y1 - clip.y);
    }

    const float x_min = std::min(
        {corners[0], corners[2], corners[4], corners[6]});
    const float x_max = std::max(
        {corners[0], corners[2], corners[4], corners[6]});
    const float y_min = std::min(
        {corners[1], corners[3], corners[5], corners[7]});
    const float y_max = std::max(
        {corners[1], corners[3], corners[5], corners[7]});
    const int y0 = std::max(0, static_cast<int>(std::floor(
                                   std::max(y_min, clip.y))));
    const int y1 = std::min(height_px_, static_cast<int>(std::ceil(
                                            std::min(y_max,
                                                     clip.y + clip.height))));
    if (y1 <= y0) {
        return;
    }

    // Axis-aligned fast path (also the only path that honors the rounded
    // corner radius — the SDF needs an unrotated box).
    const bool axis_aligned =
        corners[1] == corners[3] && corners[5] == corners[7] &&
        corners[0] == corners[4] && corners[2] == corners[6];
    const float s = static_cast<float>(dpi_) / 96.0f;
    const float radius_px = std::clamp(
        radius_dip * s, 0.0f,
        std::min(x_max - x_min, y_max - y_min) * 0.5f);

    const int x0_clip = std::max(
        0, static_cast<int>(std::floor(std::max(x_min, clip.x))));
    const int x1_clip = std::min(
        width_px_, static_cast<int>(std::ceil(
                       std::min(x_max, clip.x + clip.width))));

    auto blend = [&](int x, int y, float coverage) {
        const int a = static_cast<int>(a_f * 255.0f * coverage + 0.5f);
        if (a <= 0) {
            return;
        }
        std::uint32_t& dst =
            buffer_[static_cast<std::size_t>(y) * width_px_ + x];
        const int db = dst & 0xFF;
        const int dg = (dst >> 8) & 0xFF;
        const int dr = (dst >> 16) & 0xFF;
        const int da = (dst >> 24) & 0xFF;
        const int inv = 255 - a;
        const int ob = (sb * a + db * inv + 127) / 255;
        const int og = (sg * a + dg * inv + 127) / 255;
        const int or_ = (sr * a + dr * inv + 127) / 255;
        const int oa = a + (da * inv + 127) / 255;
        dst = PackBgra(static_cast<std::uint8_t>(ob),
                       static_cast<std::uint8_t>(og),
                       static_cast<std::uint8_t>(or_),
                       static_cast<std::uint8_t>(oa));
    };

    if (axis_aligned) {
        const float cx = (x_min + x_max) * 0.5f;
        const float cy = (y_min + y_max) * 0.5f;
        const float hx = (x_max - x_min) * 0.5f;
        const float hy = (y_max - y_min) * 0.5f;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0_clip; x < x1_clip; ++x) {
                float coverage = 1.0f;
                if (radius_px > 0.0f) {
                    // Rounded-rect SDF (pixel-center sampling).
                    const float qx = std::fabs(x + 0.5f - cx) - (hx - radius_px);
                    const float qy = std::fabs(y + 0.5f - cy) - (hy - radius_px);
                    const float ox = std::max(qx, 0.0f);
                    const float oy = std::max(qy, 0.0f);
                    const float d = std::sqrt(ox * ox + oy * oy) +
                                    std::min(std::max(qx, qy), 0.0f) -
                                    radius_px;
                    coverage = std::clamp(0.5f - d, 0.0f, 1.0f);
                }
                blend(x, y, coverage);
            }
        }
        return;
    }

    // General convex quad: scanline between the two outer edge crossings.
    for (int y = y0; y < y1; ++y) {
        const float yc = y + 0.5f;
        float left = 1e30f, right = -1e30f;
        for (int e = 0; e < 4; ++e) {
            const int f = (e + 1) % 4;
            const float ex0 = corners[e * 2], ey0 = corners[e * 2 + 1];
            const float ex1 = corners[f * 2], ey1 = corners[f * 2 + 1];
            if ((ey0 <= yc && yc < ey1) || (ey1 <= yc && yc < ey0)) {
                const float x =
                    ex0 + (yc - ey0) * (ex1 - ex0) / (ey1 - ey0);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        if (!(right > left)) {
            continue;
        }
        const int xs = std::max(
            x0_clip, static_cast<int>(std::floor(left)));
        const int xe = std::min(
            x1_clip, static_cast<int>(std::ceil(right)));
        for (int x = xs; x < xe; ++x) {
            blend(x, y, 1.0f);
        }
    }
}

void SoftwareRenderer::FillRect(Layout::Rect rect, Theme::Color color,
                                float radius) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    float corners[8];
    DipRectToPixelQuad(rect, corners);
    BlendQuadPx(corners, color, radius);
}

void SoftwareRenderer::StrokeRect(Layout::Rect rect, Theme::Color color,
                                  float width, float radius) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f || !(width > 0.0f)) {
        return;
    }
    // Edge quads; corner rounding on the stroke outline is approximated
    // with square joins (same net effect at small radii).
    (void)radius;
    FillRect({rect.x, rect.y, rect.width, width}, color, 0.0f);
    FillRect({rect.x, rect.y + rect.height - width, rect.width, width},
             color, 0.0f);
    const float inner_h = std::max(0.0f, rect.height - 2.0f * width);
    FillRect({rect.x, rect.y + width, width, inner_h}, color, 0.0f);
    FillRect({rect.x + rect.width - width, rect.y + width, width, inner_h},
             color, 0.0f);
}

void SoftwareRenderer::DrawLine(Layout::Point start, Layout::Point end,
                                Theme::Color color, float width) {
    RequireFrame();
    if (!(width > 0.0f)) {
        return;
    }
    const float s = static_cast<float>(dpi_) / 96.0f;
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    float nx = 0.0f, ny = 0.0f;
    if (len > 1e-6f) {
        nx = -dy / len;
        ny = dx / len;
    }
    const float hw = width * 0.5f;
    const float pts_dip[8] = {
        start.x + nx * hw, start.y + ny * hw,
        end.x + nx * hw,   end.y + ny * hw,
        start.x - nx * hw, start.y - ny * hw,
        end.x - nx * hw,   end.y - ny * hw};
    float corners[8];
    for (int i = 0; i < 4; ++i) {
        const float px = pts_dip[i * 2], py = pts_dip[i * 2 + 1];
        corners[i * 2] =
            (transform_.a * px + transform_.c * py + transform_.tx) * s;
        corners[i * 2 + 1] =
            (transform_.b * px + transform_.d * py + transform_.ty) * s;
    }
    BlendQuadPx(corners, color, 0.0f);
}

void SoftwareRenderer::BlendMask(const std::vector<std::uint8_t>& mask,
                                 int w, int h, Layout::Point origin_px,
                                 Theme::Color color) {
    float opacity = 1.0f;
    for (const float o : layer_opacity_) {
        opacity *= o;
    }
    const float base_a = std::clamp(color.a * opacity, 0.0f, 1.0f);
    if (base_a <= 0.0f) {
        return;
    }
    Layout::Rect clip{0.0f, 0.0f, static_cast<float>(width_px_),
                      static_cast<float>(height_px_)};
    if (!clip_stack_px_.empty()) {
        const Layout::Rect& top = clip_stack_px_.back();
        const float x1 = std::min(clip.width, top.x + top.width);
        const float y1 = std::min(clip.height, top.y + top.height);
        clip.x = std::max(clip.x, top.x);
        clip.y = std::max(clip.y, top.y);
        clip.width = std::max(0.0f, x1 - clip.x);
        clip.height = std::max(0.0f, y1 - clip.y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(
                                   std::max(origin_px.x, clip.x))));
    const int y0 = std::max(0, static_cast<int>(std::floor(
                                   std::max(origin_px.y, clip.y))));
    const int x1 = std::min(
        {width_px_, static_cast<int>(origin_px.x) + w,
         static_cast<int>(std::ceil(clip.x + clip.width))});
    const int y1 = std::min(
        {height_px_, static_cast<int>(origin_px.y) + h,
         static_cast<int>(std::ceil(clip.y + clip.height))});
    const int sr = static_cast<int>(
        std::clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    const int sg = static_cast<int>(
        std::clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    const int sb = static_cast<int>(
        std::clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int mx = x - static_cast<int>(origin_px.x);
            const int my = y - static_cast<int>(origin_px.y);
            const std::uint8_t bg = mask[static_cast<std::size_t>(my) * w * 4 +
                                         mx * 4];
            const float coverage =
                (255.0f - static_cast<float>(bg)) / 255.0f;
            const int a = static_cast<int>(base_a * coverage * 255.0f + 0.5f);
            if (a <= 0) {
                continue;
            }
            std::uint32_t& dst =
                buffer_[static_cast<std::size_t>(y) * width_px_ + x];
            const int db = dst & 0xFF;
            const int dg = (dst >> 8) & 0xFF;
            const int dr = (dst >> 16) & 0xFF;
            const int da = (dst >> 24) & 0xFF;
            const int inv = 255 - a;
            dst = PackBgra(
                static_cast<std::uint8_t>((sb * a + db * inv + 127) / 255),
                static_cast<std::uint8_t>((sg * a + dg * inv + 127) / 255),
                static_cast<std::uint8_t>((sr * a + dr * inv + 127) / 255),
                static_cast<std::uint8_t>(a + (da * inv + 127) / 255));
        }
    }
}

void SoftwareRenderer::DrawText(std::string_view text, Layout::Rect rect,
                                TextStyle style) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (text.empty() || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    float corners[8];
    DipRectToPixelQuad(rect, corners);
    const float s = static_cast<float>(dpi_) / 96.0f;
    const int w_px = std::max(1, static_cast<int>(rect.width * s + 0.5f));
    const int h_px = std::max(1, static_cast<int>(rect.height * s + 0.5f));
    if (w_px > 8192 || h_px > 8192 ||
        static_cast<std::size_t>(w_px) * h_px > (16u << 20)) {
        return;
    }
    TextStyle px_style = style;
    px_style.size = style.size * s;  // RasterizeMask wants pixels
    std::vector<std::uint8_t> mask;
    if (!GdiText::RasterizeMask(text, px_style, w_px, h_px, &mask)) {
        return;
    }
    // Text under arbitrary transforms lands on the transformed quad's
    // AABB (documented software-path approximation).
    const float x_min = std::min(
        {corners[0], corners[2], corners[4], corners[6]});
    const float y_min = std::min(
        {corners[1], corners[3], corners[5], corners[7]});
    BlendMask(mask, w_px, h_px, {x_min, y_min}, style.color);
}

Layout::Size SoftwareRenderer::MeasureText(std::string_view text,
                                           TextStyle style) const {
    return GdiText::Measure(text, style, dpi_);
}

void SoftwareRenderer::DrawBackdropBlur(Layout::Rect rect, float radius) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (!(radius > 0.0f) || rect.width <= 0.0f || rect.height <= 0.0f ||
        buffer_.empty()) {
        return;
    }
    float corners[8];
    const Layout::Rect aabb = DipRectToPixelQuad(rect, corners);
    Layout::Rect clip{0.0f, 0.0f, static_cast<float>(width_px_),
                      static_cast<float>(height_px_)};
    if (!clip_stack_px_.empty()) {
        clip = clip_stack_px_.back();
    }
    const int x0 = std::max(0, static_cast<int>(std::max(aabb.x, clip.x)));
    const int y0 = std::max(0, static_cast<int>(std::max(aabb.y, clip.y)));
    const int x1 = std::min(
        width_px_, static_cast<int>(std::ceil(std::min(
                       aabb.x + aabb.width, clip.x + clip.width))));
    const int y1 = std::min(
        height_px_, static_cast<int>(std::ceil(std::min(
                        aabb.y + aabb.height, clip.y + clip.height))));
    const int r = std::clamp(static_cast<int>(radius + 0.5f), 1, 8);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    // Separable box blur, two passes (H then V) on a row/column copy.
    std::vector<std::uint32_t> tmp(static_cast<std::size_t>(x1 - x0) *
                                   (y1 - y0));
    const int rw = x1 - x0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int b = 0, g = 0, rr = 0, a = 0, n = 0;
            for (int k = -r; k <= r; ++k) {
                const int xx = std::clamp(x + k, x0, x1 - 1);
                const std::uint32_t p =
                    buffer_[static_cast<std::size_t>(y) * width_px_ + xx];
                b += p & 0xFF;
                g += (p >> 8) & 0xFF;
                rr += (p >> 16) & 0xFF;
                a += (p >> 24) & 0xFF;
                ++n;
            }
            tmp[static_cast<std::size_t>(y - y0) * rw + (x - x0)] = PackBgra(
                static_cast<std::uint8_t>(b / n),
                static_cast<std::uint8_t>(g / n),
                static_cast<std::uint8_t>(rr / n),
                static_cast<std::uint8_t>(a / n));
        }
    }
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int b = 0, g = 0, rr = 0, a = 0, n = 0;
            for (int k = -r; k <= r; ++k) {
                const int yy = std::clamp(y + k, y0, y1 - 1) - y0;
                const std::uint32_t p =
                    tmp[static_cast<std::size_t>(yy) * rw + (x - x0)];
                b += p & 0xFF;
                g += (p >> 8) & 0xFF;
                rr += (p >> 16) & 0xFF;
                a += (p >> 24) & 0xFF;
                ++n;
            }
            buffer_[static_cast<std::size_t>(y) * width_px_ + x] = PackBgra(
                static_cast<std::uint8_t>(b / n),
                static_cast<std::uint8_t>(g / n),
                static_cast<std::uint8_t>(rr / n),
                static_cast<std::uint8_t>(a / n));
        }
    }
}

void SoftwareRenderer::BlendImage(const Image& img, Layout::Rect dst_dip,
                                  float opacity, float corner_radius) {
    (void)corner_radius;  // rounded image masks are a D2D path feature
    float corners[8];
    const Layout::Rect aabb = DipRectToPixelQuad(dst_dip, corners);
    float total_opacity = std::clamp(opacity, 0.0f, 1.0f);
    for (const float o : layer_opacity_) {
        total_opacity *= o;
    }
    if (total_opacity <= 0.0f) {
        return;
    }
    Layout::Rect clip{0.0f, 0.0f, static_cast<float>(width_px_),
                      static_cast<float>(height_px_)};
    if (!clip_stack_px_.empty()) {
        const Layout::Rect& top = clip_stack_px_.back();
        const float x1 = std::min(clip.width, top.x + top.width);
        const float y1 = std::min(clip.height, top.y + top.height);
        clip.x = std::max(clip.x, top.x);
        clip.y = std::max(clip.y, top.y);
        clip.width = std::max(0.0f, x1 - clip.x);
        clip.height = std::max(0.0f, y1 - clip.y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(
                                   std::max(aabb.x, clip.x))));
    const int y0 = std::max(0, static_cast<int>(std::floor(
                                   std::max(aabb.y, clip.y))));
    const int x1 = std::min(
        width_px_, static_cast<int>(std::ceil(std::min(
                       aabb.x + aabb.width, clip.x + clip.width))));
    const int y1 = std::min(
        height_px_, static_cast<int>(std::ceil(std::min(
                        aabb.y + aabb.height, clip.y + clip.height))));
    if (x1 <= x0 || y1 <= y0 || aabb.width <= 0.0f || aabb.height <= 0.0f) {
        return;
    }
    for (int y = y0; y < y1; ++y) {
        const int sy = std::clamp(
            static_cast<int>((y + 0.5f - aabb.y) * img.h / aabb.height), 0,
            img.h - 1);
        for (int x = x0; x < x1; ++x) {
            const int sx = std::clamp(
                static_cast<int>((x + 0.5f - aabb.x) * img.w / aabb.width),
                0, img.w - 1);
            const std::size_t idx =
                (static_cast<std::size_t>(sy) * img.w + sx) * 4;
            const int sa = static_cast<int>(
                img.bgra[idx + 3] * total_opacity + 0.5f);
            if (sa <= 0) {
                continue;
            }
            std::uint32_t& dst =
                buffer_[static_cast<std::size_t>(y) * width_px_ + x];
            const int db = dst & 0xFF;
            const int dg = (dst >> 8) & 0xFF;
            const int dr = (dst >> 16) & 0xFF;
            const int da = (dst >> 24) & 0xFF;
            const int inv = 255 - sa;
            dst = PackBgra(
                static_cast<std::uint8_t>(
                    (img.bgra[idx] * sa + db * inv + 127) / 255),
                static_cast<std::uint8_t>(
                    (img.bgra[idx + 1] * sa + dg * inv + 127) / 255),
                static_cast<std::uint8_t>(
                    (img.bgra[idx + 2] * sa + dr * inv + 127) / 255),
                static_cast<std::uint8_t>(sa + (da * inv + 127) / 255));
        }
    }
}

bool SoftwareRenderer::DrawImage(std::string_view path, Layout::Rect rect,
                                 float opacity, float corner_radius) {
    RequireFrame();
    rect = NormalizeRect(rect);
    if (path.empty() || rect.width <= 0.0f || rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }
    const std::string key(path);
    auto it = image_cache_.find(key);
    if (it == image_cache_.end()) {
        Image img;
        DecodeImageFileToBgra(key, &img.bgra, &img.w, &img.h);
        if (image_cache_.size() >= 64) {
            image_cache_.clear();
        }
        it = image_cache_.emplace(key, std::move(img)).first;
    }
    if (it->second.bgra.empty()) {
        return false;
    }
    BlendImage(it->second, rect, opacity, corner_radius);
    return true;
}

bool SoftwareRenderer::DrawFrameBgra(const void* bgra, int width,
                                     int height, Layout::Rect dst_rect,
                                     float opacity) {
    RequireFrame();
    dst_rect = NormalizeRect(dst_rect);
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        dst_rect.width <= 0.0f || dst_rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }
    Image img;
    img.w = width;
    img.h = height;
    img.bgra.assign(
        static_cast<const std::uint8_t*>(bgra),
        static_cast<const std::uint8_t*>(bgra) +
            static_cast<std::size_t>(width) * height * 4u);
    BlendImage(img, dst_rect, opacity, 0.0f);
    return true;
}

void SoftwareRenderer::PushLayer(float opacity,
                                 std::uint32_t blend_mode, float blur) {
    RequireFrame();
    (void)blend_mode;  // deferred — see header comment
    (void)blur;
    layer_opacity_.push_back(std::clamp(opacity, 0.0f, 1.0f));
}

void SoftwareRenderer::PopLayer() {
    RequireFrame();
    if (!layer_opacity_.empty()) {
        layer_opacity_.pop_back();
    }
}

void SoftwareRenderer::SetTransform(float a, float b, float c, float d,
                                    float tx, float ty) {
    RequireFrame();
    transform_ = Mat2x3{a, b, c, d, tx, ty};
}

void SoftwareRenderer::ResetTransform() {
    RequireFrame();
    transform_ = Mat2x3{};
}

bool SoftwareRenderer::DrawResourcePixels(const void* bgra,
                                          std::uint32_t id, int width,
                                          int height, Layout::Rect dst,
                                          float opacity,
                                          float corner_radius) {
    RequireFrame();
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    auto& img = resource_cache_[id];
    if (img.bgra.empty()) {
        if (resource_cache_.size() >= 256) {
            resource_cache_.clear();
        }
        img.w = width;
        img.h = height;
        img.bgra.assign(
            static_cast<const std::uint8_t*>(bgra),
            static_cast<const std::uint8_t*>(bgra) +
                static_cast<std::size_t>(width) * height * 4u);
    }
    BlendImage(img, dst, opacity, corner_radius);
    return true;
}

bool SoftwareRenderer::DrawVideoPixels(const void* bgra, std::uint32_t id,
                                       int width, int height,
                                       Layout::Rect dst, float opacity,
                                       float corner_radius) {
    RequireFrame();
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    auto& img = video_cache_[id];
    img.w = width;
    img.h = height;
    img.bgra.assign(
        static_cast<const std::uint8_t*>(bgra),
        static_cast<const std::uint8_t*>(bgra) +
            static_cast<std::size_t>(width) * height * 4u);
    BlendImage(img, dst, opacity, corner_radius);
    return true;
}

void SoftwareRenderer::FreeResource(std::uint32_t id) {
    resource_cache_.erase(id);
    video_cache_.erase(id);
}

} // namespace XER::UI::Rendering::Windows
