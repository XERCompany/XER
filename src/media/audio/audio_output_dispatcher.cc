#include "media/audio/audio_output_dispatcher.h"

#include <cstdio>

namespace XER::Media {
namespace {
std::string ParamsKey(const AudioParameters& p) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d/%d/%d/%d", p.sample_rate,
                  static_cast<int>(p.channel_layout),
                  static_cast<int>(p.format), p.frames_per_buffer);
    return buf;
}
}  // namespace

AudioOutputStream* AudioOutputDispatcher::Acquire(
    const AudioParameters& params, std::string* error_out) {
    const auto key = ParamsKey(params);
    auto it = streams_.find(key);
    if (it != streams_.end()) {
        ++it->second.refs;
        return it->second.stream.get();
    }
    auto stream = factory_(params, error_out);
    if (!stream) return nullptr;
    auto* raw = stream.get();
    streams_[key] = {std::move(stream), 1};
    return raw;
}

void AudioOutputDispatcher::Release(const AudioParameters& params) {
    auto it = streams_.find(ParamsKey(params));
    if (it == streams_.end()) return;
    if (--it->second.refs <= 0) {
        it->second.stream->Stop();
        it->second.stream->Close();
        streams_.erase(it);
    }
}

}  // namespace XER::Media
