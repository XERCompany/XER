#include "async_file_reader.h"

#include "io_thread_pool.h"

#include <algorithm>

namespace AsyncIO::IO::Async {

AsyncFileReader::AsyncFileReader()
    : file_size_(0), current_position_(0), pending_ops_(0) {}

AsyncFileReader::~AsyncFileReader() {
    CloseFile();
}

bool AsyncFileReader::OpenFile(const std::string& filepath) {
    try {
        std::scoped_lock lock(state_mutex_);
        filepath_.clear();
        file_size_ = 0;
        current_position_ = 0;

        // Probe the file by opening it once; we don't keep this stream.
        auto probe = std::make_unique<std::ifstream>(filepath, std::ios::binary);
        if (!probe->is_open()) {
            last_error_ = "Failed to open file: " + filepath;
            return false;
        }
        probe->seekg(0, std::ios::end);
        const auto end = probe->tellg();
        if (end < 0) {
            last_error_ = "Failed to query file size: " + filepath;
            return false;
        }
        filepath_ = filepath;
        file_size_ = static_cast<int64_t>(end);
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Open exception: ") + e.what();
        return false;
    }
}

bool AsyncFileReader::IsFileOpen() const {
    std::scoped_lock lock(state_mutex_);
    return !filepath_.empty();
}

void AsyncFileReader::CloseFile() {
    std::scoped_lock lock(state_mutex_);
    filepath_.clear();
    file_size_ = 0;
    current_position_ = 0;
}

std::unique_ptr<std::ifstream> AsyncFileReader::OpenForRead() const {
    if (filepath_.empty()) return nullptr;
    auto f = std::make_unique<std::ifstream>(filepath_, std::ios::binary);
    if (!f->is_open()) return nullptr;
    return f;
}

bool AsyncFileReader::ReadAsync(size_t bytes_to_read, ReadCallback callback) {
    int64_t offset;
    {
        std::scoped_lock lock(state_mutex_);
        if (filepath_.empty()) {
            last_error_ = "File not open";
            return false;
        }
        offset = current_position_;
        // Reserve the range now so concurrent ReadAsync calls don't overlap.
        // ReadAsyncAt is preferred for true parallelism (independent offsets).
        current_position_ += static_cast<int64_t>(bytes_to_read);
    }
    return ReadAsyncAt(offset, bytes_to_read, std::move(callback));
}

bool AsyncFileReader::ReadAsyncAt(int64_t offset, size_t bytes_to_read, ReadCallback callback) {
    std::string path;
    {
        std::scoped_lock lock(state_mutex_);
        if (filepath_.empty()) {
            last_error_ = "File not open";
            return false;
        }
        path = filepath_;
    }

    pending_ops_.fetch_add(1, std::memory_order_relaxed);

    IOThreadPool::GetSharedInstance().Enqueue(
        [this, path, offset, bytes_to_read, callback = std::move(callback)]() {
            std::vector<uint8_t> buffer(bytes_to_read);
            size_t bytes_read = 0;
            int error_code = 0;

            // Open a fresh stream for this operation only. This is safe to
            // run concurrently with other ReadAsyncAt calls and with
            // CloseFile (we hold our own copy of the path; if the file is
            // closed the open simply fails).
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                error_code = 1;
            } else {
                file.seekg(offset, std::ios::beg);
                file.read(reinterpret_cast<char*>(buffer.data()),
                          static_cast<std::streamsize>(bytes_to_read));
                bytes_read = static_cast<size_t>(file.gcount());
                buffer.resize(bytes_read);
                error_code = (file.bad() ? 1 : 0);
            }

            if (callback) {
                callback(error_code, bytes_read, buffer.data());
            }

            pending_ops_.fetch_sub(1, std::memory_order_relaxed);
        });
    return true;
}

bool AsyncFileReader::ReadSync(size_t bytes_to_read, std::vector<uint8_t>& output_buffer) {
    try {
        int64_t offset;
        std::string path;
        {
            std::scoped_lock lock(state_mutex_);
            if (filepath_.empty()) {
                last_error_ = "File not open";
                return false;
            }
            offset = current_position_;
            path = filepath_;
            current_position_ += static_cast<int64_t>(bytes_to_read);
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            last_error_ = "File not open";
            return false;
        }
        file.seekg(offset, std::ios::beg);
        output_buffer.resize(bytes_to_read);
        file.read(reinterpret_cast<char*>(output_buffer.data()),
                  static_cast<std::streamsize>(bytes_to_read));
        const size_t bytes_read = static_cast<size_t>(file.gcount());
        output_buffer.resize(bytes_read);
        return !file.bad();
    } catch (const std::exception& e) {
        last_error_ = std::string("Read exception: ") + e.what();
        return false;
    }
}

bool AsyncFileReader::Seek(int64_t offset) {
    std::scoped_lock lock(state_mutex_);
    if (filepath_.empty()) {
        last_error_ = "File not open";
        return false;
    }
    if (offset < 0 || offset > file_size_) {
        last_error_ = "Seek out of range";
        return false;
    }
    current_position_ = offset;
    return true;
}

int64_t AsyncFileReader::GetPosition() const {
    std::scoped_lock lock(state_mutex_);
    return current_position_;
}

int64_t AsyncFileReader::GetFileSize() const {
    std::scoped_lock lock(state_mutex_);
    return file_size_;
}

bool AsyncFileReader::IsReading() const {
    return pending_ops_.load(std::memory_order_relaxed) > 0;
}

int AsyncFileReader::GetPendingOperations() const {
    return pending_ops_.load(std::memory_order_relaxed);
}

bool AsyncFileReader::QueryFileSize() {
    // Used only by OpenFile historically; size is now probed there directly.
    return file_size_ > 0;
}

std::string AsyncFileReader::GetLastError() const {
    std::scoped_lock lock(state_mutex_);
    return last_error_;
}

}  // namespace AsyncIO::IO::Async
