#include "magic_number_detector.h"
#include "helper/path_utils.h"
#include <fstream>
#include <cstring>

namespace image {

std::string MagicNumberDetector::Detect(const uint8_t* h, std::size_t len) {
    if (!h || len < 4) return "";

    // JPEG: FF D8 FF
    if (len >= 3 && h[0]==0xFF && h[1]==0xD8 && h[2]==0xFF) return "jpeg";
    // PNG: 89 50 4E 47
    if (len >= 4 && h[0]==0x89 && h[1]=='P' && h[2]=='N' && h[3]=='G') return "png";
    // GIF87a / GIF89a
    if (len >= 6 && h[0]=='G' && h[1]=='I' && h[2]=='F') return "gif";
    // RIFF/WEBP
    if (len >= 12 && h[0]=='R' && h[1]=='I' && h[2]=='F' && h[3]=='F' &&
        h[8]=='W' && h[9]=='E' && h[10]=='B' && h[11]=='P') return "webp";
    // BMP: BM
    if (len >= 2 && h[0]=='B' && h[1]=='M') return "bmp";
    // TIFF: 49 49 / 4D 4D
    if (len >= 4 && ((h[0]==0x49 && h[1]==0x49 && h[2]==0x2A && h[3]==0x00) ||
                     (h[0]==0x4D && h[1]==0x4D && h[2]==0x00 && h[3]==0x2A))) return "tiff";
    // OpenEXR: 76 2F 31 01
    if (len >= 4 && h[0]==0x76 && h[1]==0x2F && h[2]==0x31 && h[3]==0x01) return "exr";
    // PSD: 38 42 50 53
    if (len >= 4 && h[0]=='8' && h[1]=='B' && h[2]=='P' && h[3]=='S') return "psd";
    // HEIF / HEIC: ftyp box at offset 4
    if (len >= 12 && h[4]=='f' && h[5]=='t' && h[6]=='y' && h[7]=='p') return "heif";
    // SVG: starts with '<'
    if (len >= 5 && h[0]=='<') return "svg";
    // Canon CR2 (TIFF with sub-type)
    if (len >= 4 && h[0]==0x49 && h[1]==0x49 && h[2]==0x2A && h[3]==0x00) return "raw";

    return "";
}

std::string MagicNumberDetector::DetectFile(const std::string& path) {
    const auto fs_path = XER::Helper::Path::FromUtf8(
        XER::Helper::Path::NormalizeUtf8Path(path));
    std::ifstream f(fs_path, std::ios::binary);
    if (!f) return "";
    uint8_t buf[16] = {};
    f.read(reinterpret_cast<char*>(buf), sizeof(buf));
    return Detect(buf, static_cast<std::size_t>(f.gcount()));
}

}  // namespace image
