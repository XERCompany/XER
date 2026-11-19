#include "media/audio/audio_sync_writer.h"

#include <algorithm>
#include <cstring>

#include "cache/shared_memory_channel.h"

namespace XER::Media {

AudioSyncWriter::AudioSyncWriter(const AudioParameters& params)
    : params_(params) {}
AudioSyncWriter::~AudioSyncWriter() = default;

bool AudioSyncWriter::AttachChannel(const std::string& name,
                                    std::string* error_out) {
    channel_ = XER::Cache::SharedMemoryChannel::Create(
        name,
        static_cast<std::uint32_t>(params_.GetBytesPerBuffer()) * 4,
        error_out);
    return channel_ != nullptr;
}

bool AudioSyncWriter::Write(const std::uint8_t* src, std::size_t bytes) {
    if (!src || !bytes) return false;
    if (channel_)
        return channel_->Send(src, static_cast<std::uint32_t>(bytes), 0);
    if (!segment_.valid()) return false;
    const std::size_t n =
        std::min<std::size_t>(bytes, segment_.size());
    std::memcpy(segment_.data(), src, n);
    return true;
}

}  // namespace XER::Media
