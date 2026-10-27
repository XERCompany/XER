/**
 * This file is part of XER, Platform subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#include "content/platform/platform_common.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <lmcons.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#include <pwd.h>
#include <pthread.h>
#include <mach/mach.h>
#else
#include <sys/sysinfo.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#endif

namespace Engine::Content::Platform {

PlatformType GetCurrentPlatform() {
#ifdef _WIN32
  return PlatformType::Windows;
#elif defined(__APPLE__)
  return PlatformType::MacOS;
#elif defined(__linux__)
  return PlatformType::Linux;
#else
  return PlatformType::Unknown;
#endif
}

const char* GetPlatformName() {
  switch (GetCurrentPlatform()) {
    case PlatformType::Windows:
      return "Windows";
    case PlatformType::Linux:
      return "Linux";
    case PlatformType::MacOS:
      return "macOS";
    default:
      return "Unknown";
  }
}

// Windows implementation
#ifdef _WIN32
class WindowsPlatform : public IPlatform {
public:
  std::string GetOSVersion() const override {
    OSVERSIONINFOEXA osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXA));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    
#pragma warning(push)
#pragma warning(disable: 4996)
    if (GetVersionExA((OSVERSIONINFOA*)&osvi)) {
      char version[64];
      snprintf(version, sizeof(version), "Windows %d.%d (Build %d)",
               osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
      return version;
    }
#pragma warning(pop)
    
    return "Windows (Unknown Version)";
  }
  
  std::string GetArchitecture() const {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    
    switch (si.wProcessorArchitecture) {
      case PROCESSOR_ARCHITECTURE_AMD64:
        return "x64";
      case PROCESSOR_ARCHITECTURE_INTEL:
        return "x86";
      case PROCESSOR_ARCHITECTURE_ARM64:
        return "ARM64";
      case PROCESSOR_ARCHITECTURE_ARM:
        return "ARM";
      default:
        return "Unknown";
    }
  }
  
  std::string GetHostname() const {
    char hostname[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(hostname);
    
    if (GetComputerNameA(hostname, &size)) {
      return hostname;
    }
    return "localhost";
  }
  
  std::string GetHomeDirectory() const {
    char path[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, path) == S_OK) {
      return path;
    }
    return "C:\\Users\\Default";
  }
  
  std::string GetTempDirectory() const {
    char path[MAX_PATH];
    if (GetTempPathA(MAX_PATH, path) > 0) {
      return path;
    }
    return "C:\\Temp";
  }
  
  std::string GetApplicationDataDirectory() const {
    char path[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path) == S_OK) {
      return path;
    }
    return GetHomeDirectory();
  }
  
  unsigned int GetProcessId() const override {
    return GetCurrentProcessId();
  }
  
  unsigned int GetThreadId() const override {
    return GetCurrentThreadId();
  }
  
  size_t GetTotalMemory() const override {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullTotalPhys;
  }
  
  size_t GetAvailableMemory() const override {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullAvailPhys;
  }
  
  uint64_t GetTimestampMs() const override {
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    
    return static_cast<uint64_t>((counter.QuadPart * 1000) / frequency.QuadPart);
  }
  
  void SleepMs(uint32_t milliseconds) const override {
    Sleep(milliseconds);
  }
};
#endif

// macOS implementation
#ifdef __APPLE__
class MacOSPlatform : public IPlatform {
public:
  std::string GetOSVersion() const override {
    char str[256];
    size_t size = sizeof(str);
    
    if (sysctlbyname("kern.osrelease", str, &size, nullptr, 0) == 0) {
      return std::string("macOS ") + str;
    }
    return "macOS (Unknown Version)";
  }
  
  std::string GetArchitecture() const override {
    char str[256];
    size_t size = sizeof(str);
    
    if (sysctlbyname("hw.machine", str, &size, nullptr, 0) == 0) {
      return str;
    }
    return "Unknown";
  }
  
  std::string GetHostname() const override {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
      return hostname;
    }
    return "localhost";
  }
  
  std::string GetHomeDirectory() const override {
    const char* home = getenv("HOME");
    if (home) {
      return home;
    }
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
      return pw->pw_dir;
    }
    return "/tmp";
  }
  
  std::string GetTempDirectory() const override {
    return "/tmp";
  }
  
  std::string GetApplicationDataDirectory() const override {
    std::string home = GetHomeDirectory();
    return home + "/Library/Application Support";
  }
  
  unsigned int GetProcessId() const override {
    return getpid();
  }
  
  unsigned int GetThreadId() const override {
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<unsigned int>(tid);
  }
  
  size_t GetTotalMemory() const override {
    int mib[2];
    int64_t physical_memory;
    size_t length;
    
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(int64_t);
    
    if (sysctl(mib, 2, &physical_memory, &length, nullptr, 0) == 0) {
      return physical_memory;
    }
    return 0;
  }
  
  size_t GetAvailableMemory() const override {
    vm_size_t page_size;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);
    
    host_page_size(mach_host_self(), &page_size);
    
    if (host_statistics64(mach_host_self(), HOST_VM_INFO,
                          (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
      return static_cast<size_t>(vm_stats.free_count * page_size);
    }
    return 0;
  }
  
  uint64_t GetTimestampMs() const override {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
  }
  
  void SleepMs(uint32_t milliseconds) const override {
    usleep(milliseconds * 1000);
  }
};
#endif

// Linux implementation
#ifdef __linux__
class LinuxPlatform : public IPlatform {
public:
  std::string GetOSVersion() const override {
    struct utsname uname_info;
    if (uname(&uname_info) == 0) {
      return std::string(uname_info.sysname) + " " + uname_info.release;
    }
    return "Linux (Unknown Version)";
  }
  
  std::string GetArchitecture() const override {
    struct utsname uname_info;
    if (uname(&uname_info) == 0) {
      return uname_info.machine;
    }
    return "Unknown";
  }
  
  std::string GetHostname() const override {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
      return hostname;
    }
    return "localhost";
  }
  
  std::string GetHomeDirectory() const override {
    const char* home = getenv("HOME");
    if (home) {
      return home;
    }
    
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
      return pw->pw_dir;
    }
    return "/tmp";
  }
  
  std::string GetTempDirectory() const override {
    const char* tmp = getenv("TMPDIR");
    if (tmp) {
      return tmp;
    }
    return "/tmp";
  }
  
  std::string GetApplicationDataDirectory() const override {
    std::string home = GetHomeDirectory();
    const char* xdg_data = getenv("XDG_DATA_HOME");
    
    if (xdg_data) {
      return xdg_data;
    }
    
    return home + "/.local/share";
  }
  
  unsigned int GetProcessId() const override {
    return getpid();
  }
  
  unsigned int GetThreadId() const override {
    return static_cast<unsigned int>(gettid());
  }
  
  size_t GetTotalMemory() const override {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
      return info.totalram * info.mem_unit;
    }
    return 0;
  }
  
  size_t GetAvailableMemory() const override {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
      return info.freeram * info.mem_unit;
    }
    return 0;
  }
  
  uint64_t GetTimestampMs() const override {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
  }
  
  void SleepMs(uint32_t milliseconds) const override {
    usleep(milliseconds * 1000);
  }
};
#endif

std::unique_ptr<IPlatform> CreatePlatform() {
#ifdef _WIN32
  return std::make_unique<WindowsPlatform>();
#elif defined(__APPLE__)
  return std::make_unique<MacOSPlatform>();
#elif defined(__linux__)
  return std::make_unique<LinuxPlatform>();
#else
  return nullptr;
#endif
}

}  // namespace Engine::Content::Platform
