/**
 * PresentationService — bridge between XER::Media::Router and the Desgin
 * UI (XER::UI) Presentation API implementations (design doc: the service
 * acts as a xorjocx bridge; embedders supply a PresentationServiceDelegate
 * that lives in content/ and delegates to this).
 */
#pragma once

#include <functional>
#include <string>

#include "media/router/media_route.h"
#include "media/router/media_router.h"

namespace XER::Media {

// Implemented by the embedder (Desgin UI side). The service forwards
// presentation availability/start/stop requests into the media router.
class PresentationServiceDelegate {
public:
    virtual ~PresentationServiceDelegate() = default;
    // Called when presentation availability for |url| changes.
    virtual void OnAvailabilityChanged(const std::string& url,
                                       bool available) = 0;
    // Called when a route for |url| delivered a message from the sink.
    virtual void OnRouteMessage(const std::string& url,
                                const std::string& message) = 0;
    // Called when an active presentation route terminated.
    virtual void OnRouteTerminated(const std::string& url) = 0;
};

class PresentationService {
public:
    static PresentationService& Instance();

    void SetDelegate(PresentationServiceDelegate* delegate);

    // Starts watching sink availability for |url| (mirrors the
    // Presentation API availability flow).
    void WatchAvailability(const std::string& url);
    void UnwatchAvailability(const std::string& url);

    // Requests a presentation route for |url| onto |sink|; returns the
    // route id (or kInvalidRouteId on failure).
    RouteId StartPresentation(const std::string& url, const MediaSink& sink,
                              std::string* error);
    bool SendMessage(const std::string& url, const std::string& message,
                     std::string* error);
    bool StopPresentation(const std::string& url, std::string* error);

private:
    struct Binding {
        std::string url;
        RouteId route = kInvalidRouteId;
    };

    PresentationServiceDelegate* delegate_ = nullptr;
    std::vector<Binding> bindings_;
};

}  // namespace XER::Media
