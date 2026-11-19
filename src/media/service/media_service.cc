#include "media/service/media_service.h"

namespace XER::Media {

MediaService::MediaService(MediaServiceConfig config)
    : config_(config) {}

MediaService& MediaService::Instance() {
    static MediaService service;
    return service;
}

}  // namespace XER::Media
