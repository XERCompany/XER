/**
 * RouteMessage — framing for the two-way string channel on a MediaRoute.
 * Messages carry an origin tag (source/sink), a monotonically increasing
 * sequence and a text payload. Framing is length-prefixed UTF-8.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Media {

struct RouteMessage {
    enum class Origin : std::uint8_t { kSource, kSink };
    Origin origin = Origin::kSource;
    std::uint64_t sequence = 0;
    std::string payload;
};

// Appends a length-prefixed frame to |out|.
void EncodeRouteMessage(const RouteMessage& msg,
                        std::vector<std::uint8_t>* out);
// Decodes one frame from the front of |buffer|; returns bytes consumed
// (0 when incomplete, -1 on malformed input).
std::int64_t DecodeRouteMessage(const std::vector<std::uint8_t>& buffer,
                                RouteMessage* out);

}  // namespace XER::Media
