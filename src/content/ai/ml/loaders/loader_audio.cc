#include "loader_audio.h"
#include "audio/file_io_codecs/codec_wav_float.h"
#include "audio/file_io_codecs/codec_ffmpeg_decode_helper.h"

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

namespace XER::ML::Loaders {

Dataset LoaderAudio::Load(const std::string& path) {
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("AudioLoader: cannot open " + path);

    std::vector<uint8_t> raw(
        (std::istreambuf_iterator<char>(f)),
         std::istreambuf_iterator<char>());

    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    std::vector<float> samples;
    if (ext == ".wav") {
        XER::Audio::CodecIO::WavFloatCodec codec;
        if (!codec.Decode32(raw.data(), raw.size(), samples))
            throw std::runtime_error("AudioLoader: WAV decode failed: " + path);
    } else {
        std::string err;
        if (!XER::Audio::CodecIO::detail::DecodeAudioBufferWithFfmpeg(
                raw.data(), raw.size(), ext, &samples, &err))
            throw std::runtime_error("AudioLoader: FFmpeg decode failed (" + ext + "): " + err);
    }

    Dataset ds{{{std::move(samples)}}, {}};
    StoreCachedDataset(Name(), path, ds);
    return ds;
}

}  // namespace XER::ML::Loaders
