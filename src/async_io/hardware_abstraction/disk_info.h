#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace XER::AsyncIO::IO::Hardware {

struct DiskInfo {
    std::string device;         // "C:", "/dev/sda1", etc.
    std::string mount_point;     // Windows: drive root; Linux: mount path
    std::string filesystem;      // "NTFS", "ext4", "tmpfs", etc. ("" if unknown)
    std::string label;           // Volume label (Windows) / "" otherwise
    uint64_t total_bytes = 0;
    uint64_t free_bytes  = 0;
    uint64_t used_bytes  = 0;
    bool is_removable    = false;
    bool is_read_only    = false;
    bool is_network      = false;  // Network share / remote mount
};

struct DiskIoStats {
    uint64_t reads       = 0;
    uint64_t writes      = 0;
    uint64_t read_bytes  = 0;
    uint64_t write_bytes = 0;
    uint64_t read_sectors  = 0;
    uint64_t write_sectors = 0;
};

class DiskInfoProvider {
public:
    DiskInfoProvider();
    ~DiskInfoProvider();

    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    // Enumerate mounted/usable disks.
    std::vector<DiskInfo> GetDisks() const;

    // Aggregate I/O counters across physical disks (Linux /proc/diskstats;
    // best-effort on Windows). Zero fields are unsupported rather than absent.
    DiskIoStats GetIoStats() const;

private:
    bool initialized_ = false;
};

}  // namespace XER::AsyncIO::IO::Hardware
