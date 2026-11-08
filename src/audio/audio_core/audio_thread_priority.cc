#include "audio_thread_priority.h"

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
#elif defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#else
    #include <pthread.h>
#endif

namespace XER::Audio::Core {

AudioThreadPriority::AudioThreadPriority() {
    cpu_info_.Initialize();
}

AudioThreadPriority::~AudioThreadPriority() {}

bool AudioThreadPriority::SetCurrentThreadPriority(ThreadPriority priority) {
#ifdef _WIN32
    BOOL success = FALSE;
    int win_priority = THREAD_PRIORITY_NORMAL;
    
    switch (priority) {
        case ThreadPriority::LOW: win_priority = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::NORMAL: win_priority = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::HIGH: win_priority = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::CRITICAL: win_priority = THREAD_PRIORITY_HIGHEST; break;
    }
    
    success = ::SetThreadPriority(GetCurrentThread(), win_priority);
    return success != FALSE;
#elif defined(__linux__)
    struct sched_param param;
    int policy = SCHED_FIFO;
    
    switch (priority) {
        case ThreadPriority::CRITICAL: param.sched_priority = 98; break;
        case ThreadPriority::HIGH: param.sched_priority = 50; break;
        case ThreadPriority::NORMAL: param.sched_priority = 0; policy = SCHED_OTHER; break;
        case ThreadPriority::LOW: param.sched_priority = 0; policy = SCHED_OTHER; break;
    }
    
    return pthread_setschedparam(pthread_self(), policy, &param) == 0;
#else
    return false;
#endif
}

bool AudioThreadPriority::SetThreadPriority(std::thread::native_handle_type thread_handle, ThreadPriority priority) {
#ifdef _WIN32
    int win_priority = THREAD_PRIORITY_NORMAL;
    
    switch (priority) {
        case ThreadPriority::LOW: win_priority = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::NORMAL: win_priority = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::HIGH: win_priority = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::CRITICAL: win_priority = THREAD_PRIORITY_HIGHEST; break;
    }
    
    BOOL success = ::SetThreadPriority(thread_handle, win_priority);
    return success != FALSE;
#else
    return false;
#endif
}

bool AudioThreadPriority::SetThreadAffinity(int core_id) {
#ifdef _WIN32
    DWORD_PTR mask = 1ULL << core_id;
    DWORD_PTR success = SetThreadAffinityMask(GetCurrentThread(), mask);
    return success != 0;
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    return false;
#endif
}

bool AudioThreadPriority::GetOptimalCore(int& core_id) {
    // Prefer a high-performance core if available
    core_id = 0;
    return true;
}

ThreadPriority AudioThreadPriority::GetCurrentPriority() {
#ifdef _WIN32
    int priority = GetThreadPriority(GetCurrentThread());
    
    if (priority >= THREAD_PRIORITY_HIGHEST) {
        return ThreadPriority::CRITICAL;
    } else if (priority >= THREAD_PRIORITY_ABOVE_NORMAL) {
        return ThreadPriority::HIGH;
    } else if (priority <= THREAD_PRIORITY_BELOW_NORMAL) {
        return ThreadPriority::LOW;
    }
#endif
    
    return ThreadPriority::NORMAL;
}

std::string AudioThreadPriority::GetPriorityName(ThreadPriority priority) {
    switch (priority) {
        case ThreadPriority::LOW: return "Low";
        case ThreadPriority::NORMAL: return "Normal";
        case ThreadPriority::HIGH: return "High";
        case ThreadPriority::CRITICAL: return "Critical";
        default: return "Unknown";
    }
}

bool AudioThreadPriority::SetAffinity(int core_id) {
    return SetThreadAffinity(core_id);
}

int AudioThreadPriority::GetOptimalAudioCore() {
    int optimal_core = 0;
    // if (cpu_info_.IsInitialized()) {
    //     // Use first available core
    //     optimal_core = 0;
    // }
    return optimal_core;
}

}  // namespace XER::Audio::Core
