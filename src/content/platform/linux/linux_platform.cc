#include "linux_platform.h"
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdlib>

namespace Engine::Content::Platform {

LinuxPlatform::LinuxPlatform() {
}

std::string LinuxPlatform::GetOSVersion() const {
    std::ifstream file("/etc/os-release");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                size_t start = line.find('"') + 1;
                size_t end = line.rfind('"');
                if (start < end) {
                    return line.substr(start, end - start);
                }
            }
        }
    }
    
    struct utsname uname_info;
    if (uname(&uname_info) == 0) {
        return std::string(uname_info.sysname) + " " + uname_info.release;
    }
    
    return "Unknown Linux";
}

std::string LinuxPlatform::GetArchitecture() const {
    struct utsname uname_info;
    if (uname(&uname_info) == 0) {
        return std::string(uname_info.machine);
    }
    return "Unknown";
}

std::string LinuxPlatform::GetHostname() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "Unknown";
}

std::string LinuxPlatform::GetHomeDirectory() const {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home);
    }
    return "/tmp";
}

std::string LinuxPlatform::GetTempDirectory() const {
    const char* tmp = std::getenv("TMPDIR");
    if (tmp) {
        return std::string(tmp);
    }
    return "/tmp";
}

std::string LinuxPlatform::GetApplicationDataDirectory() const {
    std::string home = GetHomeDirectory();
    return home + "/.local/share";
}

unsigned int LinuxPlatform::GetCurrentProcessId() const {
    return getpid();
}

unsigned int LinuxPlatform::GetCurrentThreadId() const {
#ifdef __linux__
    return static_cast<unsigned int>(syscall(SYS_gettid));
#else
    return static_cast<unsigned int>(pthread_self());
#endif
}

size_t LinuxPlatform::GetTotalMemory() const {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return static_cast<size_t>(info.totalram) * info.mem_unit;
    }
    return 0;
}

size_t LinuxPlatform::GetAvailableMemory() const {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return static_cast<size_t>(info.freeram) * info.mem_unit;
    }
    return 0;
}

uint64_t LinuxPlatform::GetTimestampMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void LinuxPlatform::SleepMs(uint32_t milliseconds) const {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

}  // namespace Engine::Content::Platform
