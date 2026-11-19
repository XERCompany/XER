/**
 * PaintCanvasVideoRenderer — draws DecodedFrame video onto a canvas
 * target (design doc: the video renderer's last stage paints through
 * Skia when a canvas is bound). Frame delivery is callback-based so the
 * sink can raster to Skia, D2D, or a test framebuffer without linking
 * against graphics libraries here.
 */
#pragma once

#include <functional>

#include "media/decoders/decoder.h"

namespace XER::Media {

class PaintCanvasVideoRenderer {
public:
    // |rgba| is tightly packed BGRA8, |width|x|height|.
    using PaintCallback =
        std::function<void(const std::uint8_t* rgba, int width,
                           int height, TimeTicks timestamp)>;

    void SetPaintCallback(PaintCallback cb) { cb_ = std::move(cb); }

    // Paints |frame| if a callback is bound; no-op otherwise.
    void Paint(const DecodedFrame& frame) const;

private:
    PaintCallback cb_;
};

}  // namespace XER::Media
