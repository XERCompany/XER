#include "codec_flac_decoder.h"

#include "codec_ffmpeg_decode_helper.h"

#include <cstring>

namespace XER::Engine::Audio::CodecIO {

bool FlacDecoder::Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const {
    if (data == nullptr || bytes == 0) {
        return false;
    }
    std::string error;
    if (!detail::DecodeAudioBufferWithFfmpeg(data, bytes, ".flac", &out, &error)) {
        out.clear();
        return false;
    }
    return true;
}

}  // namespace XER::Engine::Audio::CodecIO
