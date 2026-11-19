#include "media/muxers/container_codecs.h"

namespace XER::Media {

bool ContainerSupportsCodec(Container container, CodecId codec) {
    switch (container) {
        case Container::kWebm:
            return codec == CodecId::kVP8 || codec == CodecId::kVP9 ||
                   codec == CodecId::kAV1 || codec == CodecId::kH264 ||
                   codec == CodecId::kOpus;
        case Container::kMkv:
            return codec == CodecId::kVP8 || codec == CodecId::kVP9 ||
                   codec == CodecId::kAV1 || codec == CodecId::kH264 ||
                   codec == CodecId::kH265 || codec == CodecId::kOpus;
        case Container::kMp4:
            return codec == CodecId::kVP9 || codec == CodecId::kAV1 ||
                   codec == CodecId::kH264 || codec == CodecId::kH265 ||
                   codec == CodecId::kOpus || codec == CodecId::kAAC;
    }
    return false;
}

std::vector<CodecId> ContainerCodecs(Container container, StreamType type) {
    std::vector<CodecId> out;
    for (CodecId c : {CodecId::kVP8, CodecId::kVP9, CodecId::kAV1,
                      CodecId::kH264, CodecId::kH265, CodecId::kOpus,
                      CodecId::kAAC}) {
        const bool is_audio = c == CodecId::kOpus || c == CodecId::kAAC;
        if (is_audio != (type == StreamType::kAudio)) continue;
        if (ContainerSupportsCodec(container, c)) out.push_back(c);
    }
    return out;
}

const char* ContainerName(Container container) {
    switch (container) {
        case Container::kWebm: return "webm";
        case Container::kMkv: return "mkv";
        case Container::kMp4: return "mp4";
    }
    return "?";
}

const char* ContainerExtension(Container container) {
    switch (container) {
        case Container::kWebm: return ".webm";
        case Container::kMkv: return ".mkv";
        case Container::kMp4: return ".mp4";
    }
    return "";
}

}  // namespace XER::Media
