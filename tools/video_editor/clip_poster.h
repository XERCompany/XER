// Poster frame extraction for the VideoEditor core (XER Studio
// Phase 1B). Static and UI-free; all paths are UTF-8 std::strings.
// Pipeline per the Phase-1B spec: VideoDecoder seek to the requested
// clip position -> NextFrame -> FrameExporter::ToFile (PNG only —
// FrameExporter has no JPEG path; the spec's ".jpg" catalog name is
// illustrative).
#pragma once

#include <string>

namespace XER::VideoEditor {

// Writes a PNG poster for a media file. `seek_seconds` is clamped to
// [0, duration]; the decoder seeks keyframe-backward then decodes
// forward (bounded) to the first frame at/after the target — falls
// back to the last decoded frame at EOF. Returns false with
// error_out set for: audio-only files (GetWidth/Height == 0), decode
// failure, or export failure. UTF-8 paths; parent dirs of
// utf8_out_png_path are created. Stateless/static — callers own
// concurrency.
class ClipPoster {
public:
    static bool Extract(const std::string& utf8_source_path,
                        double seek_seconds,
                        const std::string& utf8_out_png_path,
                        std::string* error_out = nullptr);
};

}  // namespace XER::VideoEditor
