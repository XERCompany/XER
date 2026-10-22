#include "packet.h"
#include <cstring>

namespace Lunca::Common {

// AudioPacket implementation
AudioPacket::AudioPacket() {}

AudioPacket::AudioPacket(const AudioPacketHeader& header)
    : header_(header) {}

void AudioPacket::SetHeader(const AudioPacketHeader& header) {
    header_ = header;
}

const AudioPacketHeader& AudioPacket::GetHeader() const {
    return header_;
}

void AudioPacket::SetPayload(const std::vector<uint8_t>& payload) {
    payload_ = payload;
}

const std::vector<uint8_t>& AudioPacket::GetPayload() const {
    return payload_;
}

std::vector<uint8_t> AudioPacket::Serialize() const {
    std::vector<uint8_t> data;
    data.reserve(HEADER_SIZE + payload_.size());

    // Serialize header
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header_);
    data.insert(data.end(), header_bytes, header_bytes + HEADER_SIZE);

    // Append payload
    data.insert(data.end(), payload_.begin(), payload_.end());

    return data;
}

bool AudioPacket::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < HEADER_SIZE) {
        return false;
    }

    // Deserialize header
    std::memcpy(&header_, data.data(), HEADER_SIZE);

    // Extract payload
    payload_.assign(data.begin() + HEADER_SIZE, data.end());

    return true;
}

size_t AudioPacket::GetTotalSize() const {
    return HEADER_SIZE + payload_.size();
}

bool AudioPacket::IsValid() const {
    return header_.stream_id != 0 && !payload_.empty();
}

} // namespace Lunca::Common
