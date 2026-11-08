#pragma once
#include "loader_base.h"

namespace XER::ML::Loaders {

/// Audio loader via XER::Audio::CodecIO.
/// WAV: decoded with WavFloatCodec::Decode32.
/// mp3/flac/ogg/aac/m4a: decoded via DecodeAudioBufferWithFfmpeg.
/// Returns a single-row Dataset with raw float32 PCM samples.
class LoaderAudio : public LoaderBase {
public:
    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "AudioLoader"; }
};

}  // namespace XER::ML::Loaders
