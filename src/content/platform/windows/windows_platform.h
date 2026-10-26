#pragma once

#include "../platform_common.h"
#include <windows.h>

namespace Engine::Content::Platform {

class WindowsPlatform : public IPlatform {
public:
    WindowsPlatform();
    ~WindowsPlatform() override = default;

    // System information
    std::string GetOSVersion() const override;
    std::string GetArchitecture() const override;
    std::string GetHostname() const override;

    // File system
    std::string GetHomeDirectory() const override;
    std::string GetTempDirectory() const override;
    std::string GetApplicationDataDirectory() const override;

    // Process/Thread
    unsigned int GetCurrentProcessId() const override;
    unsigned int GetCurrentThreadId() const override;

    // Memory
    size_t GetTotalMemory() const override;
    size_t GetAvailableMemory() const override;

    // Time
    uint64_t GetTimestampMs() const override;
    void SleepMs(uint32_t milliseconds) const override;

private:
    std::string GetWindowsFolderPath(int csidl) const;
};

}  // namespace Engine::Content::Platform
