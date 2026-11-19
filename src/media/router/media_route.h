/**
 * MediaSource / MediaSink / MediaRoute — value types and the two-way
 * messaging channel created when a source is linked to a sink
 * (design doc: media routing).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace XER::Media {

struct MediaSource {
    std::string uri;
    bool operator==(const MediaSource& o) const { return uri == o.uri; }
};

struct MediaSink {
    std::string id;        // stable provider-assigned id
    std::string name;      // user-facing label
    std::string provider;  // owning MediaRouteProvider name
};

using RouteId = std::uint64_t;
constexpr RouteId kInvalidRouteId = 0;

class MediaRoute {
public:
    virtual ~MediaRoute() = default;
    virtual RouteId id() const = 0;
    virtual const MediaSource& source() const = 0;
    virtual const MediaSink& sink() const = 0;
    // Two-way messaging between source and sink.
    virtual bool SendMessage(const std::string& message, std::string* error) = 0;
    virtual void SetMessageHandler(std::function<void(std::string)> cb) = 0;
    virtual bool active() const = 0;
};

}  // namespace XER::Media
