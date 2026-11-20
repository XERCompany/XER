// oiio_image.cc — see oiio_image.h for the contract. The OIIO path is
// compiled only when the OpenImageIO headers are installed (pkg-config
// OpenImageIO); without them every entry point degrades to failure so
// callers keep their WIC/FFmpeg fallbacks. OiioHandlesExtension is a
// static list and answers either way.
#include "oiio_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#if __has_include(<OpenImageIO/imageio.h>)
#define XER_VE_HAVE_OIIO 1
#include <OpenImageIO/imageio.h>
#else
#define XER_VE_HAVE_OIIO 0
#endif

#if XER_VE_HAVE_OIIO
#include "helper/path_utils.h"
#endif

#if XER_VE_HAVE_OIIO && defined(_WIN32) && \
    defined(XER_VE_OIIO_RUNTIME_DLLS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace XER::VideoEditor::Io {

#if XER_VE_HAVE_OIIO
namespace {

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

}  // namespace
#endif  // XER_VE_HAVE_OIIO

Image ReadImage(const std::filesystem::path& path) {
#if XER_VE_HAVE_OIIO
    if (!EnsureOiioRuntime()) return {};  // absent runtime
    // OIIO filename params are const std::string& / string_view /
    // fs::path depending on the release — a UTF-8 std::string converts
    // to all of them, and OIIO expects UTF-8 bytes anyway.
    const std::string filename = Helper::Path::ToUtf8(path);
    auto in = OIIO::ImageInput::open(filename);
    if (!in) {
        return {};
    }
    const OIIO::ImageSpec& spec = in->spec();
    const int file_channels = spec.nchannels;
    if (spec.width <= 0 || spec.height <= 0 || file_channels < 1) {
        in->close();
        return {};
    }

    // Always read as float — OIIO converts from the native format —
    // and only pull the first 4 channels (EXRs can carry extra layers).
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
#else
    (void)path;
    return {};
#endif
}

bool WriteImage(const std::filesystem::path& path,
                const std::uint8_t* bgra, int width, int height) {
#if XER_VE_HAVE_OIIO
    if (!EnsureOiioRuntime()) return false;  // absent runtime
    if (bgra == nullptr || width <= 0 || height <= 0) {
        return false;
    }
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
#else
    (void)path;
    (void)bgra;
    (void)width;
    (void)height;
    return false;
#endif
}

bool OiioHandlesExtension(const std::string& ext) {
    static const std::unordered_set<std::string> kExts = {
        "exr", "dpx", "tif",  "tiff", "raw", "cr2", "nef",
        "arw", "dng", "psd",  "hdr",  "r3d", "ora", "pfm",
    };
    // Normalize: callers pass either "exr" or the path.extension()
    // form ".exr" — strip a leading dot and lowercase so BOTH resolve.
    // (The un-normalized lookup silently rejected every dot-prefixed
    // extension, which made the entire OIIO still-image path dead
    // code — bug-audit finding.)
    std::string key = ext;
    if (!key.empty() && key.front() == '.') {
        key.erase(key.begin());
    }
    for (char& c : key) {
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return kExts.count(key) != 0;
}

}  // namespace XER::VideoEditor::Io
