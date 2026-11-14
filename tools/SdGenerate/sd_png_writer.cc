#include "sd_png_writer.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace XER::Tools::SdGenerate {

namespace {

void PutU32(std::string* out, uint32_t v) {
    out->push_back(static_cast<char>((v >> 24) & 0xFF));
    out->push_back(static_cast<char>((v >> 16) & 0xFF));
    out->push_back(static_cast<char>((v >> 8) & 0xFF));
    out->push_back(static_cast<char>(v & 0xFF));
}

void PutChunk(std::string* png, const char type[4], const std::string& data) {
    PutU32(png, static_cast<uint32_t>(data.size()));
    const size_t crc_start = png->size();
    png->append(type, 4);
    png->append(data);
    // CRC covers chunk type + data.
    const uLong crc = crc32(0L, Z_NULL, 0);
    const uLong c = crc32(crc,
                          reinterpret_cast<const Bytef*>(png->data() + crc_start),
                          static_cast<uInt>(png->size() - crc_start));
    PutU32(png, static_cast<uint32_t>(c));
}

std::string BuildParametersText(const std::string& prompt,
                                const std::string& negative,
                                const std::map<std::string, std::string>& meta,
                                const std::string& sampler,
                                uint32_t w, uint32_t h) {
    auto get = [&](const char* k, const char* dflt) -> std::string {
        const auto it = meta.find(k);
        return it == meta.end() ? std::string(dflt) : it->second;
    };
    std::string out = prompt;
    if (!negative.empty()) out += "\nNegative prompt: " + negative;
    out += "\nSteps: " + get("steps", "?") +
           ", Sampler: " + sampler +
           ", CFG scale: " + get("guidance", "?") +
           ", Seed: " + get("seed", "?") +
           ", Size: " + std::to_string(w) + "x" + std::to_string(h) +
           ", Model: " + get("model", "?");
    return out;
}

}  // namespace

bool WritePng(const uint8_t* rgba, uint32_t width, uint32_t height,
              const std::string& path,
              const std::map<std::string, std::string>& metadata,
              const std::string& prompt, const std::string& negative_prompt,
              const std::string& sampler_name, std::string* error) {
    if (!rgba || width == 0 || height == 0) {
        if (error) *error = "empty image";
        return false;
    }

    // Raw scanlines: each row prefixed with filter byte 0 (None).
    const size_t stride = static_cast<size_t>(width) * 4;
    std::string raw;
    raw.reserve((stride + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back('\0');
        raw.append(reinterpret_cast<const char*>(rgba + stride * y), stride);
    }

    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::string idat(bound, '\0');
    if (compress2(reinterpret_cast<Bytef*>(idat.data()), &bound,
                  reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), Z_DEFAULT_COMPRESSION) != Z_OK) {
        if (error) *error = "zlib compress failed";
        return false;
    }
    idat.resize(bound);

    std::string png;
    png.reserve(idat.size() + 256);
    png.append("\x89PNG\r\n\x1a\n", 8);

    std::string ihdr;
    PutU32(&ihdr, width);
    PutU32(&ihdr, height);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // color type RGBA
    ihdr.push_back(0);   // compression
    ihdr.push_back(0);   // filter
    ihdr.push_back(0);   // interlace
    PutChunk(&png, "IHDR", ihdr);

    std::string text = "parameters\0" +
        BuildParametersText(prompt, negative_prompt, metadata, sampler_name,
                            width, height);
    PutChunk(&png, "tEXt", text);

    PutChunk(&png, "IDAT", idat);

    PutChunk(&png, "IEND", "");

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    f.write(png.data(), static_cast<std::streamsize>(png.size()));
    if (!f) {
        if (error) *error = "write failed for " + path;
        return false;
    }
    return true;
}

bool TensorToRgba8(const XER::MLData::Types::Tensor& image,
                   std::vector<uint8_t>* out_rgba, uint32_t* out_width,
                   uint32_t* out_height) {
    const auto shape = image.GetShape();
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 3 ||
        image.GetDataType() != XER::MLData::Types::DataType::FLOAT32 ||
        !image.GetData()) {
        return false;
    }
    const uint32_t h = shape[2];
    const uint32_t w = shape[3];
    const float* src = static_cast<const float*>(image.GetData());
    const uint64_t plane = static_cast<uint64_t>(w) * h;

    out_rgba->resize(static_cast<size_t>(w) * h * 4);
    for (uint64_t i = 0; i < plane; ++i) {
        const float r = std::clamp(src[i], 0.0f, 1.0f);
        const float g = std::clamp(src[plane + i], 0.0f, 1.0f);
        const float b = std::clamp(src[2 * plane + i], 0.0f, 1.0f);
        const size_t o = i * 4;
        (*out_rgba)[o + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
        (*out_rgba)[o + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
        (*out_rgba)[o + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
        (*out_rgba)[o + 3] = 255;
    }
    *out_width = w;
    *out_height = h;
    return true;
}

}  // namespace XER::Tools::SdGenerate
