/**
 * DecoderSelector — walks an ordered decoder list and picks the first
 * implementation that reports CanDecode + successful Initialize
 * (design doc: "the first one which reports success will be used,
 * typically the hardware decoder for video").
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/decoders/decoder.h"

namespace XER::Media {

// Returns the first decoder initialized for |stream|, or nullptr when none
// accepted. Errors from each attempt are appended to |log| (may be null).
template <typename DecoderT>
DecoderT* SelectDecoder(std::vector<std::unique_ptr<DecoderT>>& candidates,
                        const DemuxerStream& stream, std::string* log) {
    for (auto& d : candidates) {
        if (!d->CanDecode(stream.codec())) continue;
        std::string err;
        if (d->Initialize(stream, &err)) return d.get();
        if (log) {
            *log += "decoder init failed: " + err + "\n";
        }
    }
    return nullptr;
}

}  // namespace XER::Media
