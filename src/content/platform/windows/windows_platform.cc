#include "windows_platform.h"
#include <shlobj.h>
#include <memory>
#include <chrono>

namespace XER::Content::Platform {

NativeWindowsPlatform::NativeWindowsPlatform() {
}

std::string NativeWindowsPlatform::GetOSVersion() const {
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

#pragma warning(push)
#pragma warning(disable: 4996)
    if (!GetVersionEx((OSVERSIONINFO*)&osvi)) {
        return "Unknown Windows";
    }
#pragma warning(pop)

    return "Windows " + std::to_string(osvi.dwMajorVersion) + "." + 
           std::to_string(osvi.dwMinorVersion);
}

std::string NativeWindowsPlatform::GetArchitecture() const {
#ifdef _WIN64
    return "x64";
#else
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        return "x64";
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        return "x86";
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        return "ARM64";
    }
    return "Unknown";
#endif
}

std::string NativeWindowsPlatform::GetHostname() const {
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(computerName[0]);
    
    if (GetComputerNameA(computerName, &size)) {
        return std::string(computerName);
    }
    return "Unknown";
}

std::string NativeWindowsPlatform::GetHomeDirectory() const {
    return GetWindowsFolderPath(CSIDL_PROFILE);
}

std::string NativeWindowsPlatform::GetTempDirectory() const {
    char tempPath[MAX_PATH + 1];
    DWORD result = GetTempPathA(MAX_PATH + 1, tempPath);
    
    if (result > 0 && result <= MAX_PATH) {
        return std::string(tempPath);
    }
    return "C:\\Temp";
}

std::string NativeWindowsPlatform::GetApplicationDataDirectory() const {
    return GetWindowsFolderPath(CSIDL_APPDATA);
}

unsigned int NativeWindowsPlatform::GetProcessId() const {
    return ::GetCurrentProcessId();
}

unsigned int NativeWindowsPlatform::GetThreadId() const {
    return ::GetCurrentThreadId();
}

size_t NativeWindowsPlatform::GetTotalMemory() const {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    
    if (GlobalMemoryStatusEx(&statex)) {
        return static_cast<size_t>(statex.ullTotalPhys);
    }
    return 0;
}

size_t NativeWindowsPlatform::GetAvailableMemory() const {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    
    if (GlobalMemoryStatusEx(&statex)) {
        return static_cast<size_t>(statex.ullAvailPhys);
    }
    return 0;
}

uint64_t NativeWindowsPlatform::GetTimestampMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void NativeWindowsPlatform::SleepMs(uint32_t milliseconds) const {
    Sleep(milliseconds);
}

std::string NativeWindowsPlatform::GetWindowsFolderPath(int csidl) const {
    char path[MAX_PATH];
    
    if (SHGetFolderPathA(NULL, csidl, NULL, 0, path) == S_OK) {
        return std::string(path);
    }
    
    return "";
}

}  // namespace XER::Content::Platform
