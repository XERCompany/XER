#include "io_stream_reader.h"
#include <fstream>

namespace AsyncIO::IO::Async {

IOStreamReader::IOStreamReader(std::string path)
    : path_(std::move(path)), open_(false) {}

IOStreamReader::~IOStreamReader() {
    Close();
}

bool IOStreamReader::Open() {
    std::lock_guard lock(mutex_);
    open_ = true;
    return open_;
}

void IOStreamReader::Close() {
    std::lock_guard lock(mutex_);
    open_ = false;
}

void IOStreamReader::ReadAsync(size_t offset, size_t length, std::function<void(std::vector<std::uint8_t>, bool)> callback) {
    {
        std::lock_guard lock(mutex_);
        if (!open_) {
            if (callback) {
                callback({}, false);
            }
            return;
        }
    }

    IOThreadPool::GetSharedInstance().Enqueue([path=path_, offset, length, callback=std::move(callback)]() mutable {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            if (callback) {
                callback({}, false);
            }
            return;
        }

        input.seekg(0, std::ios::end);
        size_t fileSize = static_cast<size_t>(input.tellg());
        if (offset > fileSize) {
            if (callback) {
                callback({}, false);
            }
            return;
        }

        size_t readLen = std::min(length, fileSize - offset);
        input.seekg(offset, std::ios::beg);

        std::vector<std::uint8_t> buffer(readLen);
        input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(readLen));
        if (callback) {
            callback(std::move(buffer), input.good() || input.eof());
        }
    });
}

} // namespace AsyncIO::IO::Async
