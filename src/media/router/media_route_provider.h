/**
 * MediaRouteProvider — implemented by route providers (Cast, DIAL, local
 * display...). The Router forwards availability queries and route
 * create/control to the provider that owns the selected sink.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/router/media_route.h"

namespace XER::Media {

class MediaRouteProvider {
public:
    virtual ~MediaRouteProvider() = default;
    virtual std::string name() const = 0;
    virtual std::vector<MediaSink> ListSinks(const MediaSource& source) = 0;
    virtual std::unique_ptr<MediaRoute> CreateRoute(const MediaSource& source,
                                                    const MediaSink& sink,
                                                    std::string* error) = 0;
    virtual bool TerminateRoute(RouteId route, std::string* error) = 0;
    virtual bool RouteSendMessage(RouteId route, const std::string& message,
                                  std::string* error) = 0;
};

}  // namespace XER::Media
