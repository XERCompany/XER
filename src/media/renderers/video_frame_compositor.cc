#include "media/renderers/video_frame_compositor.h"

#include <algorithm>
#include <utility>

namespace XER::Media {

void VideoFrameCompositor::AddFrame(DecodedFrame frame) {
    auto it = std::lower_bound(
        pending_.begin(), pending_.end(), frame.timestamp,
        [](const DecodedFrame& f, TimeTicks t) { return f.timestamp < t; });
    pending_.insert(it, std::move(frame));
}

const DecodedFrame* VideoFrameCompositor::CurrentFrame() {
    if (!time_source_ || pending_.empty()) return nullptr;
    const TimeTicks now = time_source_->CurrentMediaTime();
    // Latest frame whose timestamp is <= now; drop strictly stale frames.
    const DecodedFrame* best = nullptr;
    std::size_t drop = 0;
    for (const auto& f : pending_) {
        if (f.timestamp <= now) {
            best = &f;
            ++drop;
        } else {
            break;
        }
    }
    if (drop > 1) pending_.erase(pending_.begin(), pending_.begin() + drop - 1);
    return pending_.empty() ? nullptr : &pending_.front();
}

}  // namespace XER::Media
