#include "platform_common.h"

#ifdef _WIN32
#include "windows/windows_platform.h"
#elif defined(__linux__)
#include "linux/linux_platform.h"
#elif defined(__APPLE__)
#include "apple/apple_platform.h"
#endif

#include <chrono>

namespace Engine::Content::Platform {

PlatformType GetCurrentPlatform() {
#ifdef _WIN32
    return PlatformType::Windows;
#elif defined(__linux__)
    return PlatformType::Linux;
#elif defined(__APPLE__)
    return PlatformType::MacOS;
#else
    return PlatformType::Unknown;
#endif
}

const char* GetPlatformName() {
    switch (GetCurrentPlatform()) {
        case PlatformType::Windows: return "Windows";
        case PlatformType::Linux: return "Linux";
        case PlatformType::MacOS: return "macOS";
        default: return "Unknown";
    }
}

std::unique_ptr<IPlatform> CreatePlatform() {
#ifdef _WIN32
    return std::make_unique<WindowsPlatform>();
#elif defined(__linux__)
    return std::make_unique<LinuxPlatform>();
#elif defined(__APPLE__)
    return std::make_unique<ApplePlatform>();
#else
    return nullptr;
#endif
}

}  // namespace Engine::Content::Platform
