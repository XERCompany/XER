#include "media/renderers/paint_canvas_video_renderer.h"

namespace XER::Media {

void PaintCanvasVideoRenderer::Paint(const DecodedFrame& frame) const {
    if (!cb_ || frame.pixels.empty() || frame.width <= 0) return;
    cb_(frame.pixels.data(), frame.width, frame.height, frame.timestamp);
}

}  // namespace XER::Media
