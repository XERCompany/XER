/**
 * AudioDecoder — the audio-side Decoder specialisation. Kept as a
 * separate header so audio-only consumers don't pull the video surface.
 */
#pragma once

#include "media/decoders/decoder.h"

namespace XER::Media {

// Aliases keep the design-doc names available directly.
using AudioDecoderBase = AudioDecoder;

}  // namespace XER::Media
