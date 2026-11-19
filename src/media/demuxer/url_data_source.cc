#include "media/demuxer/url_data_source.h"

#include <cstdlib>
#include <cstring>

#include "content/network/http/http_sync_request.h"

namespace XER::Media {

std::unique_ptr<UrlDataSource> UrlDataSource::Open(const std::string& url,
                                                   std::string* error_out) {
    if (url.empty()) {
        if (error_out) *error_out = "empty url";
        return nullptr;
    }
    auto ds = std::unique_ptr<UrlDataSource>(new UrlDataSource(url));
    if (!ds->Probe(error_out)) return nullptr;
    return ds;
}

bool UrlDataSource::Probe(std::string* error_out) {
    namespace H = XER::Network::Http;
    // Try a HEAD for Content-Length; fall back to a 1-byte range GET
    // when HEAD is unsupported.
    auto head = H::SyncRequest("HEAD", url_, {}, {}, timeout_ms_);
    if (!head.transport_ok) {
        if (error_out)
            *error_out = head.error.empty() ? "HEAD request failed"
                                            : head.error;
        return false;
    }
    auto len_it = head.headers.find("content-length");
    if (len_it != head.headers.end())
        size_ = std::strtoll(len_it->second.c_str(), nullptr, 10);
    probed_ = true;
    return head.status >= 200 && head.status < 400;
}

DataSource::Status UrlDataSource::ReadAt(std::uint64_t offset,
                                         std::uint8_t* out,
                                         std::size_t size,
                                         std::size_t* read_out) {
    namespace H = XER::Network::Http;
    if (read_out) *read_out = 0;
    if (size_ >= 0 &&
        offset >= static_cast<std::uint64_t>(size_))
        return Status::kEos;

    const std::string range =
        "Range: bytes=" + std::to_string(offset) + "-" +
        std::to_string(offset + size - 1);
    auto resp = H::SyncRequest("GET", url_, {}, {range}, timeout_ms_);
    if (!resp.transport_ok || resp.status >= 400)
        return Status::kError;
    // 206 = ranged, 200 = whole-body (server ignored Range — clip the
    // prefix ourselves when the offset is in range).
    const std::string& body = resp.body;
    const char* src = nullptr;
    std::size_t avail = 0;
    if (resp.status == 206) {
        src = body.data();
        avail = body.size();
    } else if (resp.status == 200 && body.size() > offset) {
        src = body.data() + offset;
        avail = body.size() - offset;
    } else {
        return Status::kEos;
    }
    const std::size_t n = avail < size ? avail : size;
    std::memcpy(out, src, n);
    if (read_out) *read_out = n;
    return n == 0 ? Status::kEos : Status::kOk;
}

}  // namespace XER::Media
