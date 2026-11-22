// still_image.cc — see still_image.h for the contract.
//
// Decode: image::ImageLoader (src/image magic-detect decoders) owns
// every format it declares — a failure there is the final answer, no
// silent second decode. OpenImageIO survives only as the narrow
// fallback for extensions the loader has no decoder for (DPX, Radiance
// .hdr, PFM, ORA, R3D, generic .raw) and for the encode formats the
// image::ImageSaver can't write (TIFF/EXR/...). The OIIO import is
// compiled only when the headers are installed; without them the
// residual paths degrade to failure like before.

#include "still_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "helper/path_utils.h"
#include "image/core/image_buffer.h"
#include "image/core/image_descriptor.h"
#include "image/core/pixel_format.h"
#include "image/io/image_loader.h"
#include "image/io/image_saver.h"

#if __has_include(<OpenImageIO/imageio.h>)
#define XER_VE_HAVE_OIIO 1
#include <OpenImageIO/imageio.h>
#else
#define XER_VE_HAVE_OIIO 0
#endif

#if XER_VE_HAVE_OIIO && defined(_WIN32) && \
    defined(XER_VE_OIIO_RUNTIME_DLLS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace XER::VideoEditor::Io {
namespace {

// ".EXR" / "EXR" -> "exr" — the extension normalisation the OIIO
// predicate documented (the un-normalised lookup silently rejected
// every dot-prefixed extension once — bug-audit finding).
std::string NormalizeExt(const std::string& ext) {
    std::string key = ext;
    if (!key.empty() && key.front() == '.') {
        key.erase(key.begin());
    }
    for (char& c : key) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return key;
}

// [0,inf) -> byte. HDR/linear sources get Reinhard + a 1/2.2 display
// gamma; LDR sources are assumed display-referred and only clamped.
// Alpha is never tone-mapped — it stays a plain 0..1 coverage value.
std::uint8_t ColorToByte(float v, bool hdr) {
    if (!(v > 0.0f)) {
        return 0;  // also catches NaN
    }
    if (hdr) {
        v = v / (1.0f + v);               // Reinhard
        v = std::pow(v, 1.0f / 2.2f);     // linear -> sRGB-ish
    }
    v = v * 255.0f + 0.5f;
    return v >= 255.0f ? std::uint8_t{255}
                       : static_cast<std::uint8_t>(v);
}

std::uint8_t AlphaToByte(float v) {
    if (!(v > 0.0f)) {
        return 0;
    }
    v = v * 255.0f + 0.5f;
    return v >= 255.0f ? std::uint8_t{255}
                       : static_cast<std::uint8_t>(v);
}

// Repack an image::ImageBuffer into the BGRA8 Io::Image contract.
// RGBA32F sources live in FloatData(); everything else in Data() with
// the declared packed PixelFormat.
bool FromImageBuffer(const image::ImageBuffer& buf, bool has_alpha,
                     Image* out) {
    const int w = buf.Width();
    const int h = buf.Height();
    if (w <= 0 || h <= 0) {
        return false;
    }
    const std::size_t npix =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h);

    const std::vector<float>& fdata = buf.FloatData();
    if (!fdata.empty()) {
        if (fdata.size() < npix * 4u) {
            return false;
        }
        // Tone-map only when the source actually exceeds the display
        // range — EXR float planes decoded through FFmpeg arrive
        // normalised [0,1] and must not get a second Reinhard.
        float max_v = 0.0f;
        for (const float v : fdata) {
            max_v = std::max(max_v, v);
        }
        const bool hdr = max_v > 1.0f;
        out->bgra.resize(npix * 4u);
        for (std::size_t i = 0; i < npix; ++i) {
            const float* p = fdata.data() + i * 4u;
            std::uint8_t* d = out->bgra.data() + i * 4u;
            d[0] = ColorToByte(p[2], hdr);
            d[1] = ColorToByte(p[1], hdr);
            d[2] = ColorToByte(p[0], hdr);
            d[3] = AlphaToByte(p[3]);
        }
        out->width = w;
        out->height = h;
        out->has_alpha = has_alpha;
        out->ok = true;
        return true;
    }

    const std::uint8_t* s = buf.Data();
    const std::size_t have = buf.DataSize();
    if (s == nullptr || have == 0) {
        return false;
    }
    out->bgra.resize(npix * 4u);
    std::uint8_t* dst = out->bgra.data();
    switch (buf.Format()) {
        case image::PixelFormat::RGBA8:
            if (have < npix * 4u) return false;
            for (std::size_t i = 0; i < npix; ++i) {
                dst[i * 4u + 0] = s[i * 4u + 2];
                dst[i * 4u + 1] = s[i * 4u + 1];
                dst[i * 4u + 2] = s[i * 4u + 0];
                dst[i * 4u + 3] = s[i * 4u + 3];
            }
            break;
        case image::PixelFormat::BGRA8:
            if (have < npix * 4u) return false;
            std::memcpy(dst, s, npix * 4u);
            break;
        case image::PixelFormat::RGB8:
            if (have < npix * 3u) return false;
            for (std::size_t i = 0; i < npix; ++i) {
                dst[i * 4u + 0] = s[i * 3u + 2];
                dst[i * 4u + 1] = s[i * 3u + 1];
                dst[i * 4u + 2] = s[i * 3u + 0];
                dst[i * 4u + 3] = 255;
            }
            break;
        case image::PixelFormat::BGR8:
            if (have < npix * 3u) return false;
            for (std::size_t i = 0; i < npix; ++i) {
                dst[i * 4u + 0] = s[i * 3u + 0];
                dst[i * 4u + 1] = s[i * 3u + 1];
                dst[i * 4u + 2] = s[i * 3u + 2];
                dst[i * 4u + 3] = 255;
            }
            break;
        case image::PixelFormat::Gray8:
            if (have < npix) return false;
            for (std::size_t i = 0; i < npix; ++i) {
                dst[i * 4u + 0] = s[i];
                dst[i * 4u + 1] = s[i];
                dst[i * 4u + 2] = s[i];
                dst[i * 4u + 3] = 255;
            }
            break;
        case image::PixelFormat::Gray16:
            // Little-endian 16-bit — the high byte is the display value.
            if (have < npix * 2u) return false;
            for (std::size_t i = 0; i < npix; ++i) {
                const std::uint8_t v = s[i * 2u + 1u];
                dst[i * 4u + 0] = v;
                dst[i * 4u + 1] = v;
                dst[i * 4u + 2] = v;
                dst[i * 4u + 3] = 255;
            }
            break;
        case image::PixelFormat::RGBA16:
            // LE R,G,B,A 16-bit each — take the high byte per channel.
            if (have < npix * 8u) return false;
            for (std::size_t i = 0; i < npix; ++i) {
                dst[i * 4u + 0] = s[i * 8u + 5u];
                dst[i * 4u + 1] = s[i * 8u + 3u];
                dst[i * 4u + 2] = s[i * 8u + 1u];
                dst[i * 4u + 3] = s[i * 8u + 7u];
            }
            break;
        default:
            return false;  // planar/unknown — caller reports decode failure
    }
    out->width = w;
    out->height = h;
    out->has_alpha = has_alpha;
    out->ok = true;
    return true;
}

#if XER_VE_HAVE_OIIO

// Extensions ImageLoader has no decoder for — the entire remaining
// OIIO surface. (PFM/ORA/R3D/.raw are tiny but were advertised by the
// import filter before the swap; keeping them costs nothing while the
// DPX/HDR fallback is still linked.)
bool OiioResidualExtension(const std::string& ext_lower) {
    static const std::unordered_set<std::string> kResidual = {
        "dpx", "hdr", "pfm", "ora", "r3d", "raw",
    };
    return kResidual.count(ext_lower) != 0;
}

// The OIIO import is delay-loaded into the module: the first call into
// the DLL fires the GNU delay-load helper, which terminates the process
// outright when the DLL is absent. Probe with LoadLibrary first so a
// missing runtime degrades to failure (the documented contract) instead
// of killing the host; a module that loads stays resident and the
// delay-import binder then binds to it directly.
// XER_VE_OIIO_RUNTIME_DLLS is the '|'-separated list of delay-load
// target names resolved at link time.
bool EnsureOiioRuntime() {
#if defined(_WIN32) && defined(XER_VE_OIIO_RUNTIME_DLLS)
    static const bool kLoaded = [] {
        const std::string names = XER_VE_OIIO_RUNTIME_DLLS;
        std::size_t pos = 0;
        while (true) {
            const std::size_t bar = names.find('|', pos);
            const std::string name =
                names.substr(pos, bar == std::string::npos
                                      ? std::string::npos
                                      : bar - pos);
            if (!name.empty()) {
                const std::wstring wname(name.begin(), name.end());
                if (::LoadLibraryW(wname.c_str()) == nullptr) {
                    return false;
                }
            }
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
        return true;
    }();
    return kLoaded;
#else
    return true;
#endif
}

// Residual decode path — only reached for extensions OiioResidualExtension
// accepts (DPX/HDR/PFM/ORA/R3D/RAW). Same float-read + BGRA contract the
// old full-OIIO ReadImage had.
Image ReadImageOiio(const std::filesystem::path& path) {
    if (!EnsureOiioRuntime()) return {};  // absent runtime
    // OIIO filename params are const std::string& / string_view /
    // fs::path depending on the release — a UTF-8 std::string converts
    // to all of them, and OIIO expects UTF-8 bytes anyway.
    const std::string filename = Helper::Path::ToUtf8(path);
    auto in = OIIO::ImageInput::open(filename);
    if (!in) {
        // A failed open leaves the reason in OIIO's global error
        // state — drain it. Callers probe arbitrary files through this
        // path (the catalog's still-image import fallback), and an
        // undrained error trips OIIO's atexit pending-error report.
        (void)OIIO::geterror();
        return {};
    }
    const OIIO::ImageSpec& spec = in->spec();
    const int file_channels = spec.nchannels;
    if (spec.width <= 0 || spec.height <= 0 || file_channels < 1) {
        in->close();
        return {};
    }

    // Always read as float — OIIO converts from the native format —
    // and only pull the first 4 channels.
    const int w = spec.width;
    const int h = spec.height;
    const int nch = std::min(file_channels, 4);
    const std::size_t npix =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<float> pix(npix * static_cast<std::size_t>(nch));
    if (!in->read_image(0, 0, 0, nch, OIIO::TypeDesc::FLOAT,
                        pix.data())) {
        in->close();
        return {};
    }
    in->close();

    // HDR/linear when the source format is floating point or the data
    // actually exceeds the 0..1 display range.
    float max_v = 0.0f;
    for (const float v : pix) {
        max_v = std::max(max_v, v);
    }
    const bool hdr = spec.format == OIIO::TypeDesc::FLOAT ||
                     spec.format == OIIO::TypeDesc::HALF ||
                     spec.format == OIIO::TypeDesc::DOUBLE ||
                     max_v > 1.0f;

    Image out;
    out.width = w;
    out.height = h;
    out.has_alpha = file_channels >= 2 && file_channels != 3;
    out.bgra.resize(npix * 4u);
    for (std::size_t i = 0; i < npix; ++i) {
        const float* p = pix.data() + i * static_cast<std::size_t>(nch);
        float r, g, b, a;
        switch (nch) {
            case 1:  r = p[0]; g = p[0]; b = p[0]; a = 1.0f; break;
            case 2:  r = p[0]; g = p[0]; b = p[0]; a = p[1]; break;
            case 3:  r = p[0]; g = p[1]; b = p[2]; a = 1.0f; break;
            default: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
        }
        std::uint8_t* d = out.bgra.data() + i * 4u;
        d[0] = ColorToByte(b, hdr);
        d[1] = ColorToByte(g, hdr);
        d[2] = ColorToByte(r, hdr);
        d[3] = AlphaToByte(a);
    }
    out.ok = true;
    return out;
}

// Residual encode path — TIFF/EXR and anything ImageSaver does not
// write (also the backup when the FFmpeg encoder path fails a PNG).
bool WriteImageOiio(const std::filesystem::path& path,
                    const std::uint8_t* bgra, int width, int height) {
    if (!EnsureOiioRuntime()) return false;  // absent runtime
    const std::string filename = Helper::Path::ToUtf8(path);
    auto out = OIIO::ImageOutput::create(filename);
    if (!out) {
        return false;
    }
    OIIO::ImageSpec spec(width, height, 4, OIIO::TypeDesc::UINT8);
    spec.channelnames = {"R", "G", "B", "A"};
    if (!out->open(filename, spec)) {
        return false;
    }

    // OIIO writes RGB(A); repack BGRA -> RGBA.
    const std::size_t npix =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> rgba(npix * 4u);
    for (std::size_t i = 0; i < npix; ++i) {
        rgba[i * 4u + 0] = bgra[i * 4u + 2];
        rgba[i * 4u + 1] = bgra[i * 4u + 1];
        rgba[i * 4u + 2] = bgra[i * 4u + 0];
        rgba[i * 4u + 3] = bgra[i * 4u + 3];
    }
    const bool ok = out->write_image(OIIO::TypeDesc::UINT8, rgba.data());
    out->close();
    return ok;
}

#endif  // XER_VE_HAVE_OIIO

}  // namespace

Image ReadImage(const std::filesystem::path& path) {
    // src/image decoder chain — magic-byte detect with an extension
    // fallback. It owns every format it declares: a failure here is the
    // final answer for covered formats (no silent OIIO second decode).
    image::ImageDescriptor desc;
    std::shared_ptr<image::ImageBuffer> buf =
        image::ImageLoader().Load(Helper::Path::ToUtf8(path), desc);
    if (buf != nullptr && buf->Width() > 0 && buf->Height() > 0) {
        Image out;
        if (FromImageBuffer(*buf, desc.has_alpha, &out)) {
            return out;
        }
    }

#if XER_VE_HAVE_OIIO
    // Residual only: the loader declared no decoder for these
    // extensions, so the OIIO attempt cannot shadow a covered format.
    if (OiioResidualExtension(NormalizeExt(
            Helper::Path::ToUtf8(path.extension())))) {
        return ReadImageOiio(path);
    }
#endif
    return {};
}

bool WriteImage(const std::filesystem::path& path,
                const std::uint8_t* bgra, int width, int height) {
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const std::string ext =
        NormalizeExt(Helper::Path::ToUtf8(path.extension()));
    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        // image::ImageSaver — FFmpeg png/mjpeg encoder; repack the
        // BGRA8 contract into the saver's RGBA8 ImageBuffer.
        image::ImageBuffer img(width, height, image::PixelFormat::RGBA8);
        std::uint8_t* dst = img.Data();
        const std::size_t npix =
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < npix; ++i) {
            dst[i * 4u + 0] = bgra[i * 4u + 2];
            dst[i * 4u + 1] = bgra[i * 4u + 1];
            dst[i * 4u + 2] = bgra[i * 4u + 0];
            dst[i * 4u + 3] = bgra[i * 4u + 3];
        }
        if (image::ImageSaver().Save(img, Helper::Path::ToUtf8(path))) {
            return true;
        }
        // Encoder backend unavailable — fall through to the OIIO
        // residual below rather than failing a path that used to work.
    }
#if XER_VE_HAVE_OIIO
    return WriteImageOiio(path, bgra, width, height);
#else
    return false;
#endif
}

bool StillImageHandlesExtension(const std::string& ext) {
    static const std::unordered_set<std::string> kExts = {
        // image::DecoderFactory builtins (src/image/formats):
        "jpg", "jpeg", "jpe", "png", "gif", "webp",
        "tif", "tiff", "exr", "heic", "heif", "avif",
        "psd", "psb", "svg", "svgz",
        "cr2", "cr3", "nef", "dng", "arw", "raf",
        // OIIO residual (OiioResidualExtension):
        "dpx", "hdr", "pfm", "ora", "r3d", "raw",
    };
    return kExts.count(NormalizeExt(ext)) != 0;
}

}  // namespace XER::VideoEditor::Io
