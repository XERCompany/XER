// font_catalog.cc — see font_catalog.h for the contract.
// Enumeration is lazy + cached: the first Families() call walks the
// system font manager once and the result is reused for the session.
#include "font_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(XER_VE_HAS_SKIA)
#include <core/SkCanvas.h>
#include <core/SkColor.h>
#include <core/SkFont.h>
#include <core/SkFontMetrics.h>
#include <core/SkFontMgr.h>
#include <core/SkFontStyle.h>
#include <core/SkImageInfo.h>
#include <core/SkPaint.h>
#include <core/SkString.h>
#include <core/SkSurface.h>
#include <core/SkTypeface.h>
#if defined(_WIN32) && __has_include(<ports/SkTypeface_win.h>)
#include <ports/SkTypeface_win.h>
#define XER_FONT_CATALOG_GDI_MGR 1
#endif
#endif

namespace XER::VideoEditor {
namespace {

#if defined(XER_VE_HAS_SKIA)

sk_sp<SkFontMgr> CatalogFontMgr() {
    static const sk_sp<SkFontMgr> mgr = [] {
#if defined(XER_FONT_CATALOG_GDI_MGR)
        if (sk_sp<SkFontMgr> m = SkFontMgr_New_GDI()) {
            return m;
        }
#endif
        return SkFontMgr::RefEmpty();
    }();
    return mgr;
}

std::vector<FontFamilyInfo> EnumerateFamilies() {
    std::vector<FontFamilyInfo> out;
    sk_sp<SkFontMgr> mgr = CatalogFontMgr();
    if (!mgr) {
        return out;
    }
    const int count = mgr->countFamilies();
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        FontFamilyInfo family;
        SkString name;
        mgr->getFamilyName(i, &name);
        family.name.assign(name.c_str(), name.size());
        sk_sp<SkFontStyleSet> set = mgr->createStyleSet(i);
        if (set) {
            for (int s = 0; s < set->count(); ++s) {
                SkFontStyle fs;
                SkString style_name;
                set->getStyle(s, &fs, &style_name);
                FontStyleInfo info;
                info.weight = fs.weight();
                info.italic =
                    fs.slant() != SkFontStyle::kUpright_Slant;
                info.name.assign(style_name.c_str(),
                                 style_name.size());
                family.styles.push_back(std::move(info));
            }
        }
        if (family.styles.empty()) {
            // A family with no enumerable styles still resolves via
            // matchFamilyStyle — offer the canonical four.
            family.styles = {
                {400, false, "Regular"},
                {700, false, "Bold"},
                {400, true, "Italic"},
                {700, true, "Bold Italic"},
            };
        }
        out.push_back(std::move(family));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return out;
}

const std::vector<FontFamilyInfo>& CachedFamilies() {
    static const std::vector<FontFamilyInfo> families =
        EnumerateFamilies();
    return families;
}

const std::vector<FontStyleInfo>& EmptyStyles() {
    static const std::vector<FontStyleInfo> none;
    return none;
}

#else

const std::vector<FontFamilyInfo>& CachedFamilies() {
    static const std::vector<FontFamilyInfo> none;
    return none;
}

const std::vector<FontStyleInfo>& EmptyStyles() {
    static const std::vector<FontStyleInfo> none;
    return none;
}

#endif

}  // namespace

bool FontCatalog::Available() {
#if defined(XER_VE_HAS_SKIA)
    return true;
#else
    return false;
#endif
}

const std::vector<FontFamilyInfo>& FontCatalog::Families() {
    return CachedFamilies();
}

std::vector<std::string> FontCatalog::FamilyNames() {
    std::vector<std::string> names;
    names.reserve(Families().size());
    for (const FontFamilyInfo& f : Families()) {
        names.push_back(f.name);
    }
    return names;
}

const std::vector<FontStyleInfo>& FontCatalog::StylesOf(
    const std::string& family) {
    for (const FontFamilyInfo& f : Families()) {
        if (f.name == family) {
            return f.styles;
        }
    }
    return EmptyStyles();
}

bool FontCatalog::RenderPreview(const std::string& family, int weight,
                                bool italic, const std::string& utf8,
                                float size_px,
                                std::vector<std::uint8_t>* bgra,
                                int* width, int* height) {
#if defined(XER_VE_HAS_SKIA)
    if (bgra == nullptr || width == nullptr || height == nullptr ||
        utf8.empty() || size_px <= 0.0f) {
        return false;
    }
    sk_sp<SkFontMgr> mgr = CatalogFontMgr();
    if (!mgr) {
        return false;
    }
    const SkFontStyle fs(
        std::clamp(weight, 1, 1000), SkFontStyle::kNormal_Width,
        italic ? SkFontStyle::kItalic_Slant
               : SkFontStyle::kUpright_Slant);
    sk_sp<SkTypeface> face;
    if (!family.empty()) {
        face = mgr->matchFamilyStyle(family.c_str(), fs);
    }
    if (!face) {
        face = mgr->legacyMakeTypeface(family.empty() ? nullptr
                                                    : family.c_str(),
                                       fs);
    }
    if (!face) {
        face = mgr->legacyMakeTypeface(nullptr, fs);
    }
    if (!face) {
        return false;
    }
    SkFont font(face, size_px);
    font.setEdging(SkFont::Edging::kAntiAlias);
    font.setSubpixel(true);
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    const SkScalar text_w =
        font.measureText(utf8.data(), utf8.size(),
                         SkTextEncoding::kUTF8);
    const int pad = std::max(2, static_cast<int>(size_px * 0.2f));
    const int w = std::max(
        1, static_cast<int>(std::ceil(text_w)) + 2 * pad);
    const int h =
        std::max(1, static_cast<int>(
                        std::ceil(-metrics.fAscent + metrics.fDescent)) +
                        2 * pad);
    sk_sp<SkSurface> surface =
        SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h));
    if (!surface) {
        return false;
    }
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorWHITE);
    canvas->drawSimpleText(utf8.data(), utf8.size(),
                           SkTextEncoding::kUTF8,
                           static_cast<SkScalar>(pad),
                           static_cast<SkScalar>(pad - metrics.fAscent),
                           font, paint);
    bgra->resize(static_cast<size_t>(w) * h * 4);
    const SkImageInfo dst = SkImageInfo::Make(
        w, h, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
    if (!surface->readPixels(dst, bgra->data(),
                             static_cast<size_t>(w) * 4, 0, 0)) {
        bgra->clear();
        return false;
    }
    *width = w;
    *height = h;
    return true;
#else
    (void)family;
    (void)weight;
    (void)italic;
    (void)utf8;
    (void)size_px;
    (void)bgra;
    (void)width;
    (void)height;
    return false;
#endif
}

}  // namespace XER::VideoEditor
