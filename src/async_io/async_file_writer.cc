#include "async_file_writer.h"

#include "io_thread_pool.h"

#include <fstream>

namespace AsyncIO::IO::Async {

AsyncFileWriter::AsyncFileWriter()
    : file_handle_(nullptr), bytes_written_(0), is_writing_(false), pending_ops_(0) {}

AsyncFileWriter::~AsyncFileWriter() {
    CloseFile();
}

bool AsyncFileWriter::CreateFile(const std::string& filepath, bool append) {
    try {
        std::scoped_lock lock(state_mutex_);
        if (file_handle_ != nullptr) {
            std::ofstream* existing_file = static_cast<std::ofstream*>(file_handle_);
            existing_file->flush();
            existing_file->close();
            delete existing_file;
            file_handle_ = nullptr;
        }

        std::ios::openmode mode = std::ios::binary;
        if (append) {
            mode |= std::ios::app;
        } else {
            mode |= std::ios::out | std::ios::trunc;
        }

        std::ofstream* file = new std::ofstream(filepath, mode);
        if (!file->is_open()) {
            last_error_ = "Failed to create file: " + filepath;
            delete file;
            return false;
        }

        file_handle_ = file;
        bytes_written_ = 0;
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Create exception: ") + e.what();
        return false;
    }
}

bool AsyncFileWriter::IsFileOpen() const {
    std::scoped_lock lock(state_mutex_);
    return file_handle_ != nullptr;
}

void AsyncFileWriter::CloseFile() {
    std::scoped_lock lock(state_mutex_);
    if (file_handle_) {
        std::ofstream* file = static_cast<std::ofstream*>(file_handle_);
        file->flush();
        file->close();
        delete file;
        file_handle_ = nullptr;
    }
}

bool AsyncFileWriter::WriteAsync(const uint8_t* data, size_t bytes_to_write, WriteCallback callback) {
    std::ofstream* file = nullptr;
    {
        std::scoped_lock lock(state_mutex_);
        if (!file_handle_) {
            last_error_ = "File not open";
            return false;
        }

        if (is_writing_) {
            last_error_ = "Write already in progress";
            return false;
        }

        is_writing_ = true;
        ++pending_ops_;
        file = static_cast<std::ofstream*>(file_handle_);
    }

    std::vector<uint8_t> data_copy(data, data + bytes_to_write);

    IOThreadPool::GetSharedInstance().Enqueue([this, data_copy, callback, file]() {
        int error_code = 0;
        {
            std::scoped_lock lock(state_mutex_);
            if (file_handle_ != file) {
                error_code = 1;
            } else {
                file->write(reinterpret_cast<const char*>(data_copy.data()), static_cast<std::streamsize>(data_copy.size()));
                error_code = file->fail() ? 1 : 0;
                if (error_code == 0) {
                    bytes_written_ += static_cast<int64_t>(data_copy.size());
                }
            }
        }

        if (callback) {
            callback(error_code, error_code == 0 ? data_copy.size() : 0);
        }

        std::scoped_lock lock(state_mutex_);
        is_writing_ = false;
        --pending_ops_;
    });
    return true;
}

bool AsyncFileWriter::WriteSync(const uint8_t* data, size_t bytes_to_write) {
    try {
        std::scoped_lock lock(state_mutex_);
        if (!file_handle_) {
            last_error_ = "File not open";
            return false;
        }

        std::ofstream* file = static_cast<std::ofstream*>(file_handle_);
        file->write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes_to_write));
        
        if (!file->fail()) {
            bytes_written_ += static_cast<int64_t>(bytes_to_write);
            return true;
        }
        
        last_error_ = "Write failed";
        return false;
    } catch (const std::exception& e) {
        last_error_ = std::string("Write exception: ") + e.what();
        return false;
    }
}

bool AsyncFileWriter::FlushBuffer() {
    try {
        std::scoped_lock lock(state_mutex_);
        if (!file_handle_) {
            return false;
        }

        std::ofstream* file = static_cast<std::ofstream*>(file_handle_);
        file->flush();
        return !file->fail();
    } catch (...) {
        last_error_ = "Flush failed";
        return false;
    }
}

size_t AsyncFileWriter::GetBufferedBytes() const {
    std::scoped_lock lock(state_mutex_);
    return write_buffer_.size();
}

bool AsyncFileWriter::IsWriting() const {
    std::scoped_lock lock(state_mutex_);
    return is_writing_;
}

int AsyncFileWriter::GetPendingOperations() const {
    std::scoped_lock lock(state_mutex_);
    return pending_ops_;
}

int64_t AsyncFileWriter::GetBytesWritten() const {
    std::scoped_lock lock(state_mutex_);
    return bytes_written_;
}

std::string AsyncFileWriter::GetLastError() const {
    std::scoped_lock lock(state_mutex_);
    return last_error_;
}

}  // namespace AsyncIO::IO::Async
