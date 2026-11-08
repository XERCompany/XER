#include "codec_mp3_lame.h"

#include "codec_ffmpeg_decode_helper.h"
#include "codec_ffmpeg_encode_helper.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace XER::Audio::CodecIO {

bool Mp3LameCodec::Encode(const float* input, size_t frames, std::vector<uint8_t>& out) const {
	return EncodeInterleaved(input, frames, 44100, 1, out);
}

bool Mp3LameCodec::EncodeInterleaved(const float* input, size_t frames, int sample_rate, int channels, std::vector<uint8_t>& out) const {
    if (input == nullptr || frames == 0) {
        return false;
    }
	if (sample_rate <= 0 || (channels != 1 && channels != 2)) {
		out.clear();
		return false;
	}

#if ENGINE_CODEC_HAS_LAME_HEADERS
    lame_t lame = lame_init();
    if (lame == nullptr) {
        return false;
    }
	lame_set_in_samplerate(lame, sample_rate);
	lame_set_num_channels(lame, channels);
    lame_set_quality(lame, 2);
    if (lame_init_params(lame) < 0) {
        lame_close(lame);
        return false;
    }

	std::vector<short> pcm(frames * static_cast<size_t>(channels));
	for (size_t i = 0; i < pcm.size(); ++i) {
        const float clamped = std::clamp(input[i], -1.0f, 1.0f);
        pcm[i] = static_cast<short>(clamped * 32767.0f);
    }

    const int mp3_capacity = static_cast<int>(1.25 * frames + 7200);
    out.assign(static_cast<std::size_t>(mp3_capacity), 0);
	const int encoded = channels == 1
		? lame_encode_buffer(
			lame,
			pcm.data(),
			pcm.data(),
			static_cast<int>(frames),
			out.data(),
			mp3_capacity)
		: lame_encode_buffer_interleaved(
			lame,
			pcm.data(),
			static_cast<int>(frames),
			out.data(),
			mp3_capacity);
    if (encoded < 0) {
        lame_close(lame);
        out.clear();
        return false;
    }
    const int flushed = lame_encode_flush(lame, out.data() + encoded, mp3_capacity - encoded);
    lame_close(lame);
    if (flushed < 0) {
        out.clear();
        return false;
    }
    out.resize(static_cast<std::size_t>(encoded + flushed));
    return !out.empty();
#else
    std::string error;
    if (detail::EncodeInterleavedAudioBufferWithFfmpeg(input, frames, sample_rate, channels, ".mp3", "libmp3lame", &out, &error)) {
        return true;
    }
    if (detail::EncodeInterleavedAudioBufferWithFfmpeg(input, frames, sample_rate, channels, ".mp3", "mp3", &out, &error)) {
        return true;
    }
    if (detail::EncodeInterleavedAudioBufferWithFfmpeg(input, frames, sample_rate, channels, ".mp3", "mp3_mf", &out, &error)) {
        return true;
    }
    out.clear();
    return false;
#endif
}

bool Mp3LameCodec::Decode(const uint8_t* data, size_t bytes, std::vector<float>& out) const {
    if (data == nullptr || bytes == 0) {
        return false;
    }

    std::string error;
    if (!detail::DecodeAudioBufferWithFfmpeg(data, bytes, ".mp3", &out, &error)) {
        out.clear();
        return false;
    }
    return true;
}

}  // namespace XER::Audio::CodecIO
