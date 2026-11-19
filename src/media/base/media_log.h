/**
 * Scoped media logging facade. Routes through XER::Flux when available;
 * falls back to stderr. Media code logs via these macros so that
 * subsystems can be filtered by tag.
 */
#pragma once

#include <string>

namespace XER::Media {

enum class MediaLogLevel { kDebug, kInfo, kWarning, kError };

// Per-tag sink registration; nullptr restores the default stderr sink.
using MediaLogSink = void (*)(MediaLogLevel, const char* tag,
                              const std::string& message);
void SetMediaLogSink(MediaLogSink sink);
void MediaLog(MediaLogLevel level, const char* tag,
              const std::string& message);
bool MediaLogEnabled(MediaLogLevel level);

}  // namespace XER::Media

#define XER_MEDIA_LOG(level, tag, msg) \
    ::XER::Media::MediaLog(::XER::Media::MediaLogLevel::level, tag, msg)
#define XER_MEDIA_DVLOG(tag, msg) XER_MEDIA_LOG(kDebug, tag, msg)
#define XER_MEDIA_ILOG(tag, msg) XER_MEDIA_LOG(kInfo, tag, msg)
#define XER_MEDIA_WLOG(tag, msg) XER_MEDIA_LOG(kWarning, tag, msg)
#define XER_MEDIA_ELOG(tag, msg) XER_MEDIA_LOG(kError, tag, msg)
