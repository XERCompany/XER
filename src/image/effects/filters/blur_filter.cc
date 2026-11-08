#include "blur_filter.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace image {

void BlurFilter::Apply(ImageBuffer& img) const {
    if (!img.IsValid()) return;
    int w = img.Width(), h = img.Height();
    std::vector<uint32_t> px(w * h);
    const uint8_t* d = img.Data();
    for (int i = 0; i < w*h; ++i)
        px[i] = XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
    if (gaussian_)
        XER::Bridge::Skia::FilterGaussianBlur(&px, w, h, sigma_, sigma_);
    else
        XER::Bridge::Skia::FilterBlur(&px, w, h,
                                         static_cast<int>(sigma_),
                                         static_cast<int>(sigma_));
    uint8_t* dst = img.Data();
    for (int i = 0; i < w*h; ++i) {
        uint32_t c = px[i];
        dst[i*4+0]=(c>>24)&0xFF; dst[i*4+1]=(c>>16)&0xFF;
        dst[i*4+2]=(c>>8)&0xFF;  dst[i*4+3]=c&0xFF;
    }
}

}  // namespace image
