/**
 * XER::Media::Router (design doc: media routing / Presentation API).
 *
 * Matches media sources (URIs) with sinks capable of rendering them. A
 * linked pair becomes a MediaRoute carrying two-way string messaging.
 * The Router keeps bookkeeping of routes, pending route requests and sink
 * availability; actual sink I/O is delegated to MediaRouteProviders
 * (e.g. Cast/DIAL via the router component extension).
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media/router/media_route.h"
#include "media/router/media_route_provider.h"

namespace XER::Media {

class MediaRouter {
public:
    static MediaRouter& Instance();

    // Sink availability.
    using SinksCallback = std::function<void(std::vector<MediaSink>)>;
    // Registers for notifications when sinks for |source| change.
    void RegisterSinksListener(const MediaSource& source, SinksCallback cb);
    void UnregisterSinksListener(const MediaSource& source);

    // Route lifecycle. The router keeps ownership of created routes in its
    // bookkeeping (per the design doc); callers interact through the id.
    RouteId CreateRoute(const MediaSource& source, const MediaSink& sink,
                        std::string* error);
    bool TerminateRoute(RouteId route, std::string* error);
    bool SendMessage(RouteId route, const std::string& message,
                     std::string* error);

    MediaRoute* FindRoute(RouteId id);

    // Providers.
    void AddProvider(std::unique_ptr<MediaRouteProvider> provider);
    MediaRouteProvider* FindProvider(const std::string& name);

    // Bookkeeping.
    std::vector<RouteId> ActiveRoutes() const;
    bool HasPendingRequest(const MediaSource& source) const;

private:
    struct RouteRecord {
        std::unique_ptr<MediaRoute> route;
        MediaRouteProvider* provider;
    };
    struct SinksListener {
        MediaSource source;
        SinksCallback cb;
    };

    void NotifySinksChanged(const MediaSource& source);

    std::vector<std::unique_ptr<MediaRouteProvider>> providers_;
    std::vector<RouteRecord> routes_;
    std::vector<SinksListener> listeners_;
    std::vector<MediaSource> pending_requests_;
    RouteId next_route_id_ = 1;
};

}  // namespace XER::Media
