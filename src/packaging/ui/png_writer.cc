/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "packaging/ui/png_writer.h"

#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace XER::Packaging::Ui {

namespace {

void PutU32(std::string* out, std::uint32_t v) {
    out->push_back(static_cast<char>((v >> 24) & 0xFF));
    out->push_back(static_cast<char>((v >> 16) & 0xFF));
    out->push_back(static_cast<char>((v >> 8) & 0xFF));
    out->push_back(static_cast<char>(v & 0xFF));
}

void PutChunk(std::string* png, const char type[4],
              const std::string& data) {
    PutU32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t crc_start = png->size();
    png->append(type, 4);
    png->append(data);
    const uLong crc = crc32(0L, Z_NULL, 0);
    const uLong c =
        crc32(crc,
              reinterpret_cast<const Bytef*>(png->data() + crc_start),
              static_cast<uInt>(png->size() - crc_start));
    PutU32(png, static_cast<std::uint32_t>(c));
}

}  // namespace

bool WriteBgraPng(const std::uint8_t* bgra, int width, int height,
                  const std::string& utf8_path, std::string* error) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        if (error != nullptr) *error = "empty pixel buffer";
        return false;
    }
    // Raw scanlines: filter byte 0 + RGBA (swap B<->R).
    const std::size_t stride = static_cast<std::size_t>(width) * 4 + 1;
    std::string raw;
    raw.resize(stride * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        char* row = raw.data() + stride * static_cast<std::size_t>(y);
        row[0] = 0;
        const std::uint8_t* src =
            bgra + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            row[1 + x * 4 + 0] = static_cast<char>(src[x * 4 + 2]);
            row[1 + x * 4 + 1] = static_cast<char>(src[x * 4 + 1]);
            row[1 + x * 4 + 2] = static_cast<char>(src[x * 4 + 0]);
            row[1 + x * 4 + 3] = static_cast<char>(src[x * 4 + 3]);
        }
    }

    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> idat(bound);
    const int rc = compress2(idat.data(), &bound,
                             reinterpret_cast<const Bytef*>(raw.data()),
                             static_cast<uLong>(raw.size()),
                             Z_BEST_SPEED);
    if (rc != Z_OK) {
        if (error != nullptr)
            *error = "zlib compress failed: " + std::to_string(rc);
        return false;
    }

    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    PutU32(&ihdr, static_cast<std::uint32_t>(width));
    PutU32(&ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filters
    ihdr.push_back(0);  // no interlace
    PutChunk(&png, "IHDR", ihdr);
    PutChunk(&png, "IDAT",
             std::string(reinterpret_cast<const char*>(idat.data()),
                         bound));
    PutChunk(&png, "IEND", {});

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(utf8_path).parent_path(), ec);
    std::ofstream out(utf8_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr)
            *error = "cannot write " + utf8_path;
        return false;
    }
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

}  // namespace XER::Packaging::Ui
