// Image I/O via OpenImageIO — reads formats WIC/FFmpeg can't (EXR,
// DPX, TIFF, RAW via libraw, PSD) and writes review/sequence frames
// (PNG/EXR/TIFF). Degrades gracefully: every entry point returns
// false when OIIO is absent — callers keep existing paths.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace XER::VideoEditor::Io {

struct Image {
    std::vector<std::uint8_t> bgra;  // B,G,R,A — DrawFrameBgra-ready
    int width = 0;
    int height = 0;
    bool has_alpha = false;
    bool ok = false;
};

// Decodes any OIIO-supported image to straight-alpha BGRA8.
// HDR sources are tone-mapped with a simple Reinhard + sRGB curve.
Image ReadImage(const std::filesystem::path& path);

// Writes a BGRA8 buffer as PNG/TIFF/EXR (chosen by extension).
bool WriteImage(const std::filesystem::path& path,
                const std::uint8_t* bgra, int width, int height);

// True for extensions OIIO handles that WIC doesn't: exr, dpx, tif,
// tiff, raw-family, psd, hdr.
bool OiioHandlesExtension(const std::string& ext_lower);

}  // namespace XER::VideoEditor::Io
