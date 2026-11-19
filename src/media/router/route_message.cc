#include "media/router/route_message.h"

#include <cstring>

namespace XER::Media {
namespace {
void PutU32(std::vector<std::uint8_t>* out, std::uint32_t v) {
    out->push_back(static_cast<std::uint8_t>(v));
    out->push_back(static_cast<std::uint8_t>(v >> 8));
    out->push_back(static_cast<std::uint8_t>(v >> 16));
    out->push_back(static_cast<std::uint8_t>(v >> 24));
}
std::uint32_t GetU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
}  // namespace

void EncodeRouteMessage(const RouteMessage& msg,
                        std::vector<std::uint8_t>* out) {
    PutU32(out, static_cast<std::uint32_t>(msg.payload.size()) + 9);
    out->push_back(static_cast<std::uint8_t>(msg.origin));
    for (int i = 0; i < 8; ++i)
        out->push_back(static_cast<std::uint8_t>(msg.sequence >> (i * 8)));
    out->insert(out->end(), msg.payload.begin(), msg.payload.end());
}

std::int64_t DecodeRouteMessage(const std::vector<std::uint8_t>& buffer,
                                RouteMessage* out) {
    if (buffer.size() < 4) return 0;
    const std::uint32_t len = GetU32(buffer.data());
    if (len < 9 || len > 16u * 1024 * 1024) return -1;
    if (buffer.size() < 4 + len) return 0;
    const std::uint8_t* p = buffer.data() + 4;
    out->origin = static_cast<RouteMessage::Origin>(p[0]);
    std::uint64_t seq = 0;
    for (int i = 0; i < 8; ++i)
        seq |= static_cast<std::uint64_t>(p[1 + i]) << (i * 8);
    out->sequence = seq;
    out->payload.assign(reinterpret_cast<const char*>(p + 9), len - 9);
    return 4 + static_cast<std::int64_t>(len);
}

}  // namespace XER::Media
