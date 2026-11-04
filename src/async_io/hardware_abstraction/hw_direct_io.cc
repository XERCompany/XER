#include "hw_direct_io.h"

#ifdef _WIN32
    #include <windows.h>
    #include <winioctl.h>
#elif defined(__linux__)
    #include <fcntl.h>
    #include <unistd.h>
    #include <fstream>
    #include <sstream>
#endif

namespace AsyncIO::IO::Hardware {

DirectIOManager::DirectIOManager()
    : initialized_(false), optimal_alignment_(4096), minimum_alignment_(512), 
      total_bytes_transferred_(0) {}

DirectIOManager::~DirectIOManager() {}

bool DirectIOManager::Initialize() {
    DetectAlignmentRequirements();
    initialized_ = true;
    return true;
}

bool DirectIOManager::IsInitialized() const {
    return initialized_;
}

DirectIOBuffer DirectIOManager::AllocateBuffer(size_t size, size_t alignment) {
    DirectIOBuffer buffer;
    buffer.size = size;
    buffer.alignment = alignment;

#ifdef _WIN32
    buffer.data = static_cast<uint8_t*>(_aligned_malloc(size, alignment));
#else
    posix_memalign(reinterpret_cast<void**>(&buffer.data), alignment, size);
#endif

    buffer.is_aligned = (reinterpret_cast<uintptr_t>(buffer.data) % alignment) == 0;
    return buffer;
}

void DirectIOManager::ReleaseBuffer(DirectIOBuffer& buffer) {
    if (buffer.data) {
#ifdef _WIN32
        _aligned_free(buffer.data);
#else
        free(buffer.data);
#endif
        buffer.data = nullptr;
    }
}

int DirectIOManager::OpenFileForDIO(const std::string& filepath, DirectIOMode mode) {
#ifdef _WIN32
    DWORD access = 0;
    switch (mode) {
        case DirectIOMode::READ: access = GENERIC_READ; break;
        case DirectIOMode::WRITE: access = GENERIC_WRITE; break;
        case DirectIOMode::READ_WRITE: access = GENERIC_READ | GENERIC_WRITE; break;
    }

    DWORD creation = (mode == DirectIOMode::WRITE || mode == DirectIOMode::READ_WRITE)
        ? OPEN_ALWAYS
        : OPEN_EXISTING;

    HANDLE file = CreateFileA(
        filepath.c_str(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        creation,
        FILE_FLAG_NO_BUFFERING,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(file));
#else
    int flags = O_DIRECT;
    switch (mode) {
        case DirectIOMode::READ: flags |= O_RDONLY; break;
        case DirectIOMode::WRITE: flags |= O_WRONLY; break;
        case DirectIOMode::READ_WRITE: flags |= O_RDWR; break;
    }

    int fd = open(filepath.c_str(), flags, 0644);
    return fd;  // -1 on failure
#endif
}

bool DirectIOManager::ReadDirect(int file_handle, DirectIOBuffer& buffer, size_t bytes) {
    if (!VerifyBufferAlignment(buffer)) {
        return false;
    }

#ifdef _WIN32
    DWORD bytes_read = 0;
    BOOL success = ReadFile(
        reinterpret_cast<HANDLE>(static_cast<intptr_t>(file_handle)),
        buffer.data,
        static_cast<DWORD>(bytes),
        &bytes_read,
        nullptr
    );
    
    if (success) {
        total_bytes_transferred_ += bytes_read;
    }
    return (success && bytes_read == bytes);
#else
    ssize_t bytes_read = read(file_handle, buffer.data, bytes);
    if (bytes_read > 0) {
        total_bytes_transferred_ += bytes_read;
    }
    return (bytes_read == static_cast<ssize_t>(bytes));
#endif
}

bool DirectIOManager::WriteDirect(int file_handle, const DirectIOBuffer& buffer, size_t bytes) {
    if (!VerifyBufferAlignment(buffer)) {
        return false;
    }

#ifdef _WIN32
    DWORD bytes_written = 0;
    BOOL success = WriteFile(
        reinterpret_cast<HANDLE>(static_cast<intptr_t>(file_handle)),
        buffer.data,
        static_cast<DWORD>(bytes),
        &bytes_written,
        nullptr
    );
    
    if (success) {
        total_bytes_transferred_ += bytes_written;
    }
    return (success && bytes_written == bytes);
#else
    ssize_t bytes_written = write(file_handle, buffer.data, bytes);
    if (bytes_written > 0) {
        total_bytes_transferred_ += bytes_written;
    }
    return (bytes_written == static_cast<ssize_t>(bytes));
#endif
}

void DirectIOManager::CloseFile(int file_handle) {
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(file_handle)));
#else
    close(file_handle);
#endif
}

bool DirectIOManager::VerifyBufferAlignment(const DirectIOBuffer& buffer) const {
    return buffer.is_aligned;
}

double DirectIOManager::GetAverageIOLatencyMS() const {
#ifdef __linux__
    // /proc/diskstats columns: major minor name reads_completed reads_merged
    //   sectors_read time_spent_reading_ms writes_completed writes_merged
    //   sectors_written time_spent_writing_ms ...
    std::ifstream f("/proc/diskstats");
    if (!f.is_open()) return 0.0;

    uint64_t total_ios = 0, total_time_ms = 0;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        int major, minor;
        std::string name;
        uint64_t reads, reads_merged, sectors_r, time_r_ms;
        uint64_t writes, writes_merged, sectors_w, time_w_ms;
        if (!(iss >> major >> minor >> name
                  >> reads >> reads_merged >> sectors_r >> time_r_ms
                  >> writes >> writes_merged >> sectors_w >> time_w_ms)) continue;
        // Skip loop/ram/zram devices
        if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 ||
            name.rfind("zram", 0) == 0) continue;
        const uint64_t ios = reads + writes;
        if (ios > 0) {
            total_ios    += ios;
            total_time_ms += (time_r_ms + time_w_ms);
        }
    }
    if (total_ios == 0) return 0.0;
    return static_cast<double>(total_time_ms) / static_cast<double>(total_ios);
#else
    return 0.0;
#endif
}

uint64_t DirectIOManager::GetTotalBytesTransferred() const {
    return total_bytes_transferred_;
}

void DirectIOManager::DetectAlignmentRequirements() {
#ifdef _WIN32
    // Windows typically requires 4KB alignment
    optimal_alignment_ = 4096;
    minimum_alignment_ = 512;
#elif defined(__linux__)
    // Linux typically requires page alignment
    optimal_alignment_ = sysconf(_SC_PAGE_SIZE);
    minimum_alignment_ = 512;
#endif
}

}  // namespace AsyncIO::IO::Hardware
