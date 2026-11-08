#include "vfx_engine.h"
#include "../filters/blur_filter.h"
#include "../filters/sharpen_filter.h"
#include "../filters/color_matrix_filter.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace image {

void VfxEngine::AddBlur(double sigma, bool gaussian) {
    BlurFilter f(sigma, gaussian);
    chain_.Add("blur", [f](ImageBuffer& img){ f.Apply(img); });
}

void VfxEngine::AddSharpen(double amount, double sigma) {
    SharpenFilter f(amount, sigma);
    chain_.Add("sharpen", [f](ImageBuffer& img){ f.Apply(img); });
}

void VfxEngine::AddGrayscale() {
    auto f = ColorMatrixFilter::Grayscale();
    chain_.Add("grayscale", [f](ImageBuffer& img){ f.Apply(img); });
}

void VfxEngine::AddSepia(float amount) {
    auto f = ColorMatrixFilter::Sepia(amount);
    chain_.Add("sepia", [f](ImageBuffer& img){ f.Apply(img); });
}

void VfxEngine::AddInvert() {
    auto f = ColorMatrixFilter::Invert();
    chain_.Add("invert", [f](ImageBuffer& img){ f.Apply(img); });
}

void VfxEngine::AddEdgeDetect() {
    chain_.Add("edge", [](ImageBuffer& img) {
        if (!XER::Bridge::Skia::IsAvailable()) return;
        int w=img.Width(), h=img.Height();
        std::vector<uint32_t> px(w*h);
        const uint8_t* d=img.Data();
        for (int i=0;i<w*h;++i)
            px[i]=XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
        XER::Bridge::Skia::FilterEdgeDetect(&px, w, h);
        uint8_t* dst=img.Data();
        for (int i=0;i<w*h;++i) {
            uint32_t c=px[i];
            dst[i*4+0]=(c>>24)&0xFF; dst[i*4+1]=(c>>16)&0xFF;
            dst[i*4+2]=(c>>8)&0xFF;  dst[i*4+3]=c&0xFF;
        }
    });
}

void VfxEngine::AddEmboss(double angle, double strength) {
    chain_.Add("emboss", [angle, strength](ImageBuffer& img) {
        if (!XER::Bridge::Skia::IsAvailable()) return;
        int w=img.Width(), h=img.Height();
        std::vector<uint32_t> px(w*h);
        const uint8_t* d=img.Data();
        for (int i=0;i<w*h;++i)
            px[i]=XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
        XER::Bridge::Skia::FilterEmboss(&px, w, h, angle, strength);
        uint8_t* dst=img.Data();
        for (int i=0;i<w*h;++i) {
            uint32_t c=px[i];
            dst[i*4+0]=(c>>24)&0xFF; dst[i*4+1]=(c>>16)&0xFF;
            dst[i*4+2]=(c>>8)&0xFF;  dst[i*4+3]=c&0xFF;
        }
    });
}

void VfxEngine::AddVignette(double strength, double feather) {
    chain_.Add("vignette", [strength, feather](ImageBuffer& img) {
        if (!XER::Bridge::Skia::IsAvailable()) return;
        int w=img.Width(), h=img.Height();
        std::vector<uint32_t> px(w*h);
        const uint8_t* d=img.Data();
        for (int i=0;i<w*h;++i)
            px[i]=XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
        XER::Bridge::Skia::FilterVignette(&px, w, h, strength, feather);
        uint8_t* dst=img.Data();
        for (int i=0;i<w*h;++i) {
            uint32_t c=px[i];
            dst[i*4+0]=(c>>24)&0xFF; dst[i*4+1]=(c>>16)&0xFF;
            dst[i*4+2]=(c>>8)&0xFF;  dst[i*4+3]=c&0xFF;
        }
    });
}

void VfxEngine::Process(ImageBuffer& img) const { chain_.Apply(img); }

}  // namespace image
