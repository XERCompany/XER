#include "io_stream_writer.h"
#include <fstream>

namespace AsyncIO::IO::Async {

IOStreamWriter::IOStreamWriter(std::string path)
    : path_(std::move(path)), open_(false) {}

IOStreamWriter::~IOStreamWriter() {
    Close();
}

bool IOStreamWriter::Open() {
    std::lock_guard lock(mutex_);
    open_ = true;
    return open_;
}

void IOStreamWriter::Close() {
    std::lock_guard lock(mutex_);
    open_ = false;
}

void IOStreamWriter::WriteAsync(size_t offset, const std::vector<std::uint8_t> &data, std::function<void(bool)> callback) {
    {
        std::lock_guard lock(mutex_);
        if (!open_) {
            if (callback) {
                callback(false);
            }
            return;
        }
    }

    IOThreadPool::GetSharedInstance().Enqueue([path=path_, offset, data, callback=std::move(callback)]() mutable {
        std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!output) {
            output.clear();
            output.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!output) {
                if (callback) {
                    callback(false);
                }
                return;
            }
        }

        output.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        if (callback) {
            callback(output.good());
        }
    });
}

} // namespace AsyncIO::IO::Async
