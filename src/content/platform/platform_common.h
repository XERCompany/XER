#pragma once

#include <string>
#include <memory>
#include <functional>

namespace Engine::Content::Platform {

// Platform detection
enum class PlatformType {
    Windows,
    Linux,
    MacOS,
    Unknown
};

PlatformType GetCurrentPlatform();
const char* GetPlatformName();

// Base platform interface
class IPlatform {
public:
    virtual ~IPlatform() = default;

    // System information
    virtual std::string GetOSVersion() const = 0;
    virtual std::string GetArchitecture() const = 0;
    virtual std::string GetHostname() const = 0;

    // File system
    virtual std::string GetHomeDirectory() const = 0;
    virtual std::string GetTempDirectory() const = 0;
    virtual std::string GetApplicationDataDirectory() const = 0;

    // Process/Thread
    virtual unsigned int GetCurrentProcessId() const = 0;
    virtual unsigned int GetCurrentThreadId() const = 0;

    // Memory
    virtual size_t GetTotalMemory() const = 0;
    virtual size_t GetAvailableMemory() const = 0;

    // Time
    virtual uint64_t GetTimestampMs() const = 0;
    virtual void SleepMs(uint32_t milliseconds) const = 0;
};

// Platform factory
std::unique_ptr<IPlatform> CreatePlatform();

}  // namespace Engine::Content::Platform
