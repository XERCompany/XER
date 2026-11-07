#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace XER::AsyncIO::IO::Hardware {

// Direct I/O operation modes
enum class DirectIOMode {
    READ,
    WRITE,
    READ_WRITE
};

// Buffer alignment requirements
struct DirectIOBuffer {
    uint8_t* data;
    size_t size;
    size_t alignment;
    bool is_aligned;
};

class DirectIOManager {
public:
    DirectIOManager();
    ~DirectIOManager();

    // Initialization
    bool Initialize();
    bool IsInitialized() const;

    // Buffer management
    DirectIOBuffer AllocateBuffer(size_t size, size_t alignment = 4096);
    void ReleaseBuffer(DirectIOBuffer& buffer);

    // Direct I/O operations
    // OpenFileForDIO returns an opaque file handle (>= 0) usable with
    // ReadDirect/WriteDirect, or -1 on failure. Use CloseFile to release it.
    int  OpenFileForDIO(const std::string& filepath, DirectIOMode mode);
    bool ReadDirect(int file_handle, DirectIOBuffer& buffer, size_t bytes);
    bool WriteDirect(int file_handle, const DirectIOBuffer& buffer, size_t bytes);
    void CloseFile(int file_handle);

    // Alignment utilities
    size_t GetOptimalAlignment() const { return optimal_alignment_; }
    size_t GetMinimumAlignment() const { return minimum_alignment_; }
    bool VerifyBufferAlignment(const DirectIOBuffer& buffer) const;

    // Performance info
    double GetAverageIOLatencyMS() const;
    uint64_t GetTotalBytesTransferred() const;

private:
    bool initialized_;
    size_t optimal_alignment_;
    size_t minimum_alignment_;
    uint64_t total_bytes_transferred_;

    void DetectAlignmentRequirements();
};

}  // namespace XER::AsyncIO::IO::Hardware
