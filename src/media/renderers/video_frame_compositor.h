/**
 * VideoFrameCompositor — orders decoded video frames by timestamp and
 * picks the frame due at the TimeSource clock on each sink callback
 * (design doc).
 */
#pragma once

#include <vector>

#include "media/decoders/decoder.h"
#include "media/renderers/time_source.h"

namespace XER::Media {

class VideoFrameCompositor {
public:
    void SetTimeSource(TimeSource* ts) { time_source_ = ts; }
    void AddFrame(DecodedFrame frame);
    // Returns the frame due at the current media time, or nullptr.
    const DecodedFrame* CurrentFrame();
    void Flush() { pending_.clear(); }
    std::size_t pending() const { return pending_.size(); }

private:
    TimeSource* time_source_ = nullptr;
    std::vector<DecodedFrame> pending_;  // sorted by timestamp
};

}  // namespace XER::Media
