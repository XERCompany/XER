#include "media/router/presentation_service.h"

#include <algorithm>

namespace XER::Media {

PresentationService& PresentationService::Instance() {
    static PresentationService instance;
    return instance;
}

void PresentationService::SetDelegate(PresentationServiceDelegate* d) {
    delegate_ = d;
}

void PresentationService::WatchAvailability(const std::string& url) {
    MediaSource src{url};
    MediaRouter::Instance().RegisterSinksListener(
        src, [this, url](std::vector<MediaSink> sinks) {
            if (delegate_)
                delegate_->OnAvailabilityChanged(url, !sinks.empty());
        });
}

void PresentationService::UnwatchAvailability(const std::string& url) {
    MediaRouter::Instance().UnregisterSinksListener(MediaSource{url});
}

RouteId PresentationService::StartPresentation(const std::string& url,
                                               const MediaSink& sink,
                                               std::string* error) {
    const RouteId id = MediaRouter::Instance().CreateRoute(
        MediaSource{url}, sink, error);
    if (id == kInvalidRouteId) return id;
    if (auto* route = MediaRouter::Instance().FindRoute(id)) {
        route->SetMessageHandler([this, url](std::string m) {
            if (delegate_) delegate_->OnRouteMessage(url, m);
        });
    }
    bindings_.push_back({url, id});
    return id;
}

bool PresentationService::SendMessage(const std::string& url,
                                      const std::string& message,
                                      std::string* error) {
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [&](const Binding& b) { return b.url == url; });
    if (it == bindings_.end()) {
        if (error) *error = "no presentation for url";
        return false;
    }
    return MediaRouter::Instance().SendMessage(it->route, message, error);
}

bool PresentationService::StopPresentation(const std::string& url,
                                           std::string* error) {
    auto it = std::find_if(bindings_.begin(), bindings_.end(),
                           [&](const Binding& b) { return b.url == url; });
    if (it == bindings_.end()) return true;
    const bool ok =
        MediaRouter::Instance().TerminateRoute(it->route, error);
    bindings_.erase(it);
    if (ok && delegate_) delegate_->OnRouteTerminated(url);
    return ok;
}

}  // namespace XER::Media
