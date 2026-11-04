#ifndef IO_ASYNC_IO_STREAM_READER_H
#define IO_ASYNC_IO_STREAM_READER_H

#include <cstddef>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "io_thread_pool.h"

namespace AsyncIO::IO::Async {

class IOStreamReader {
public:
    explicit IOStreamReader(std::string path);
    ~IOStreamReader();

    bool Open();
    void Close();
    void ReadAsync(size_t offset, size_t length, std::function<void(std::vector<std::uint8_t>, bool)> callback);

private:
    std::string path_;
    std::mutex mutex_;
    bool open_ = false;
};

} // namespace AsyncIO::IO::Async

#endif // IO_ASYNC_IO_STREAM_READER_H
