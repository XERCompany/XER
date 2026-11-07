#include "wrapper/skia/skia_engine_bridge_svg.h"

#if ENGINE_HAS_SKIA_BRIDGE

#include "include/core/SkCanvas.h"
#include "include/core/SkStream.h"
#include "include/core/SkBitmap.h"

#if __has_include("modules/svg/include/SkSVGDOM.h") || __has_include(<modules/svg/include/SkSVGDOM.h>)
#define ENGINE_SKIA_HAS_SVG 1
#include "modules/svg/include/SkSVGDOM.h"
#endif

#endif

namespace XER::engine::bridge::skia {

    bool RasterDrawSVG(std::vector<uint32_t>* pixels, int width, int height,
                       const std::string& svg_content, float x, float y, float scale) {
#if ENGINE_HAS_SKIA_BRIDGE && defined(ENGINE_SKIA_HAS_SVG)
        if (!pixels || pixels->size() < (size_t)(width * height)) return false;

        SkMemoryStream stream(svg_content.c_str(), svg_content.size(), false);
        sk_sp<SkSVGDOM> svg_dom = SkSVGDOM::MakeFromStream(stream);
        
        if (!svg_dom) {
            return false;
        }

        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
        SkBitmap bitmap;
        bitmap.installPixels(info, pixels->data(), width * sizeof(uint32_t));

        SkCanvas canvas(bitmap);
        canvas.save();
        canvas.translate(x, y);
        canvas.scale(scale, scale);
        
        svg_dom->render(&canvas);
        
        canvas.restore();
        return true;
#else
        return false; // SVG requires SkSVGDOM
#endif
    }

}
