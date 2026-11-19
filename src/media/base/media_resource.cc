#include "media/base/media_resource.h"

namespace XER::Media {

std::shared_ptr<MediaResource> MediaResource::Allocate(std::size_t bytes) {
    auto r = std::make_shared<MediaResource>();
    r->buf_.resize(bytes);
    return r;
}

std::shared_ptr<MediaResource> MediaResourcePool::Acquire(std::size_t bytes) {
    {
        std::lock_guard lock(mu_);
        if (!pool_.empty()) {
            auto r = std::move(pool_.back());
            pool_.pop_back();
            if (r->size() >= bytes) return r;
        }
    }
    return MediaResource::Allocate(bytes);
}

void MediaResourcePool::Release(std::shared_ptr<MediaResource> res) {
    if (!res || res.use_count() > 1) return;
    std::lock_guard lock(mu_);
    if (pool_.size() < max_pooled_) pool_.push_back(std::move(res));
}

}  // namespace XER::Media
