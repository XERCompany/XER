#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

class WatchRecompileService {
public:
    WatchRecompileService();
    ~WatchRecompileService();

    // Rebuild/re-run script with fresh V8 isolate.
    bool Recompile(const std::string& script_path, std::string* error_message) const;

    // Stop currently running watched child process, if any.
    void Stop();

private:
    bool SpawnChild(const std::string& script_path, std::string* error_message);
    bool StopChild(int grace_ms, std::string* error_message);

#ifdef _WIN32
    void* child_process_ = nullptr;   // HANDLE
#else
    int child_pid_ = -1;
#endif
};
