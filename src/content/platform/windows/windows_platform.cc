#include "windows_platform.h"
#include <shlobj.h>
#include <memory>
#include <chrono>

namespace Engine::Content::Platform {

WindowsPlatform::WindowsPlatform() {
}

std::string WindowsPlatform::GetOSVersion() const {
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

std::string WindowsPlatform::GetArchitecture() const {
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

std::string WindowsPlatform::GetHostname() const {
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(computerName[0]);
    
    if (GetComputerNameA(computerName, &size)) {
        return std::string(computerName);
    }
    return "Unknown";
}

std::string WindowsPlatform::GetHomeDirectory() const {
    return GetWindowsFolderPath(CSIDL_PROFILE);
}

std::string WindowsPlatform::GetTempDirectory() const {
    char tempPath[MAX_PATH + 1];
    DWORD result = GetTempPathA(MAX_PATH + 1, tempPath);
    
    if (result > 0 && result <= MAX_PATH) {
        return std::string(tempPath);
    }
    return "C:\\Temp";
}

std::string WindowsPlatform::GetApplicationDataDirectory() const {
    return GetWindowsFolderPath(CSIDL_APPDATA);
}

unsigned int WindowsPlatform::GetCurrentProcessId() const {
    return ::GetCurrentProcessId();
}

unsigned int WindowsPlatform::GetCurrentThreadId() const {
    return GetCurrentThreadId();
}

size_t WindowsPlatform::GetTotalMemory() const {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    
    if (GlobalMemoryStatusEx(&statex)) {
        return static_cast<size_t>(statex.ullTotalPhys);
    }
    return 0;
}

size_t WindowsPlatform::GetAvailableMemory() const {
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    
    if (GlobalMemoryStatusEx(&statex)) {
        return static_cast<size_t>(statex.ullAvailPhys);
    }
    return 0;
}

uint64_t WindowsPlatform::GetTimestampMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void WindowsPlatform::SleepMs(uint32_t milliseconds) const {
    Sleep(milliseconds);
}

std::string WindowsPlatform::GetWindowsFolderPath(int csidl) const {
    char path[MAX_PATH];
    
    if (SHGetFolderPathA(NULL, csidl, NULL, 0, path) == S_OK) {
        return std::string(path);
    }
    
    return "";
}

}  // namespace Engine::Content::Platform
