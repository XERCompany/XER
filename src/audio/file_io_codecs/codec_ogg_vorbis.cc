#include "codec_ogg_vorbis.h"

#include "codec_ffmpeg_decode_helper.h"
#include "codec_ffmpeg_encode_helper.h"

namespace XER::Engine::Audio::CodecIO {

bool OggVorbisCodec::Encode(const float* input, size_t frames, std::vector<uint8_t>& out) const {
    if (input == nullptr || frames == 0) {
        return false;
    }
	std::string error;
	if (detail::EncodeMonoAudioBufferWithFfmpeg(input, frames, 44100, ".ogg", "libvorbis", &out, &error)) {
		return true;
	}
	if (detail::EncodeMonoAudioBufferWithFfmpeg(input, frames, 44100, ".ogg", "vorbis", &out, &error)) {
		return true;
	}
	out.clear();
	return false;
}

bool OggVorbisCodec::Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const {
    if (data == nullptr || bytes == 0) {
        return false;
    }
	std::string error;
	if (!detail::DecodeAudioBufferWithFfmpeg(data, bytes, ".ogg", &out, &error)) {
		out.clear();
		return false;
	}
	return true;
}

}  // namespace XER::Engine::Audio::CodecIO
