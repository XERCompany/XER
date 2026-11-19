/**
 * UrlDataSource — DataSource over HTTP range requests backed by
 * XER::Network::Http::SyncRequest (src/content/network, YA.Fetch).
 * ReadAt() issues "Range: bytes=offset-offset+size-1"; a HEAD request
 * resolves size(); when the server ignores ranges or Content-Length is
 * absent the source reports streaming().
 */
#pragma once

#include <memory>
#include <string>

#include "media/demuxer/data_source.h"

namespace XER::Media {

class UrlDataSource final : public DataSource {
public:
    explicit UrlDataSource(std::string url) : url_(std::move(url)) {}

    static std::unique_ptr<UrlDataSource> Open(const std::string& url,
                                               std::string* error_out);

    Status ReadAt(std::uint64_t offset, std::uint8_t* out,
                  std::size_t size, std::size_t* read_out) override;
    std::int64_t size() const override { return size_; }
    bool streaming() const override { return size_ < 0; }

    const std::string& url() const { return url_; }

private:
    bool Probe(std::string* error_out);

    std::string url_;
    std::int64_t size_ = -1;
    bool probed_ = false;
    long timeout_ms_ = 30000;
};

}  // namespace XER::Media
