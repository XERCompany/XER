/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/pdf/pdf_internal.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace XER::Content::Pdf {
namespace {

// Renders `page_index` into a heap pixmap in `cs` colorspace at
// `scale` (pixels per point; dpi/72). Returns null on MuPDF error.
fz_pixmap* RenderPixmap(fz_context* ctx, fz_document* doc, int page_index,
                        float scale, fz_colorspace* cs, int alpha) {
    const fz_matrix ctm = fz_scale(scale, scale);
    return fz_new_pixmap_from_page_number(ctx, doc, page_index, ctm, cs,
                                          alpha);
}

// Copies `pm` samples into `dst` as RGBA8. `pm` may be RGB or RGBA;
// anything else is converted through fz_device_rgb first. The call
// must run inside a Guard scope (may throw on conversion).
void CopyToRgba(fz_context* ctx, fz_pixmap* pm, std::vector<uint8_t>* dst,
                int* stride) {
    fz_pixmap* owned = nullptr;
    if (!pm->colorspace ||
        fz_colorspace_n(ctx, pm->colorspace) != 3 || pm->n > 4) {
        owned = fz_convert_pixmap(ctx, pm, fz_device_rgb(ctx), nullptr,
                                  nullptr, fz_default_color_params,
                                  pm->alpha);
        pm = owned;
    }
    const int w = pm->w, h = pm->h, n = pm->n;
    const ptrdiff_t s = pm->stride;
    dst->resize((size_t)w * (size_t)h * 4);
    *stride = w * 4;
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = pm->samples + (ptrdiff_t)y * s;
        uint8_t* d = dst->data() + (size_t)y * (size_t)w * 4;
        if (n == 4) {
            memcpy(d, src, (size_t)w * 4);
        } else if (n == 3) {
            for (int x = 0; x < w; ++x) {
                d[x * 4 + 0] = src[x * 3 + 0];
                d[x * 4 + 1] = src[x * 3 + 1];
                d[x * 4 + 2] = src[x * 3 + 2];
                d[x * 4 + 3] = 0xff;
            }
        } else {
            memset(d, 0, (size_t)w * 4);
        }
    }
    if (owned) fz_drop_pixmap(ctx, owned);
}

}  // namespace

PdfBitmap PdfDocument::RenderPageToBitmap(int page_index, float dpi,
                                          bool transparent,
                                          std::string* error) {
    std::lock_guard lock(mutex_);
    PdfBitmap out;
    if (!impl_ || !impl_->doc) {
        Internal::SetError(error, "pdf: no document");
        return out;
    }
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (page_index < 0 || page_index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return out;
    }
    if (dpi <= 1.0f || dpi > 1200.0f) {
        Internal::SetError(error, "pdf: dpi out of range");
        return out;
    }

    fz_context* ctx = impl_->ctx;
    fz_document* doc = impl_->doc;
    const float scale = dpi / 72.0f;
    bool ok = Internal::Guard(ctx, [&] {
        fz_pixmap* pm = nullptr;
        fz_var(ctx); fz_var(pm);
        fz_try(ctx) {
            pm = RenderPixmap(ctx, doc, page_index, scale,
                              fz_device_rgb(ctx), transparent ? 1 : 0);
            if (!pm || pm->w <= 0 || pm->h <= 0)
                fz_throw(ctx, FZ_ERROR_GENERIC, "render produced no pixels");
            out.width = pm->w;
            out.height = pm->h;
            CopyToRgba(ctx, pm, &out.pixels, &out.stride);
        }
        fz_always(ctx) { if (pm) fz_drop_pixmap(ctx, pm); }
        fz_catch(ctx) { fz_rethrow(ctx); }
    }, error);
    if (!ok) out = PdfBitmap{};
    return out;
}

#ifdef _WIN32
namespace {

// Blits `src` (BGRA8, premultiplied or opaque) into `hdc` inside
// `dst`, letterboxed to preserve aspect ratio. Returns false on
// invalid args; Win32 failure details go to `error`.
bool BlitBgra(HDC hdc, const uint8_t* src, int src_w, int src_h,
              const RECT& dst, bool transparent, std::string* error) {
    const int dst_w = dst.right - dst.left;
    const int dst_h = dst.bottom - dst.top;
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        Internal::SetError(error, "pdf: empty blit rect");
        return false;
    }

    // Letterbox: fit src into dst preserving aspect ratio.
    const double fx = (double)dst_w / src_w;
    const double fy = (double)dst_h / src_h;
    const double f = fx < fy ? fx : fy;
    const int w = (int)(src_w * f + 0.5);
    const int h = (int)(src_h * f + 0.5);
    const int x = dst.left + (dst_w - w) / 2;
    const int y = dst.top + (dst_h - h) / 2;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = src_w;
    bmi.bmiHeader.biHeight = -src_h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (transparent) {
        // AlphaBlend needs a source HDC: stage the pixels into a
        // DIB section selected into a memory DC. MuPDF pixmaps are
        // premultiplied, matching AC_SRC_ALPHA.
        BITMAPINFO src_bmi = bmi;
        HDC memdc = CreateCompatibleDC(hdc);
        void* bits = nullptr;
        HBITMAP dib = memdc ? CreateDIBSection(hdc, &src_bmi,
                                             DIB_RGB_COLORS, &bits,
                                             nullptr, 0)
                            : nullptr;
        BOOL ok = FALSE;
        if (dib) {
            memcpy(bits, src, (size_t)src_w * src_h * 4);
            HGDIOBJ old = SelectObject(memdc, dib);
            BLENDFUNCTION bf{};
            bf.BlendOp = AC_SRC_OVER;
            bf.SourceConstantAlpha = 255;
            bf.AlphaFormat = AC_SRC_ALPHA;
            ok = AlphaBlend(hdc, x, y, w, h, memdc, 0, 0, src_w, src_h,
                            bf);
            SelectObject(memdc, old);
            DeleteObject(dib);
        }
        if (memdc) DeleteDC(memdc);
        if (dib && ok) return true;
        // DIB section failed: fall through to an opaque blit.
    }

    const int old_mode = SetStretchBltMode(hdc, HALFTONE);
    const int r = StretchDIBits(hdc, x, y, w, h, 0, 0, src_w, src_h,
                                src, &bmi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(hdc, old_mode);
    if (r == GDI_ERROR) {
        Internal::SetError(error, "pdf: StretchDIBits failed");
        return false;
    }
    return true;
}

}  // namespace

bool PdfDocument::RenderPageToHdc(void* hdc_v, int page_index,
                                  const PdfDstRect& dst, float dpi,
                                  bool transparent, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc || !hdc_v) {
        Internal::SetError(error, "pdf: no document or HDC");
        return false;
    }
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (page_index < 0 || page_index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return false;
    }
    if (dpi <= 1.0f || dpi > 1200.0f) {
        Internal::SetError(error, "pdf: dpi out of range");
        return false;
    }

    HDC hdc = (HDC)hdc_v;
    const RECT dst_rect{dst.left, dst.top, dst.right, dst.bottom};
    fz_context* ctx = impl_->ctx;
    fz_document* doc = impl_->doc;
    const float scale = dpi / 72.0f;

    return Internal::Guard(ctx, [&] {
        fz_pixmap* pm = nullptr;
        fz_var(ctx); fz_var(pm);
        fz_try(ctx) {
            pm = RenderPixmap(ctx, doc, page_index, scale,
                              fz_device_bgr(ctx), transparent ? 1 : 0);
            if (!pm || pm->w <= 0 || pm->h <= 0)
                fz_throw(ctx, FZ_ERROR_GENERIC, "render produced no pixels");
            // Pixmap may be BGR (n=3) or BGRA (n=4): copy to a tightly
            // packed BGRA buffer for GDI.
            std::vector<uint8_t> bgra((size_t)pm->w * pm->h * 4);
            for (int y = 0; y < pm->h; ++y) {
                const uint8_t* s = pm->samples + (ptrdiff_t)y * pm->stride;
                uint8_t* d = bgra.data() + (size_t)y * pm->w * 4;
                if (pm->n == 4) {
                    memcpy(d, s, (size_t)pm->w * 4);
                } else {
                    for (int x = 0; x < pm->w; ++x) {
                        d[x * 4 + 0] = s[x * 3 + 0];
                        d[x * 4 + 1] = s[x * 3 + 1];
                        d[x * 4 + 2] = s[x * 3 + 2];
                        d[x * 4 + 3] = 0xff;
                    }
                }
            }
            if (!BlitBgra(hdc, bgra.data(), pm->w, pm->h, dst_rect,
                          transparent, error)) {
                fz_throw(ctx, FZ_ERROR_GENERIC, "blit failed");
            }
        }
        fz_always(ctx) { if (pm) fz_drop_pixmap(ctx, pm); }
        fz_catch(ctx) { fz_rethrow(ctx); }
    }, error);
}

bool PdfDocument::RenderPageToHwnd(void* hwnd_v, int page_index,
                                   const PdfDstRect& dst, float dpi,
                                   bool transparent, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc || !hwnd_v) {
        Internal::SetError(error, "pdf: no document or HWND");
        return false;
    }
    HWND hwnd = (HWND)hwnd_v;
    RECT rect = {dst.left, dst.top, dst.right, dst.bottom};
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        if (!GetClientRect(hwnd, &rect)) {
            Internal::SetError(error, "pdf: GetClientRect failed");
            return false;
        }
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        Internal::SetError(error, "pdf: GetDC failed");
        return false;
    }
    const PdfDstRect d{rect.left, rect.top, rect.right, rect.bottom};
    const bool ok =
        RenderPageToHdc((void*)hdc, page_index, d, dpi, transparent, error);
    ReleaseDC(hwnd, hdc);
    return ok;
}

#else

bool PdfDocument::RenderPageToHdc(void*, int, const PdfDstRect&, float,
                                  bool, std::string* error) {
    Internal::SetError(error, "pdf: HDC rendering is Windows-only");
    return false;
}

bool PdfDocument::RenderPageToHwnd(void*, int, const PdfDstRect&, float,
                                   bool, std::string* error) {
    Internal::SetError(error, "pdf: HWND rendering is Windows-only");
    return false;
}

#endif  // _WIN32

}  // namespace XER::Content::Pdf
