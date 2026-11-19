/**
 * SharedAudioSegment — shared-memory segment for moving PCM between
 * renderer and audio service (design doc). Currently an in-process
 * buffer; the Cache::SharedMemoryChannel backend plugs in when linked.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Media {

class SharedAudioSegment {
public:
    SharedAudioSegment() = default;

    // Creates/attaches a named segment of |bytes| (|create| = creator).
    bool Map(const std::string& name, std::uint32_t bytes, bool create,
             std::string* error_out);
    void Unmap();

    std::uint8_t* data() { return data_.data(); }
    const std::uint8_t* data() const { return data_.data(); }
    std::uint32_t size() const {
        return static_cast<std::uint32_t>(data_.size());
    }
    bool valid() const { return valid_; }

private:
    std::vector<std::uint8_t> data_;
    bool valid_ = false;
};

}  // namespace XER::Media
