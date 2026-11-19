/**
 * LocalDisplayProvider — built-in MediaRouteProvider exposing the local
 * (wired/wireless secondary) display path. Routes render the source
 * through the local compositor — used for the 1-UA Presentation mode and
 * for embedding without a Cast/DIAL extension.
 */
#pragma once

#include "media/router/media_route_provider.h"

namespace XER::Media {

class LocalDisplayProvider final : public MediaRouteProvider {
public:
    std::string name() const override { return "local-display"; }

    std::vector<MediaSink> ListSinks(const MediaSource& source) override;
    std::unique_ptr<MediaRoute> CreateRoute(const MediaSource& source,
                                            const MediaSink& sink,
                                            std::string* error) override;
    bool TerminateRoute(RouteId route, std::string* error) override;
    bool RouteSendMessage(RouteId route, const std::string& message,
                          std::string* error) override;

private:
    std::uint64_t next_id_ = 1;
};

}  // namespace XER::Media
