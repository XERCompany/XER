#pragma once

#include <string>
#include <thread>
#include <cstdint>

#include "async_io/hardware_abstraction/cpu_info.h"

namespace XER::Audio::Core {

enum class ThreadPriority {
    LOW,
    NORMAL,
    HIGH,
    CRITICAL
};

class AudioThreadPriority {
public:
    AudioThreadPriority();
    ~AudioThreadPriority();

    // Priority control
    static bool SetCurrentThreadPriority(ThreadPriority priority);
    static bool SetThreadPriority(std::thread::native_handle_type thread_handle, ThreadPriority priority);

    // Affinity
    static bool SetThreadAffinity(int core_id);
    static bool GetOptimalCore(int& core_id);

    // Information
    static ThreadPriority GetCurrentPriority();
    static std::string GetPriorityName(ThreadPriority priority);

    // CPU integration
    static bool SetAffinity(int core_id);
    static int GetOptimalAudioCore();

private:
    XER::AsyncIO::IO::Hardware::CPUInfoProvider cpu_info_;
};

}  // namespace XER::Audio::Core
