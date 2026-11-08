#include "wrapper/skia/skia_engine_bridge_codec_extra.h"

#if ENGINE_HAS_SKIA_BRIDGE
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/core/SkData.h"

#if __has_include("include/encode/SkWebpEncoder.h") || __has_include(<include/encode/SkWebpEncoder.h>)
#define ENGINE_SKIA_HAS_WEBP 1
#include "include/encode/SkWebpEncoder.h"
#endif
#endif

namespace XER::Bridge::Skia {

    bool EncodeImageWEBP(const std::vector<uint32_t>& pixels, int width, int height,
                         int quality, std::vector<uint8_t>* out_bytes) {
#if ENGINE_HAS_SKIA_BRIDGE && defined(ENGINE_SKIA_HAS_WEBP)
        if (pixels.size() < (size_t)(width * height) || !out_bytes) return false;
        
        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
        SkPixmap pixmap(info, pixels.data(), width * sizeof(uint32_t));
        
        SkDynamicMemoryWStream stream;
        SkWebpEncoder::Options options;
        options.fQuality = quality;
        
        if (SkWebpEncoder::Encode(&stream, pixmap, options)) {
            sk_sp<SkData> data = stream.detachAsData();
            if (data && data->size() > 0) {
                out_bytes->assign(data->bytes(), data->bytes() + data->size());
                return true;
            }
        }
#endif
        return false;
    }

    bool EncodeImageGIF(const std::vector<uint32_t>& pixels, int width, int height,
                        std::vector<uint8_t>* out_bytes) {
        // Skia typically doesn't have a built-in robust GIF encoder out of the box in standard deps,
        // so we return false unless we bundle giflib externally.
        return false; 
    }

}
