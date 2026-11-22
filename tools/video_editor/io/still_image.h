// Still-image I/O — reads formats WIC/FFmpeg image2 can't (EXR, DPX,
// TIFF, RAW, PSD, Radiance HDR) and writes review/sequence frames
// (PNG/JPEG plus TIFF/EXR when OIIO is present).
//
// Decode routes through image::ImageLoader (src/image — magic-detect
// over exr/tiff/psd/raw/heif/webp/gif/jpeg/png/svg). A narrow OIIO
// fallback covers only the extensions the src/image decoder set does
// not: DPX, Radiance .hdr, PFM, ORA, R3D and generic .raw. Encode
// routes through image::ImageSaver for PNG/JPEG; TIFF/EXR (and any
// other extension) stay on OIIO since the saver has no encoders for
// them. Both paths degrade gracefully: absent backends return
// false/{ok=false} so callers keep their existing fallbacks.
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

// Decodes any supported still image to straight-alpha BGRA8.
// HDR sources are tone-mapped with a simple Reinhard + sRGB curve.
Image ReadImage(const std::filesystem::path& path);

// Writes a BGRA8 buffer by extension: PNG/JPEG go through
// image::ImageSaver (FFmpeg encoder); TIFF/EXR/anything else need the
// OIIO fallback — returns false when it is absent.
bool WriteImage(const std::filesystem::path& path,
                const std::uint8_t* bgra, int width, int height);

// True for every extension the still-image path can rasterize: the
// ImageLoader set (exr, tif/tiff, psd/psb, raw-family, heif/avif,
// webp, gif, jpeg, png, svg) plus the OIIO residual (dpx, hdr, pfm,
// ora, r3d, raw). Callers use it to gate the still fallback after the
// FFmpeg container probe fails — the decode itself is the accept test.
bool StillImageHandlesExtension(const std::string& ext_lower);

}  // namespace XER::VideoEditor::Io
