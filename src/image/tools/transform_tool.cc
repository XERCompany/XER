#include "transform_tool.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace image {

ImageBuffer TransformTool::ResizeNN(const ImageBuffer& img, int nw, int nh) {
    if (!img.IsValid() || nw<=0 || nh<=0) return {};
    int bpp = BytesPerPixel(img.Format());
    ImageBuffer out(nw, nh, img.Format(), img.GetColorSpace());
    const uint8_t* s = img.Data();
    uint8_t* d = out.Data();
    for (int y=0;y<nh;++y) {
        int sy = y * img.Height() / nh;
        for (int x=0;x<nw;++x) {
            int sx = x * img.Width() / nw;
            std::memcpy(d+(y*nw+x)*bpp, s+(sy*img.Width()+sx)*bpp, bpp);
        }
    }
    return out;
}

ImageBuffer TransformTool::Resize(const ImageBuffer& img, int nw, int nh) {
    if (!XER::Bridge::Skia::IsAvailable())
        return ResizeNN(img, nw, nh);
    if (!img.IsValid() || nw<=0 || nh<=0) return {};
    int sw=img.Width(), sh=img.Height();
    // Use Skia RasterDrawImage: dst starts blank, draw src scaled into it
    std::vector<uint32_t> src_px(sw*sh), dst_px(nw*nh, 0);
    const uint8_t* d=img.Data();
    for (int i=0;i<sw*sh;++i)
        src_px[i]=XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
    XER::Bridge::Skia::RasterDrawImage(&dst_px, nw, nh,
                                           src_px, sw, sh,
                                           0, 0, "src");
    ImageBuffer out(nw, nh, img.Format(), img.GetColorSpace());
    uint8_t* od = out.Data();
    for (int i=0;i<nw*nh;++i) {
        uint32_t c=dst_px[i];
        od[i*4+0]=(c>>24)&0xFF; od[i*4+1]=(c>>16)&0xFF;
        od[i*4+2]=(c>>8)&0xFF;  od[i*4+3]=c&0xFF;
    }
    return out;
}

ImageBuffer TransformTool::FitInto(const ImageBuffer& img, int max_w, int max_h) {
    if (!img.IsValid()) return {};
    float sx = static_cast<float>(max_w) / img.Width();
    float sy = static_cast<float>(max_h) / img.Height();
    float s  = std::min(sx, sy);
    return Resize(img, static_cast<int>(img.Width()*s),
                       static_cast<int>(img.Height()*s));
}

ImageBuffer TransformTool::Affine(const ImageBuffer& img,
                                   float a, float b, float c,
                                   float d, float e, float f,
                                   int out_w, int out_h) {
    if (!img.IsValid()) return {};
    int sw=img.Width(), sh=img.Height(), bpp=BytesPerPixel(img.Format());
    ImageBuffer out(out_w, out_h, img.Format(), img.GetColorSpace());
    out.Clear(0, 0, 0, 0);
    const uint8_t* src=img.Data();
    uint8_t* dst=out.Data();
    // Inverse mapping
    float det = a*e - b*d;
    if (std::abs(det) < 1e-7f) return out;
    float ia=(e/det), ib=(-b/det), ic=(b*f-e*c)/det;
    float id=(-d/det), ie=(a/det), iif=(d*c-a*f)/det;
    for (int oy=0;oy<out_h;++oy) for (int ox=0;ox<out_w;++ox) {
        float sx2=ia*ox+ib*oy+ic, sy2=id*ox+ie*oy+iif;
        int px=static_cast<int>(sx2+0.5f), py=static_cast<int>(sy2+0.5f);
        if (px>=0&&px<sw&&py>=0&&py<sh)
            std::memcpy(dst+(oy*out_w+ox)*bpp, src+(py*sw+px)*bpp, bpp);
    }
    return out;
}

}  // namespace image
