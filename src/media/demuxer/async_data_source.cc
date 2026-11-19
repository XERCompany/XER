#include "media/demuxer/async_data_source.h"

#include <cstring>

#include "async_io/async_file_reader.h"

namespace XER::Media {

AsyncDataSource::~AsyncDataSource() {
    if (reader_ && reader_->IsFileOpen()) reader_->CloseFile();
}

std::unique_ptr<AsyncDataSource> AsyncDataSource::Open(
    const std::string& path, std::string* error_out) {
    auto ds = std::unique_ptr<AsyncDataSource>(new AsyncDataSource);
    ds->reader_ =
        std::make_unique<XER::AsyncIO::IO::Async::AsyncFileReader>();
    if (!ds->reader_->OpenFile(path)) {
        if (error_out) *error_out = "cannot open " + path;
        return nullptr;
    }
    return ds;
}

std::int64_t AsyncDataSource::size() const {
    return reader_ ? reader_->GetFileSize() : -1;
}

DataSource::Status AsyncDataSource::ReadAt(std::uint64_t offset,
                                           std::uint8_t* out,
                                           std::size_t size,
                                           std::size_t* read_out) {
    if (read_out) *read_out = 0;
    auto fut = ReadAtAsync(offset, size);
    auto data = fut.get();
    if (data.empty())
        return offset >= static_cast<std::uint64_t>(this->size())
                   ? Status::kEos
                   : Status::kError;
    const std::size_t n = data.size() < size ? data.size() : size;
    std::memcpy(out, data.data(), n);
    if (read_out) *read_out = n;
    return Status::kOk;
}

std::future<std::vector<std::uint8_t>> AsyncDataSource::ReadAtAsync(
    std::uint64_t offset, std::size_t size) {
    auto promise =
        std::make_shared<std::promise<std::vector<std::uint8_t>>>();
    auto fut = promise->get_future();
    if (!reader_ || !reader_->IsFileOpen()) {
        promise->set_value({});
        return fut;
    }
    ++pending_;
    auto buf = std::make_shared<std::vector<std::uint8_t>>(size);
    reader_->ReadAsyncAt(
        static_cast<std::int64_t>(offset), size,
        [this, promise, buf](int error_code, std::size_t bytes_read,
                             const std::uint8_t* data) {
            --pending_;
            if (error_code != 0 || !data) {
                promise->set_value({});
                return;
            }
            buf->assign(data, data + bytes_read);
            promise->set_value(std::move(*buf));
        });
    return fut;
}

}  // namespace XER::Media
