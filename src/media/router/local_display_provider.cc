#include "media/router/local_display_provider.h"

#include <utility>

namespace XER::Media {
namespace {

class LocalDisplayRoute final : public MediaRoute {
public:
    LocalDisplayRoute(RouteId id, MediaSource source, MediaSink sink)
        : id_(id), source_(std::move(source)), sink_(std::move(sink)) {}

    RouteId id() const override { return id_; }
    const MediaSource& source() const override { return source_; }
    const MediaSink& sink() const override { return sink_; }
    bool SendMessage(const std::string& message, std::string*) override {
        last_message_ = message;
        return true;
    }
    void SetMessageHandler(std::function<void(std::string)> cb) override {
        handler_ = std::move(cb);
    }
    bool active() const override { return active_; }
    void Terminate() { active_ = false; }

private:
    RouteId id_;
    MediaSource source_;
    MediaSink sink_;
    std::string last_message_;
    std::function<void(std::string)> handler_;
    bool active_ = true;
};

}  // namespace

std::vector<MediaSink> LocalDisplayProvider::ListSinks(
    const MediaSource& /*source*/) {
    // The local secondary-display sink is always presented; the compositor
    // decides at route time whether a display surface exists.
    return {{"local-display-0", "This display", name()}};
}

std::unique_ptr<MediaRoute> LocalDisplayProvider::CreateRoute(
    const MediaSource& source, const MediaSink& sink, std::string* error) {
    if (sink.provider != name()) {
        if (error) *error = "sink not owned by local-display";
        return nullptr;
    }
    return std::make_unique<LocalDisplayRoute>(next_id_++, source, sink);
}

bool LocalDisplayProvider::TerminateRoute(RouteId, std::string*) {
    return true;
}

bool LocalDisplayProvider::RouteSendMessage(RouteId,
                                            const std::string&,
                                            std::string*) {
    return true;
}

}  // namespace XER::Media
