#include "media/media.h"

namespace XER::Media {

const char* MediaVersion() { return "XER::Media 1.0"; }

std::string CodecName(CodecId codec) {
    switch (codec) {
        case CodecId::kAV1:  return "av1";
        case CodecId::kH264: return "h264";
        case CodecId::kH265: return "h265";
        case CodecId::kVP8:  return "vp8";
        case CodecId::kVP9:  return "vp9";
        case CodecId::kOpus: return "opus";
        case CodecId::kAAC:  return "aac";
        default:             return "unknown";
    }
}

CodecId CodecFromName(const std::string& name) {
    if (name == "av1" || name == "av01") return CodecId::kAV1;
    if (name == "h264" || name == "avc1" || name == "avc") return CodecId::kH264;
    if (name == "h265" || name == "hevc" || name == "hvc1") return CodecId::kH265;
    if (name == "vp8") return CodecId::kVP8;
    if (name == "vp9") return CodecId::kVP9;
    if (name == "opus") return CodecId::kOpus;
    if (name == "aac" || name == "mp4a") return CodecId::kAAC;
    return CodecId::kUnknown;
}

}  // namespace XER::Media
