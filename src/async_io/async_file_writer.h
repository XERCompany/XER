#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <mutex>

namespace AsyncIO::IO::Async {

// Write completion callback: (error_code, bytes_written)
using WriteCallback = std::function<void(int, size_t)>;

class AsyncFileWriter {
public:
    AsyncFileWriter();
    ~AsyncFileWriter();

    // File operations
    bool CreateFile(const std::string& filepath, bool append = false);
    bool IsFileOpen() const;
    void CloseFile();

    // Async write
    bool WriteAsync(const uint8_t* data, size_t bytes_to_write, WriteCallback callback);
    
    // Sync write (blocking)
    bool WriteSync(const uint8_t* data, size_t bytes_to_write);

    // Buffer methods
    bool FlushBuffer();
    size_t GetBufferedBytes() const;

    // Status
    bool IsWriting() const;
    int GetPendingOperations() const;
    int64_t GetBytesWritten() const;

    // Error tracking
    std::string GetLastError() const;

private:
    mutable std::mutex state_mutex_;
    void* file_handle_;
    std::vector<uint8_t> write_buffer_;
    int64_t bytes_written_;
    bool is_writing_;
    int pending_ops_;
    std::string last_error_;
};

}  // namespace AsyncIO::IO::Async
