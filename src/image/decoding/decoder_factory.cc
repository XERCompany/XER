#include "decoder_factory.h"
#include "magic_number_detector.h"
#include "helper/string_utils.h"
#include "../formats/jpeg_decoder.h"
#include "../formats/png_decoder.h"
#include "../formats/gif_decoder.h"
#include "../formats/webp_decoder.h"
#include "../formats/tiff_decoder.h"
#include "../formats/exr_decoder.h"
#include "../formats/heif_decoder.h"
#include "../formats/psd_decoder.h"
#include "../formats/svg_decoder.h"
#include "../formats/raw_cr2_decoder.h"
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace image {

DecoderFactory& DecoderFactory::Instance() {
    static DecoderFactory inst;
    inst.RegisterBuiltins();
    return inst;
}

void DecoderFactory::Register(std::unique_ptr<BaseDecoder> dec) {
    decoders_.push_back(std::move(dec));
}

void DecoderFactory::RegisterBuiltins() {
    if (builtins_registered_) return;
    builtins_registered_ = true;
    Register(std::make_unique<JpegDecoder>());
    Register(std::make_unique<PngDecoder>());
    Register(std::make_unique<GifDecoder>());
    Register(std::make_unique<WebpDecoder>());
    Register(std::make_unique<TiffDecoder>());
    Register(std::make_unique<ExrDecoder>());
    Register(std::make_unique<HeifDecoder>());
    Register(std::make_unique<PsdDecoder>());
    Register(std::make_unique<SvgDecoder>());
    Register(std::make_unique<RawCr2Decoder>());
}

BaseDecoder* DecoderFactory::FindByMagic(const uint8_t* header, std::size_t len) {
    for (auto& d : decoders_)
        if (d->Probe(header, len)) return d.get();
    return nullptr;
}

BaseDecoder* DecoderFactory::Find(const std::string& path) {
    // Try magic bytes first
    std::ifstream f(path, std::ios::binary);
    if (f) {
        uint8_t buf[16] = {};
        f.read(reinterpret_cast<char*>(buf), sizeof(buf));
        auto* d = FindByMagic(buf, static_cast<std::size_t>(f.gcount()));
        if (d) return d;
    }
    // Fallback: extension
    std::string ext = XER::Helper::String::ToLowerAscii(
        std::filesystem::path(path).extension().string());
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& d : decoders_) {
        for (auto& e : d->Extensions())
            if (e == ext) return d.get();
    }
    return nullptr;
}

std::shared_ptr<ImageBuffer> DecoderFactory::Decode(const std::string& path,
                                                      ImageDescriptor& desc) {
    BaseDecoder* d = Find(path);
    if (!d) return nullptr;
    return d->Decode(path, desc);
}

}  // namespace image
