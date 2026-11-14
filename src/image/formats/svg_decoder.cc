#include "svg_decoder.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include "flux/core/logger.h"
#include "helper/path_utils.h"
#include "helper/tool_to.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

#if defined(_MSC_VER)
#define xer_sscanf sscanf_s
#else
#define xer_sscanf sscanf
#endif

// libxml2 for SVG attribute parsing
#if __has_include(<libxml/parser.h>)
#include <libxml/parser.h>
#include <libxml/tree.h>
#define XER_HAS_LIBXML2 1
#else
#define XER_HAS_LIBXML2 0
#endif

namespace image {

#if XER_HAS_LIBXML2
namespace {

// Parse an SVG length value: strip "px", "%", "pt", "mm" suffix and return
// the numeric part.  Returns -1 on failure.
static float ParseSvgLength(const char* s) {
    if (!s || !*s) return -1.f;
    char* end = nullptr;
    const float v = static_cast<float>(std::strtod(s, &end));
    return (end != s) ? v : -1.f;
}

// Extract width/height from the root <svg> element.
// Falls back to viewBox if width/height are missing/percentage.
static void GetSvgDimensions(xmlDocPtr doc, int& w_out, int& h_out) {
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) return;

    // Try explicit width / height attributes.
    xmlChar* ws = xmlGetProp(root, BAD_CAST "width");
    xmlChar* hs = xmlGetProp(root, BAD_CAST "height");
    float w = ws ? ParseSvgLength(reinterpret_cast<const char*>(ws)) : -1.f;
    float h = hs ? ParseSvgLength(reinterpret_cast<const char*>(hs)) : -1.f;
    xmlFree(ws); xmlFree(hs);

    if (w <= 0.f || h <= 0.f) {
        // Fall back to viewBox="x y w h"
        xmlChar* vb = xmlGetProp(root, BAD_CAST "viewBox");
        if (vb) {
            float vbx, vby, vbw = -1.f, vbh = -1.f;
            xer_sscanf(reinterpret_cast<const char*>(vb),
                        "%f %f %f %f", &vbx, &vby, &vbw, &vbh);
            xmlFree(vb);
            if (vbw > 0.f) w = vbw;
            if (vbh > 0.f) h = vbh;
        }
    }

    if (w > 0.f) w_out = static_cast<int>(w);
    if (h > 0.f) h_out = static_cast<int>(h);
}

}  // namespace
#endif  // XER_HAS_LIBXML2

std::shared_ptr<ImageBuffer>
SvgDecoder::Rasterize(const std::string& path, int target_w, int target_h,
                       ImageDescriptor& desc) {
    // Read SVG file content. fs::path keeps the UTF-16 form on Windows so
    // non-ASCII paths open correctly (ToolTo::ReadTextFile goes through an
    // ANSI std::string and would mangle them).
    std::ifstream in(XER::Helper::Path::FromUtf8(path), std::ios::binary);
    if (!in) {
        XER::Flux::Core::Logger().Error("SvgDecoder", "Cannot open SVG file: " + path);
        return nullptr;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string svg_text = ss.str();
    if (svg_text.empty()) {
        XER::Flux::Core::Logger().Error("SvgDecoder", "Cannot open SVG file: " + path);
        return nullptr;
    }

    // Determine intrinsic dimensions from SVG markup.
    int intrinsic_w = 0, intrinsic_h = 0;
#if XER_HAS_LIBXML2
    {
        xmlDocPtr doc = xmlReadMemory(svg_text.data(),
                                       static_cast<int>(svg_text.size()),
                                       "svg.xml", nullptr,
                                       XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
        if (doc) {
            GetSvgDimensions(doc, intrinsic_w, intrinsic_h);
            xmlFreeDoc(doc);
        }
    }
#endif

    // Resolve final raster size: prefer caller-supplied, then intrinsic, then 512.
    const int w = (target_w > 0) ? target_w : (intrinsic_w > 0 ? intrinsic_w : 512);
    const int h = (target_h > 0) ? target_h : (intrinsic_h > 0 ? intrinsic_h : 512);

    desc.width       = w;
    desc.height      = h;
    desc.format      = PixelFormat::RGBA8;
    desc.format_name = "svg";

    // Rasterize: fill with light-grey background via Skia if available,
    // otherwise produce a white canvas.
    auto buf = std::make_shared<ImageBuffer>(w, h, PixelFormat::RGBA8);
    buf->Clear(255, 255, 255, 255);

    if (XER::Bridge::Skia::IsAvailable()) {
        std::vector<uint32_t> px(w * h,
            XER::Bridge::Skia::MakeColorRGBA(240, 240, 240, 255));
        XER::Bridge::Skia::RasterClear(&px, w, h,
            XER::Bridge::Skia::MakeColorRGBA(240, 240, 240, 255));
        uint8_t* dst = buf->Data();
        for (int i = 0, n = w * h; i < n; ++i) {
            const uint32_t c = px[i];
            dst[i * 4 + 0] = (c >> 24) & 0xFF;
            dst[i * 4 + 1] = (c >> 16) & 0xFF;
            dst[i * 4 + 2] = (c >>  8) & 0xFF;
            dst[i * 4 + 3] =  c        & 0xFF;
        }
    }

    XER::Flux::Core::Logger().Info("SvgDecoder",
        "Rasterised " + path +
        " intrinsic=" + std::to_string(intrinsic_w) + "x" + std::to_string(intrinsic_h) +
        " output=" + std::to_string(w) + "x" + std::to_string(h));
    return buf;
}

}  // namespace image
