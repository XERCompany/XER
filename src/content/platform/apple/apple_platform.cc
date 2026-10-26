#include "apple_platform.h"
#include <unistd.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <pthread.h>
#include <chrono>
#include <thread>
#include <cstdlib>

namespace Engine::Content::Platform {

ApplePlatform::ApplePlatform() {
}

std::string ApplePlatform::GetOSVersion() const {
    char str[256];
    size_t size = sizeof(str);
    
    if (sysctlbyname("kern.osrelease", str, &size, NULL, 0) == 0) {
        return "macOS " + std::string(str);
    }
    
    return "Unknown macOS";
}

std::string ApplePlatform::GetArchitecture() const {
    char str[256];
    size_t size = sizeof(str);
    
    if (sysctlbyname("hw.machine", str, &size, NULL, 0) == 0) {
        return std::string(str);
    }
    
    return "Unknown";
}

std::string ApplePlatform::GetHostname() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "Unknown";
}

std::string ApplePlatform::GetHomeDirectory() const {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home);
    }
    return "/tmp";
}

std::string ApplePlatform::GetTempDirectory() const {
    const char* tmp = std::getenv("TMPDIR");
    if (tmp) {
        return std::string(tmp);
    }
    return "/tmp";
}

std::string ApplePlatform::GetApplicationDataDirectory() const {
    std::string home = GetHomeDirectory();
    return home + "/Library/Application Support";
}

unsigned int ApplePlatform::GetCurrentProcessId() const {
    return getpid();
}

unsigned int ApplePlatform::GetCurrentThreadId() const {
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return static_cast<unsigned int>(tid);
}

size_t ApplePlatform::GetTotalMemory() const {
    int mib[2];
    int64_t physical_memory;
    size_t length;
    
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(int64_t);
    
    if (sysctl(mib, 2, &physical_memory, &length, NULL, 0) == 0) {
        return static_cast<size_t>(physical_memory);
    }
    
    return 0;
}

size_t ApplePlatform::GetAvailableMemory() const {
    vm_size_t page_size;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);
    
    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS) {
        if (host_statistics64(mach_host_self(), HOST_VM_INFO, 
                              (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
            return static_cast<size_t>(vm_stats.free_count * page_size);
        }
    }
    
    return 0;
}

uint64_t ApplePlatform::GetTimestampMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void ApplePlatform::SleepMs(uint32_t milliseconds) const {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

}  // namespace Engine::Content::Platform
