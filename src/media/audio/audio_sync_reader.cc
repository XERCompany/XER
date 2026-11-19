#include "media/audio/audio_sync_reader.h"

#include <cstring>
#include <thread>

#include "cache/shared_memory_channel.h"

namespace XER::Media {

AudioSyncReader::AudioSyncReader(const AudioParameters& params)
    : params_(params) {}

AudioSyncReader::~AudioSyncReader() = default;

bool AudioSyncReader::Map(const std::string& name,
                          std::string* error_out) {
    return segment_.Map(
        name, static_cast<std::uint32_t>(params_.GetBytesPerBuffer()),
        false, error_out);
}

bool AudioSyncReader::AttachChannel(const std::string& name,
                                    std::string* error_out) {
    channel_ = XER::Cache::SharedMemoryChannel::Open(name, error_out);
    return channel_ != nullptr;
}

int AudioSyncReader::Read(std::uint8_t* dest, int timeout_ms) {
    if (error_.load()) return -1;
    if (channel_) {
        std::vector<std::uint8_t> msg;
        if (!channel_->Recv(&msg, static_cast<std::uint32_t>(timeout_ms)))
            return 0;
        const std::size_t n =
            msg.size() < static_cast<std::size_t>(
                             params_.GetBytesPerBuffer())
                ? msg.size()
                : static_cast<std::size_t>(params_.GetBytesPerBuffer());
        std::memcpy(dest, msg.data(), n);
        return params_.frames_per_buffer;
    }
    if (!segment_.valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 0;
    }
    std::memcpy(dest, segment_.data(), segment_.size());
    return params_.frames_per_buffer;
}

void AudioSyncReader::SignalError() { error_ = true; }

}  // namespace XER::Media
