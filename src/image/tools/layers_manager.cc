#include "layers_manager.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include "flux/core/logger.h"
#include <algorithm>

namespace image {

void LayersManager::AddLayer(const std::string& name, ImageBuffer buf,
                              float opacity, const std::string& blend_mode) {
    Layer l;
    l.name = name; l.buffer = std::move(buf);
    l.opacity = opacity; l.blend_mode = blend_mode;
    layers_.push_back(std::move(l));
}

void LayersManager::RemoveLayer(const std::string& name) {
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
        [&](const Layer& l){ return l.name == name; }), layers_.end());
}

void LayersManager::SetVisible(const std::string& name, bool v) {
    for (auto& l : layers_) if (l.name==name) l.visible=v;
}
void LayersManager::SetOpacity(const std::string& name, float o) {
    for (auto& l : layers_) if (l.name==name) l.opacity=o;
}
void LayersManager::SetBlendMode(const std::string& name, const std::string& m) {
    for (auto& l : layers_) if (l.name==name) l.blend_mode=m;
}

ImageBuffer LayersManager::Flatten(int width, int height) const {
    ImageBuffer result(width, height, PixelFormat::RGBA8, ColorSpace::sRGB);
    result.Clear(0, 0, 0, 0);
    if (!XER::Bridge::Skia::IsAvailable()) {
        flux::core::Logger().Warning("LayersManager", "Skia not available, returning blank");
        return result;
    }
    std::vector<uint32_t> dst_px(width*height, 0);
    for (const auto& l : layers_) {
        if (!l.visible || !l.buffer.IsValid()) continue;
        int sw=l.buffer.Width(), sh=l.buffer.Height();
        std::vector<uint32_t> src_px(sw*sh);
        const uint8_t* d=l.buffer.Data();
        for (int i=0;i<sw*sh;++i) {
            uint8_t r=d[i*4+0], g=d[i*4+1], b=d[i*4+2];
            uint8_t a=static_cast<uint8_t>(d[i*4+3]*l.opacity);
            src_px[i]=XER::Bridge::Skia::MakeColorRGBA(r,g,b,a);
        }
        XER::Bridge::Skia::RasterDrawImage(&dst_px, width, height,
                                               src_px, sw, sh,
                                               0, 0, l.blend_mode);
    }
    uint8_t* od=result.Data();
    for (int i=0;i<width*height;++i) {
        uint32_t c=dst_px[i];
        od[i*4+0]=(c>>24)&0xFF; od[i*4+1]=(c>>16)&0xFF;
        od[i*4+2]=(c>>8)&0xFF;  od[i*4+3]=c&0xFF;
    }
    return result;
}

}  // namespace image
