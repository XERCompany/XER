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

#include "helper/path_utils.h"

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
    // Paths are UTF-8; fs::path and ifstream need the native (wide) form.
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    const auto fs_path = XER::Helper::Path::FromUtf8(upath);
    // Try magic bytes first
    std::ifstream f(fs_path, std::ios::binary);
    if (f) {
        uint8_t buf[16] = {};
        f.read(reinterpret_cast<char*>(buf), sizeof(buf));
        auto* d = FindByMagic(buf, static_cast<std::size_t>(f.gcount()));
        if (d) return d;
    }
    // Fallback: extension
    std::string ext = XER::Helper::String::ToLowerAscii(
        XER::Helper::Path::ToUtf8(fs_path.extension()));
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    for (auto& d : decoders_) {
        for (auto& e : d->Extensions())
            if (e == ext) return d.get();
    }
    return nullptr;
}

std::shared_ptr<ImageBuffer> DecoderFactory::Decode(const std::string& path,
                                                      ImageDescriptor& desc) {
    // Hand decoders clean UTF-8 (FFmpeg expects UTF-8 filenames on Windows).
    const std::string upath = XER::Helper::Path::NormalizeUtf8Path(path);
    BaseDecoder* d = Find(upath);
    if (!d) return nullptr;
    return d->Decode(upath, desc);
}

}  // namespace image
