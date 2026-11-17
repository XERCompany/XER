#include "svg_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// librsvg (cairo backend) is the primary rasterizer — it handles the
// full SVG surface (groups, transforms, strokes, gradients) that the
// built-in mini parser below cannot. Without it the hand-rolled
// path/rect/circle rasterizer stays as the no-dependency fallback.
#if defined(__has_include)
#if __has_include(<librsvg/rsvg.h>) && __has_include(<cairo/cairo.h>)
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#define XER_VE_HAVE_RSVG 1
#endif
#endif

namespace XER::VideoEditor::AppRuntime {
namespace {

struct Pt {
    double x = 0;
    double y = 0;
};

struct Poly {
    std::vector<Pt> pts;
    bool closed = false;
};

// ---- float tokenizer --------------------------------------------------

const char* SkipSep(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' ||
                       *p == '\r'))
        ++p;
    return p;
}

const char* ReadNum(const char* p, const char* end, double* out) {
    p = SkipSep(p, end);
    if (p >= end) return nullptr;
    char* stop = nullptr;
    const double v = std::strtod(p, &stop);
    if (stop == p) return nullptr;
    *out = v;
    return stop;
}

// ---- path data → polylines --------------------------------------------

void FlattenCubic(Pt p0, Pt p1, Pt p2, Pt p3, std::vector<Pt>* out) {
    for (int i = 1; i <= 16; ++i) {
        const double t = i / 16.0, u = 1.0 - t;
        out->push_back(
            {u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x +
                 t * t * t * p3.x,
             u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y +
                 t * t * t * p3.y});
    }
}

void FlattenQuad(Pt p0, Pt p1, Pt p2, std::vector<Pt>* out) {
    for (int i = 1; i <= 12; ++i) {
        const double t = i / 12.0, u = 1.0 - t;
        out->push_back({u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
                        u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
}

// Endpoint→center arc conversion, then ~16 segments per curve span.
void FlattenArc(Pt p0, double rx, double ry, double rot_deg, bool large,
                bool sweep, Pt p1, std::vector<Pt>* out) {
    if (rx == 0 || ry == 0 || (p0.x == p1.x && p0.y == p1.y)) {
        out->push_back(p1);
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const double phi = rot_deg * 3.14159265358979323846 / 180.0;
    const double cp = std::cos(phi), sp = std::sin(phi);
    const double dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
    const double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
    double lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lam > 1) {
        const double s = std::sqrt(lam);
        rx *= s;
        ry *= s;
    }
    double num =
        rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coef = den == 0 ? 0 : std::sqrt(std::max(0.0, num / den));
    if (large == sweep) coef = -coef;
    const double cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
    const double cx = cp * cx1 - sp * cy1 + (p0.x + p1.x) / 2;
    const double cy = sp * cx1 + cp * cy1 + (p0.y + p1.y) / 2;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double d = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        double c = d == 0 ? 1 : (ux * vx + uy * vy) / d;
        c = std::clamp(c, -1.0, 1.0);
        return (ux * vy - uy * vx < 0 ? -1.0 : 1.0) * std::acos(c);
    };
    const double th1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    double dth = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx,
                       (-y1 - cy1) / ry);
    if (!sweep && dth > 0) dth -= 2 * 3.14159265358979323846;
    if (sweep && dth < 0) dth += 2 * 3.14159265358979323846;
    const int segs = std::max(2, static_cast<int>(std::fabs(dth) / 0.35));
    for (int i = 1; i <= segs; ++i) {
        const double t = th1 + dth * i / segs;
        out->push_back(
            {cx + rx * std::cos(t) * cp - ry * std::sin(t) * sp,
             cy + rx * std::cos(t) * sp + ry * std::sin(t) * cp});
    }
}

// Parses an SVG path `d` string. Returns false when no geometry.
bool ParsePathData(const std::string& d, std::vector<Poly>* polys) {
    const char* p = d.data();
    const char* const end = p + d.size();
    Pt cur{}, start{}, prev_ctrl{};
    char prev_cmd = 0;
    Poly* open = nullptr;
    const auto emit_pt = [&](Pt pt) {
        if (open == nullptr) {
            polys->push_back({});
            open = &polys->back();
        }
        open->pts.push_back(pt);
    };
    while (true) {
        p = SkipSep(p, end);
        if (p >= end) break;
        char cmd = *p;
        if ((cmd >= 'A' && cmd <= 'Z') || (cmd >= 'a' && cmd <= 'z')) {
            ++p;
        } else {
            // Implicit repeat: after M/m means lineto, else same as prev.
            if (prev_cmd == 'M') cmd = 'L';
            else if (prev_cmd == 'm') cmd = 'l';
            else cmd = prev_cmd;
        }
        if (cmd == 0) break;
        const bool rel = cmd >= 'a';
        const char uc = rel ? cmd - 32 : cmd;
        const auto next = [&](double* v) {
            // ReadNum failure must NOT clobber p — the cursor stays at
            // the command letter so the outer loop parses it next.
            const char* np = ReadNum(p, end, v);
            if (np == nullptr) return false;
            p = np;
            return true;
        };
        const auto abs_pt = [&](double x, double y) {
            return rel ? Pt{cur.x + x, cur.y + y} : Pt{x, y};
        };
        double a, b, c, dd, e, f, g;
        switch (uc) {
            case 'M': {
                if (!next(&a) || !next(&b)) return !polys->empty();
                cur = abs_pt(a, b);
                start = cur;
                open = nullptr;
                emit_pt(cur);
                break;
            }
            case 'L':
                while (next(&a) && next(&b)) {
                    cur = abs_pt(a, b);
                    emit_pt(cur);
                }
                break;
            case 'H':
                while (next(&a)) {
                    cur.x = rel ? cur.x + a : a;
                    emit_pt(cur);
                }
                break;
            case 'V':
                while (next(&a)) {
                    cur.y = rel ? cur.y + a : a;
                    emit_pt(cur);
                }
                break;
            case 'C':
                while (next(&a) && next(&b) && next(&c) && next(&dd) &&
                       next(&e) && next(&f)) {
                    const Pt c1 = abs_pt(a, b), c2 = abs_pt(c, dd),
                             to = abs_pt(e, f);
                    if (open != nullptr)
                        FlattenCubic(cur, c1, c2, to, &open->pts);
                    prev_ctrl = c2;
                    cur = to;
                }
                break;
            case 'S':
                while (next(&a) && next(&b) && next(&c) && next(&dd)) {
                    const Pt c1 = (prev_cmd == 'C' || prev_cmd == 'c' ||
                                   prev_cmd == 'S' || prev_cmd == 's')
                                      ? Pt{2 * cur.x - prev_ctrl.x,
                                           2 * cur.y - prev_ctrl.y}
                                      : cur;
                    const Pt c2 = abs_pt(a, b), to = abs_pt(c, dd);
                    if (open != nullptr)
                        FlattenCubic(cur, c1, c2, to, &open->pts);
                    prev_ctrl = c2;
                    cur = to;
                }
                break;
            case 'Q':
                while (next(&a) && next(&b) && next(&c) && next(&dd)) {
                    const Pt c1 = abs_pt(a, b), to = abs_pt(c, dd);
                    if (open != nullptr) FlattenQuad(cur, c1, to, &open->pts);
                    prev_ctrl = c1;
                    cur = to;
                }
                break;
            case 'T':
                while (next(&a) && next(&b)) {
                    const Pt c1 = (prev_cmd == 'Q' || prev_cmd == 'q' ||
                                   prev_cmd == 'T' || prev_cmd == 't')
                                      ? Pt{2 * cur.x - prev_ctrl.x,
                                           2 * cur.y - prev_ctrl.y}
                                      : cur;
                    const Pt to = abs_pt(a, b);
                    if (open != nullptr) FlattenQuad(cur, c1, to, &open->pts);
                    prev_ctrl = c1;
                    cur = to;
                }
                break;
            case 'A':
                while (next(&a) && next(&b) && next(&c) && next(&dd) &&
                       next(&e) && next(&f) && next(&g)) {
                    const Pt to = abs_pt(f, g);
                    if (open != nullptr)
                        FlattenArc(cur, a, b, c, dd != 0, e != 0, to,
                                   &open->pts);
                    cur = to;
                }
                break;
            case 'Z':
                if (open != nullptr) {
                    open->closed = true;
                    open = nullptr;
                }
                cur = start;
                break;
            default:
                // Unknown command — bail, keep what we have.
                return !polys->empty();
        }
        prev_cmd = cmd;
    }
    return !polys->empty();
}

// ---- attribute scraping ------------------------------------------------

std::string Attr(const std::string& tag, const std::string& name) {
    for (const char q : {'"', '\''}) {
        const std::string key = name + "=" + q;
        size_t pos = tag.find(key);
        if (pos == std::string::npos) continue;
        pos += key.size();
        const size_t close = tag.find(q, pos);
        if (close == std::string::npos) return {};
        return tag.substr(pos, close - pos);
    }
    return {};
}

// ---- coverage fill -----------------------------------------------------

// Fills `cov` (w*h coverage 0..1) from polys in viewBox space mapped via
// scale/offset. Nonzero winding, supersampled: cov is rendered directly
// at the destination resolution with a 4x sub-grid per pixel evaluated
// analytically by sampling sub-scanlines.
void FillPolys(const std::vector<Poly>& polys, double scale, double ox,
               double oy, int w, int h, std::vector<float>* cov) {
    constexpr int kSub = 4;
    cov->assign(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int sy = 0; sy < kSub; ++sy) {
            const double fy = (y + (sy + 0.5) / kSub - oy) / scale;
            std::vector<std::pair<double, int>> xs;  // x, winding delta
            for (const Poly& poly : polys) {
                const size_t n = poly.pts.size();
                if (n < 2) continue;
                for (size_t i = 0; i < n; ++i) {
                    const Pt& a = poly.pts[i];
                    const Pt& b = poly.pts[(i + 1) % n];
                    if (i + 1 == n && !poly.closed) break;
                    if ((a.y <= fy) == (b.y <= fy)) continue;
                    const double t = (fy - a.y) / (b.y - a.y);
                    const double x = a.x + t * (b.x - a.x);
                    xs.emplace_back(x * scale + ox, b.y > a.y ? 1 : -1);
                }
            }
            if (xs.empty()) continue;
            std::sort(xs.begin(), xs.end(),
                      [](const auto& l, const auto& r) {
                          return l.first < r.first;
                      });
            int wind = 0;
            double x_from = xs.front().first;
            for (const auto& [x, delta] : xs) {
                if (wind != 0) {
                    const int x0 = std::clamp(
                        static_cast<int>(std::ceil(x_from)), 0, w);
                    const int x1 =
                        std::clamp(static_cast<int>(std::ceil(x)), 0, w);
                    for (int px = x0; px < x1; ++px)
                        (*cov)[static_cast<size_t>(y) * w + px] +=
                            1.0f / kSub;
                }
                wind += delta;
                x_from = x;
            }
        }
    }
}

}  // namespace

#if defined(XER_VE_HAVE_RSVG)

// librsvg render: document into an ARGB32 cairo surface, un-premultiply
// and tint into BGRA8. Cairo ARGB32 on little-endian IS premultiplied
// BGRA bytes, so only the alpha channel needs straightening before the
// caller tint is applied (fill comes from `argb`, not the svg paint).
bool RasterizeWithRsvg(const std::string& svg_utf8, int out_size,
                       std::uint32_t argb, std::vector<std::uint8_t>* bgra) {
    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_data(
        reinterpret_cast<const guint8*>(svg_utf8.data()),
        static_cast<gsize>(svg_utf8.size()), &err);
    if (handle == nullptr) {
        if (err != nullptr) g_error_free(err);
        return false;
    }
    cairo_surface_t* surf = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, out_size, out_size);
    cairo_t* cr = cairo_create(surf);
    // Transparent background, then the whole document into the viewport.
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    RsvgRectangle viewport = {0.0, 0.0, static_cast<double>(out_size),
                              static_cast<double>(out_size)};
    const gboolean ok =
        rsvg_handle_render_document(handle, cr, &viewport, &err);
    if (err != nullptr) g_error_free(err);
    cairo_destroy(cr);
    g_object_unref(handle);
    if (!ok) {
        cairo_surface_destroy(surf);
        return false;
    }
    cairo_surface_flush(surf);
    const unsigned char* src = cairo_image_surface_get_data(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    bgra->assign(static_cast<size_t>(out_size) * out_size * 4, 0);
    for (int y = 0; y < out_size; ++y) {
        const unsigned char* row = src + static_cast<size_t>(y) * stride;
        for (int x = 0; x < out_size; ++x) {
            const unsigned a = row[x * 4 + 3];
            auto* px = bgra->data() +
                       (static_cast<size_t>(y) * out_size + x) * 4;
            if (a == 0) continue;
            // Glyph coverage = the rasterized alpha. D2D stages
            // DrawFrameBgra bitmaps as PREMULTIPLIED alpha, so tint
            // channels must be scaled by coverage here.
            const unsigned out_a =
                a * ((argb >> 24) & 0xFF) / 255;
            px[0] = static_cast<std::uint8_t>((argb & 0xFF) * a / 255);
            px[1] = static_cast<std::uint8_t>(
                (((argb >> 8) & 0xFF) * a) / 255);
            px[2] = static_cast<std::uint8_t>(
                (((argb >> 16) & 0xFF) * a) / 255);
            px[3] = static_cast<std::uint8_t>(out_a);
        }
    }
    cairo_surface_destroy(surf);
    return true;
}

#endif  // XER_VE_HAVE_RSVG

bool RasterizeSvgIcon(const std::string& svg_utf8, int out_size,
                      std::uint32_t argb, std::vector<std::uint8_t>* bgra) {
    if (bgra == nullptr || out_size <= 0 || out_size > 512) return false;

#if defined(XER_VE_HAVE_RSVG)
    if (RasterizeWithRsvg(svg_utf8, out_size, argb, bgra)) return true;
#endif

    // ---- fallback: mini parser (path/rect/circle, nonzero winding) ----

    // viewBox / width+height
    double vx = 0, vy = 0, vw = 24, vh = 24;
    if (const size_t p = svg_utf8.find("viewBox=");
        p != std::string::npos) {
        const std::string vb = Attr(svg_utf8.substr(p), "viewBox");
        if (!vb.empty()) {
            const char* c = vb.c_str();
            const char* e = c + vb.size();
            double a, b, cw, ch;
            if (ReadNum(c, e, &a) && ReadNum(c, e, &b) &&
                ReadNum(c, e, &cw) && ReadNum(c, e, &ch) && cw > 0 &&
                ch > 0) {
                vx = a;
                vy = b;
                vw = cw;
                vh = ch;
            }
        }
    }

    std::vector<Poly> polys;
    // <path d="...">
    for (size_t pos = 0;
         (pos = svg_utf8.find("<path", pos)) != std::string::npos;) {
        const size_t gt = svg_utf8.find('>', pos);
        const std::string tag =
            svg_utf8.substr(pos, gt == std::string::npos
                                       ? std::string::npos
                                       : gt - pos);
        const std::string d = Attr(tag, "d");
        if (!d.empty()) ParsePathData(d, &polys);
        pos = gt == std::string::npos ? svg_utf8.size() : gt + 1;
    }
    // <rect x y width height>
    for (size_t pos = 0;
         (pos = svg_utf8.find("<rect", pos)) != std::string::npos;) {
        const size_t gt = svg_utf8.find('>', pos);
        const std::string tag = svg_utf8.substr(
            pos, gt == std::string::npos ? std::string::npos : gt - pos);
        const auto num = [&](const char* k, double dv) {
            const std::string s = Attr(tag, k);
            return s.empty() ? dv : std::strtod(s.c_str(), nullptr);
        };
        const double x = num("x", 0), y = num("y", 0),
                     w = num("width", 0), h = num("height", 0);
        if (w > 0 && h > 0) {
            polys.push_back({{{x, y}, {x + w, y}, {x + w, y + h},
                              {x, y + h}},
                             true});
        }
        pos = gt == std::string::npos ? svg_utf8.size() : gt + 1;
    }
    // <circle>/<ellipse>
    for (const char* name : {"<circle", "<ellipse"}) {
        for (size_t pos = 0;
             (pos = svg_utf8.find(name, pos)) != std::string::npos;) {
            const size_t gt = svg_utf8.find('>', pos);
            const std::string tag =
                svg_utf8.substr(pos, gt == std::string::npos
                                           ? std::string::npos
                                           : gt - pos);
            const auto num = [&](const char* k, double dv) {
                const std::string s = Attr(tag, k);
                return s.empty() ? dv : std::strtod(s.c_str(), nullptr);
            };
            const double cx = num("cx", 0), cy = num("cy", 0),
                         rx = num("rx", num("r", 0)),
                         ry = num("ry", num("r", 0));
            if (rx > 0 && ry > 0) {
                Poly poly;
                poly.closed = true;
                for (int i = 0; i < 24; ++i) {
                    const double t = i * 2 * 3.14159265358979323846 / 24;
                    poly.pts.push_back(
                        {cx + rx * std::cos(t), cy + ry * std::sin(t)});
                }
                polys.push_back(std::move(poly));
            }
            pos = gt == std::string::npos ? svg_utf8.size() : gt + 1;
        }
    }
    if (polys.empty()) return false;

    // Fit viewBox into out_size preserving aspect, centered.
    const double scale = std::min(static_cast<double>(out_size) / vw,
                                  static_cast<double>(out_size) / vh);
    const double ox = (out_size - vw * scale) / 2 - vx * scale;
    const double oy = (out_size - vh * scale) / 2 - vy * scale;

    std::vector<float> cov;
    FillPolys(polys, scale, ox, oy, out_size, out_size, &cov);

    const std::uint8_t cr = (argb >> 16) & 0xFF;
    const std::uint8_t cg = (argb >> 8) & 0xFF;
    const std::uint8_t cb = argb & 0xFF;
    const std::uint8_t ca = (argb >> 24) & 0xFF;

    bgra->assign(static_cast<size_t>(out_size) * out_size * 4, 0);
    for (int i = 0; i < out_size * out_size; ++i) {
        const float c = std::clamp(cov[i], 0.0f, 1.0f);
        const auto a =
            static_cast<std::uint8_t>(std::lround(c * ca));
        auto* px = bgra->data() + static_cast<size_t>(i) * 4;
        // PREMULTIPLIED output — DrawFrameBgra's D2D bitmap expects it.
        px[0] = static_cast<std::uint8_t>(cb * a / 255);
        px[1] = static_cast<std::uint8_t>(cg * a / 255);
        px[2] = static_cast<std::uint8_t>(cr * a / 255);
        px[3] = a;
    }
    return true;
}

}  // namespace XER::VideoEditor::AppRuntime
