#include "disk_info.h"

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <sys/statvfs.h>
    #include <fstream>
    #include <sstream>
#endif

#include <cstring>
#include <string>
#include <algorithm>

namespace AsyncIO::IO::Hardware {

DiskInfoProvider::DiskInfoProvider() = default;
DiskInfoProvider::~DiskInfoProvider() = default;

bool DiskInfoProvider::Initialize() {
    initialized_ = true;
    return true;
}

#if defined(_WIN32)

static bool IsNetworkDrive(const std::string& root) {
    const std::wstring wroot(root.begin(), root.end());
    UINT type = GetDriveTypeW(wroot.c_str());
    return type == DRIVE_REMOTE;
}
static bool IsRemovableDrive(const std::string& root) {
    const std::wstring wroot(root.begin(), root.end());
    const UINT type = GetDriveTypeW(wroot.c_str());
    return type == DRIVE_REMOVABLE || type == DRIVE_CDROM;
}

std::vector<DiskInfo> DiskInfoProvider::GetDisks() const {
    std::vector<DiskInfo> disks;

    const DWORD buf_len = GetLogicalDriveStringsA(0, nullptr);
    if (buf_len == 0) return disks;
    std::vector<char> buffer(buf_len + 1);
    if (GetLogicalDriveStringsA(buf_len, buffer.data()) == 0) return disks;

    for (char* p = buffer.data(); *p != '\0'; p += std::strlen(p) + 1) {
        const std::string root(p);  // e.g. "C:\\"

        DiskInfo d{};
        d.device = root;
        d.mount_point = root;

        // Filesystem + label + flags
        char fs_name[MAX_PATH + 1] = {};
        char vol_name[MAX_PATH + 1] = {};
        DWORD fs_flags = 0;
        if (GetVolumeInformationA(root.c_str(), vol_name, sizeof(vol_name),
                                  nullptr, nullptr, &fs_flags,
                                  fs_name, sizeof(fs_name))) {
            d.filesystem = fs_name;
            d.label = vol_name;
            d.is_read_only = (fs_flags & FILE_READ_ONLY_VOLUME) != 0;
        }

        ULARGE_INTEGER avail, total, free;
        if (GetDiskFreeSpaceExA(root.c_str(), &avail, &total, &free)) {
            d.total_bytes = total.QuadPart;
            d.free_bytes  = avail.QuadPart;  // available to caller
            d.used_bytes  = d.total_bytes - free.QuadPart;
        }

        d.is_removable = IsRemovableDrive(root);
        d.is_network = IsNetworkDrive(root);
        disks.push_back(std::move(d));
    }
    return disks;
}

#elif defined(__linux__)

std::vector<DiskInfo> DiskInfoProvider::GetDisks() const {
    std::vector<DiskInfo> disks;
    std::ifstream mounts("/proc/mounts");
    if (!mounts.is_open()) return disks;

    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream iss(line);
        std::string dev, mount, fs;
        iss >> dev >> mount >> fs;
        if (dev.empty() || mount.empty()) continue;

        // Skip pseudo/kernel filesystems we can't stat meaningfully.
        if (fs == "proc" || fs == "sysfs" || fs == "cgroup" || fs == "cgroup2" ||
            fs == "devtmpfs" || fs == "devpts" || fs == "tmpfs" || fs == "ramfs" ||
            fs == "mqueue" || fs == "hugetlbfs" || fs == "fusectl" || fs == "securityfs" ||
            fs == "debugfs" || fs == "tracefs" || fs == "configfs" || fs == "pstore" ||
            fs == "bpf" || fs == "fuse.gvfsd-fuse" || fs == "autofs") {
            continue;
        }
        // Skip bind/overlay noise from containers when dev is overlay/squashfs without a real size.
        // (We still keep overlay if statvfs succeeds.)

        struct statvfs vfs{};
        if (statvfs(mount.c_str(), &vfs) != 0) continue;
        if (vfs.f_blocks == 0) continue;

        DiskInfo d{};
        d.device = dev;
        d.mount_point = mount;
        d.filesystem = fs;
        d.total_bytes = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
        d.free_bytes  = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize;
        d.used_bytes  = d.total_bytes - static_cast<uint64_t>(vfs.f_bfree) * vfs.f_frsize;
        d.is_read_only = (vfs.f_flag & ST_RDONLY) != 0;
        d.is_network = (fs == "nfs" || fs == "nfs4" || fs == "cifs" || fs == "smbfs" ||
                        fs == "fuse.sshfs" || dev.rfind("//", 0) == 0);
        d.is_removable = false;  // best-effort: requires udev, skipped
        disks.push_back(std::move(d));
    }
    return disks;
}

#else
std::vector<DiskInfo> DiskInfoProvider::GetDisks() const {
    return {};
}
#endif

DiskIoStats DiskInfoProvider::GetIoStats() const {
    DiskIoStats stats;
#if defined(__linux__)
    std::ifstream f("/proc/diskstats");
    if (!f.is_open()) return stats;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        int major, minor;
        std::string name;
        uint64_t reads, reads_merged, sectors_r, ms_r;
        uint64_t writes, writes_merged, sectors_w, ms_w;
        if (!(iss >> major >> minor >> name
                  >> reads >> reads_merged >> sectors_r >> ms_r
                  >> writes >> writes_merged >> sectors_w >> ms_w)) continue;
        // Skip virtual/pseudo devices
        if (name.rfind("loop", 0) == 0 || name.rfind("ram", 0) == 0 ||
            name.rfind("zram", 0) == 0 || name.rfind("sr", 0) == 0) continue;
        stats.reads        += reads;
        stats.writes       += writes;
        stats.read_sectors  += sectors_r;
        stats.write_sectors += sectors_w;
    }
    const uint64_t sector_size = 512;
    stats.read_bytes  = stats.read_sectors  * sector_size;
    stats.write_bytes = stats.write_sectors * sector_size;
#endif
    return stats;
}

}  // namespace AsyncIO::IO::Hardware
