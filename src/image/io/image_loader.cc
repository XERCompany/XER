#include "image_loader.h"
#include "../decoding/decoder_factory.h"
#include "../decoding/multi_page_decoder.h"
#include "helper/path_utils.h"
#include "flux/core/logger.h"

namespace image {

std::shared_ptr<ImageBuffer> ImageLoader::Load(const std::string& path,
                                                 ImageDescriptor& desc) {
    auto& factory = DecoderFactory::Instance();
    auto buf = factory.Decode(path, desc);
    if (!buf)
        XER::Flux::Core::Logger().Warning("ImageLoader", "Failed to load: " + path);
    return buf;
}

std::shared_ptr<ImageBuffer> ImageLoader::Load(const std::string& path) {
    ImageDescriptor desc;
    return Load(path, desc);
}

std::vector<std::shared_ptr<ImageBuffer>>
ImageLoader::LoadAllPages(const std::string& path, ImageDescriptor& desc) {
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    auto& factory = DecoderFactory::Instance();
    BaseDecoder* d = factory.Find(upath);
    if (!d) return {};
    if (auto* mp = dynamic_cast<MultiPageDecoder*>(d))
        return mp->DecodeAll(upath, desc);
    auto single = d->Decode(upath, desc);
    if (single) return {single};
    return {};
}

std::shared_ptr<ImageBuffer> ImageLoader::LoadMemory(const uint8_t* data,
                                                       std::size_t len,
                                                       ImageDescriptor& desc) {
    auto& factory = DecoderFactory::Instance();
    BaseDecoder* d = factory.FindByMagic(data, std::min(len, std::size_t(16)));
    if (!d) return nullptr;
    return d->DecodeMemory(data, len, desc);
}

}  // namespace image
