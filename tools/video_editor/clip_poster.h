// Poster frame extraction for the VideoEditor core (XER Studio
// Phase 1B). Static and UI-free; all paths are UTF-8 std::strings.
// Pipeline per the Phase-1B spec: VideoDecoder seek to the requested
// clip position -> NextFrame -> FrameExporter::ToFile (PNG only —
// FrameExporter has no JPEG path; the spec's ".jpg" catalog name is
// illustrative). GetPosterAt (Phase 2, spec §5) is the in-memory
// variant: same seek/decode path, but returns the decoded video::Frame
// directly and falls back to the mid-stream frame when the requested
// time is out of range.
#pragma once

#include <string>

namespace video {
class Frame;  // src/video/frame.h — out-param only, forward-declared
}

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

    // In-memory poster at an arbitrary source time (Phase 2 spec §5).
    // Decodes the frame nearest `seconds` into `frame_out` (packed
    // BGRA8). Unlike Extract's clamp, an out-of-range `seconds`
    // (< 0 or beyond the probed duration) falls back to the
    // mid-stream frame — the catalog's default poster position.
    // The result is thumbnail-sized: frames wider/taller than 320 px
    // on the long edge are area-downscaled (aspect preserved) so the
    // byte-bounded PosterCache downstream can hold hundreds of them —
    // full-resolution posters made the cache thrash and the timeline
    // thumbnails blink. Returns false with error_out set for a null
    // frame_out, a missing file, an audio-only container, or when no
    // frame decodes. Stateless/static — callers own concurrency.
    static bool GetPosterAt(const std::string& utf8_source_path,
                            double seconds,
                            video::Frame* frame_out,
                            std::string* error_out = nullptr);
};

}  // namespace XER::VideoEditor
