#include "media/router/media_router.h"

#include <algorithm>

namespace XER::Media {

MediaRouter& MediaRouter::Instance() {
    static MediaRouter instance;
    return instance;
}

void MediaRouter::RegisterSinksListener(const MediaSource& source,
                                        SinksCallback cb) {
    listeners_.push_back({source, std::move(cb)});
    NotifySinksChanged(source);
}

void MediaRouter::UnregisterSinksListener(const MediaSource& source) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [&source](const SinksListener& l) {
                                        return l.source == source;
                                    }),
                     listeners_.end());
}

RouteId MediaRouter::CreateRoute(const MediaSource& source,
                                 const MediaSink& sink, std::string* error) {
    auto* provider = FindProvider(sink.provider);
    if (!provider) {
        if (error) *error = "no route provider: " + sink.provider;
        return kInvalidRouteId;
    }
    pending_requests_.push_back(source);
    auto route = provider->CreateRoute(source, sink, error);
    pending_requests_.erase(std::remove(pending_requests_.begin(),
                                        pending_requests_.end(), source),
                            pending_requests_.end());
    if (!route) return kInvalidRouteId;
    const RouteId id = route->id() != kInvalidRouteId
                           ? route->id()
                           : next_route_id_++;
    routes_.push_back({std::move(route), provider});
    return id;
}

MediaRoute* MediaRouter::FindRoute(RouteId id) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [id](const RouteRecord& r) {
                               return r.route && r.route->id() == id;
                           });
    return it == routes_.end() ? nullptr : it->route.get();
}

bool MediaRouter::TerminateRoute(RouteId route, std::string* error) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [route](const RouteRecord& r) {
                               return r.route && r.route->id() == route;
                           });
    if (it == routes_.end()) {
        if (error) *error = "unknown route";
        return false;
    }
    bool ok = it->provider->TerminateRoute(route, error);
    routes_.erase(it);
    return ok;
}

bool MediaRouter::SendMessage(RouteId route, const std::string& message,
                              std::string* error) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [route](const RouteRecord& r) {
                               return r.route && r.route->id() == route;
                           });
    if (it == routes_.end()) {
        if (error) *error = "unknown route";
        return false;
    }
    return it->provider->RouteSendMessage(route, message, error);
}

void MediaRouter::AddProvider(std::unique_ptr<MediaRouteProvider> provider) {
    providers_.push_back(std::move(provider));
    for (auto& l : listeners_) NotifySinksChanged(l.source);
}

MediaRouteProvider* MediaRouter::FindProvider(const std::string& name) {
    for (auto& p : providers_)
        if (p->name() == name) return p.get();
    return nullptr;
}

std::vector<RouteId> MediaRouter::ActiveRoutes() const {
    std::vector<RouteId> ids;
    for (auto& r : routes_)
        if (r.route && r.route->active()) ids.push_back(r.route->id());
    return ids;
}

bool MediaRouter::HasPendingRequest(const MediaSource& source) const {
    return std::find(pending_requests_.begin(), pending_requests_.end(),
                     source) != pending_requests_.end();
}

void MediaRouter::NotifySinksChanged(const MediaSource& source) {
    std::vector<MediaSink> all;
    for (auto& p : providers_) {
        auto sinks = p->ListSinks(source);
        all.insert(all.end(), sinks.begin(), sinks.end());
    }
    for (auto& l : listeners_)
        if (l.source == source && l.cb) l.cb(all);
}

}  // namespace XER::Media
