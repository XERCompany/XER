#include "media/audio/shared_audio_segment.h"

namespace XER::Media {

bool SharedAudioSegment::Map(const std::string& name, std::uint32_t bytes,
                             bool, std::string* error_out) {
    if (name.empty() || bytes == 0) {
        if (error_out) *error_out = "invalid segment";
        return false;
    }
    data_.assign(bytes, 0);
    valid_ = true;
    return true;
}

void SharedAudioSegment::Unmap() {
    data_.clear();
    valid_ = false;
}

}  // namespace XER::Media
