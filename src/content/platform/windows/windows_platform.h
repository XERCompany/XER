#pragma once

#include "../platform_common.h"
#include <windows.h>

namespace XER::Content::Platform {

// Native per-OS backend for IPlatform. platform_common.cc provides the
// canonical CreatePlatform() implementation; this class is the standalone
// Windows backend (distinct name to avoid clashing with it).
class NativeWindowsPlatform : public IPlatform {
public:
    NativeWindowsPlatform();
    ~NativeWindowsPlatform() override = default;

    // System information
    std::string GetOSVersion() const override;
    std::string GetArchitecture() const override;
    std::string GetHostname() const override;

    // File system
    std::string GetHomeDirectory() const override;
    std::string GetTempDirectory() const override;
    std::string GetApplicationDataDirectory() const override;

    // Process/Thread
    unsigned int GetProcessId() const override;
    unsigned int GetThreadId() const override;

    // Memory
    size_t GetTotalMemory() const override;
    size_t GetAvailableMemory() const override;

    // Time
    uint64_t GetTimestampMs() const override;
    void SleepMs(uint32_t milliseconds) const override;

private:
    std::string GetWindowsFolderPath(int csidl) const;
};

}  // namespace XER::Content::Platform
