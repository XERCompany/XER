#include "lens_flare.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <cmath>

namespace image {

LensFlare::LensFlare(int sx, int sy, float intensity)
    : src_x_(sx), src_y_(sy), intensity_(intensity) {
    elements_ = {
        {0.0f, 60.0f, XER::Bridge::Skia::MakeColorRGBA(255,200,100,200), 0.8f},
        {0.3f, 20.0f, XER::Bridge::Skia::MakeColorRGBA(200,220,255,180), 0.5f},
        {0.5f, 35.0f, XER::Bridge::Skia::MakeColorRGBA(255,180,50, 150), 0.4f},
        {0.7f, 15.0f, XER::Bridge::Skia::MakeColorRGBA(100,200,255,160), 0.6f},
        {1.0f, 10.0f, XER::Bridge::Skia::MakeColorRGBA(255,255,200,120), 0.3f},
    };
}

void LensFlare::Apply(ImageBuffer& img) const {
    if (!img.IsValid() || !XER::Bridge::Skia::IsAvailable()) return;
    int w=img.Width(), h=img.Height();
    int cx=w/2, cy=h/2;
    int ax=2*cx-src_x_, ay=2*cy-src_y_;
    std::vector<uint32_t> px(w*h);
    const uint8_t* d=img.Data();
    for (int i=0;i<w*h;++i)
        px[i]=XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
    for (auto& el : elements_) {
        int ex=static_cast<int>(src_x_+(ax-src_x_)*el.pos);
        int ey=static_cast<int>(src_y_+(ay-src_y_)*el.pos);
        int r=std::max(1,static_cast<int>(el.size*intensity_));
        std::vector<std::pair<int,int>> pts;
        for (int a=0;a<32;++a) {
            float ang=a*6.2831853f/32.0f;
            pts.push_back({ex+static_cast<int>(std::cos(ang)*r),
                           ey+static_cast<int>(std::sin(ang)*r)});
        }
        uint32_t c=el.color;
        uint8_t a8=static_cast<uint8_t>((c&0xFF)*el.opacity*intensity_);
        XER::Bridge::Skia::RasterFillPolygon(&px,w,h,pts,(c&0xFFFFFF00u)|a8,"src_over");
    }
    uint8_t* dst=img.Data();
    for (int i=0;i<w*h;++i) {
        uint32_t c=px[i];
        dst[i*4+0]=(c>>24)&0xFF; dst[i*4+1]=(c>>16)&0xFF;
        dst[i*4+2]=(c>>8)&0xFF;  dst[i*4+3]=c&0xFF;
    }
}

}  // namespace image
