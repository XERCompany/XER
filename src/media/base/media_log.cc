#include "media/base/media_log.h"

#include <cstdio>
#include <utility>

#include "flux/core/logger.h"

namespace XER::Media {
namespace {
MediaLogSink g_sink = nullptr;
MediaLogLevel g_min_level = MediaLogLevel::kDebug;

// Routes through the real XER::Flux logger when no custom sink is set.
XER::Flux::Core::Logger& FluxLogger() {
    static XER::Flux::Core::FluxConfig cfg = [] {
        auto c = XER::Flux::Core::FluxConfigFromEnvironment();
        c.application_name = "xer-media";
        c.timestamps = false;
        return c;
    }();
    static XER::Flux::Core::Logger logger(cfg);
    return logger;
}
}  // namespace

void SetMediaLogSink(MediaLogSink sink) { g_sink = sink; }

bool MediaLogEnabled(MediaLogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(g_min_level);
}

void MediaLog(MediaLogLevel level, const char* tag,
              const std::string& message) {
    if (!MediaLogEnabled(level)) return;
    if (g_sink) {
        g_sink(level, tag, message);
        return;
    }
    using F = XER::Flux::Core::LogLevel;
    static const F kMap[] = {F::kDebug, F::kInfo, F::kWarning, F::kError};
    FluxLogger().Log(kMap[static_cast<int>(level)], tag, message);
}

}  // namespace XER::Media
