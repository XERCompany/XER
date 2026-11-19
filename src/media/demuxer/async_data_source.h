/**
 * AsyncDataSource — DataSource backed by XER::AsyncIO::AsyncFileReader
 * (src/async_io). Sync ReadAt() is implemented via ReadAsyncAt + a
 * latch; ReadAtAsync() exposes the native async path for demuxer
 * read-ahead (multiple range reads in flight).
 */
#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "media/demuxer/data_source.h"

namespace XER::AsyncIO::IO::Async {
class AsyncFileReader;
}

namespace XER::Media {

class AsyncDataSource final : public DataSource {
public:
    ~AsyncDataSource() override;

    static std::unique_ptr<AsyncDataSource> Open(const std::string& path,
                                               std::string* error_out);

    // DataSource (sync façade).
    Status ReadAt(std::uint64_t offset, std::uint8_t* out,
                  std::size_t size, std::size_t* read_out) override;
    std::int64_t size() const override;
    bool streaming() const override { return false; }

    // Native async read: resolves with the bytes (empty = EOS/error).
    std::future<std::vector<std::uint8_t>> ReadAtAsync(
        std::uint64_t offset, std::size_t size);

    std::size_t pending_reads() const { return pending_; }

private:
    AsyncDataSource() = default;
    std::unique_ptr<XER::AsyncIO::IO::Async::AsyncFileReader> reader_;
    std::atomic<std::size_t> pending_{0};
};

}  // namespace XER::Media
