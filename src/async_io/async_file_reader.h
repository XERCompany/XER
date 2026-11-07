#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <fstream>

namespace XER::AsyncIO::IO::Async {

// Read completion callback: (error_code, bytes_read, data)
using ReadCallback = std::function<void(int, size_t, const uint8_t*)>;

// Asynchronous file reader.
//
// Design: each ReadAsync() call opens its own short-lived stream (or uses
// pread on Linux) so multiple reads can run concurrently on the IOThreadPool
// without serializing on a single shared handle. The reader keeps the file
// path + size cached; per-operation handles are cheap to create.
//
// Thread-safety: OpenFile/CloseFile/ReadAsync/ReadSync/Seek/GetPosition/
// GetFileSize/IsReading/GetPendingOperations/GetLastError are all safe to
// call from any thread. Multiple ReadAsync calls may be in flight at once.
class AsyncFileReader {
public:
    AsyncFileReader();
    ~AsyncFileReader();

    // File operations
    bool OpenFile(const std::string& filepath);
    bool IsFileOpen() const;
    void CloseFile();

    // Async read at the current logical position. Each call runs on the
    // shared IOThreadPool and invokes the callback exactly once, on the
    // worker thread, with (error_code, bytes_read, data). The data buffer
    // is owned by the reader for the duration of the callback and is freed
    // after the callback returns — copy it if you need it beyond the call.
    // Multiple ReadAsync calls may be in flight concurrently.
    bool ReadAsync(size_t bytes_to_read, ReadCallback callback);

    // Sync read (blocking). Advances the logical position.
    bool ReadSync(size_t bytes_to_read, std::vector<uint8_t>& output_buffer);

    // Async read at an explicit offset (pread-style, does not advance the
    // logical position). Preferred for concurrent reads from multiple
    // threads: each operation is independent.
    bool ReadAsyncAt(int64_t offset, size_t bytes_to_read, ReadCallback callback);

    // Stream control (affects ReadAsync/ReadSync logical position only;
    // ReadAsyncAt is unaffected).
    bool Seek(int64_t offset);
    int64_t GetPosition() const;
    int64_t GetFileSize() const;

    // Status
    bool IsReading() const;            // true if any async op in flight
    int GetPendingOperations() const;

    // Error tracking
    std::string GetLastError() const;

private:
    mutable std::mutex state_mutex_;
    std::string filepath_;             // empty => no file open
    int64_t file_size_;
    int64_t current_position_;          // advanced by ReadAsync/ReadSync only
    std::atomic<int> pending_ops_;
    std::string last_error_;

    bool QueryFileSize();
    void SetLastError(const std::string& msg) const;
    // Internal: open a fresh ifstream for one operation.
    std::unique_ptr<std::ifstream> OpenForRead() const;
};

}  // namespace XER::AsyncIO::IO::Async
