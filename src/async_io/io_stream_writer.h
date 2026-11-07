#ifndef IO_ASYNC_IO_STREAM_WRITER_H
#define IO_ASYNC_IO_STREAM_WRITER_H

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "io_thread_pool.h"

namespace XER::AsyncIO::IO::Async {

class IOStreamWriter {
public:
    explicit IOStreamWriter(std::string path);
    ~IOStreamWriter();

    bool Open();
    void Close();
    void WriteAsync(size_t offset, const std::vector<std::uint8_t> &data, std::function<void(bool)> callback);

private:
    std::string path_;
    std::mutex mutex_;
    bool open_ = false;
};

} // namespace XER::AsyncIO::IO::Async

#endif // IO_ASYNC_IO_STREAM_WRITER_H
