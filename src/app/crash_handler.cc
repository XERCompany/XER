// CrashHandler — unhandled exception / terminate capture with minidump,
// text log, session state and silent auto-restart.
//
// Design notes:
//  - All work happens inside a single-shot guard (g_in_handler): a crash
//    inside the handler itself can never recurse.
//  - The report directory is resolved lazily at crash time through
//    CacheManager::Instance() — Install() runs before the cache layer and
//    must not depend on it.
//  - Auto-restart passes --crash-depth=N (max 2 restarts) so a crash loop
//    during startup cannot spin forever; crashes/pending.txt carries the
//    report location to the new instance.

#include "app/crash_handler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "cache/cache_manager.h"

namespace XER::App::Crash {
namespace {

std::atomic<bool> g_in_handler{false};
std::atomic<bool> g_installed{false};
ULONGLONG g_start_tick = 0;
std::mutex g_state_mutex;
std::int64_t g_project_id = 0;
std::string g_project_name;

constexpr int kKeepReports = 10;
constexpr int kMaxRestartDepth = 2;

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                      static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

std::string NowStamp() {
    const auto t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// Deletes oldest report dirs beyond kKeepReports. Best-effort.
void RotateReports(const std::filesystem::path& root) {
    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
    for (const auto& e :
         std::filesystem::directory_iterator(root, ec)) {
        if (e.is_directory(ec)) dirs.push_back(e.path());
    }
    std::sort(dirs.begin(), dirs.end());  // timestamped names sort chronologically
    while (dirs.size() > static_cast<size_t>(kKeepReports)) {
        std::filesystem::remove_all(dirs.front(), ec);
        dirs.erase(dirs.begin());
    }
}

void WriteTextFile(const std::filesystem::path& path,
                   const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f) f << text;
}

// Appends one stack frame line: "  #NN <symbol> <module>+0x<rva>".
void LogStackFrame(std::ofstream& log, DWORD machine, HANDLE process,
                   HANDLE thread, CONTEXT* ctx, int index) {
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx->Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx->Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx->Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < index; ++i) {
        if (!StackWalk64(machine, process, thread, &sf, ctx,
                         nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
            return;
        }
    }
    if (!StackWalk64(machine, process, thread, &sf, ctx,
                     nullptr, SymFunctionTableAccess64,
                     SymGetModuleBase64, nullptr)) {
        return;
    }
    char symbuf[sizeof(SYMBOL_INFO) + 256]{};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    std::string name = "<unknown>";
    if (SymFromAddr(process, sf.AddrPC.Offset, &disp, sym)) {
        name = sym->Name;
    }
    // Module + RVA for the faulting address.
    IMAGEHLP_MODULE64 mi{};
    mi.SizeOfStruct = sizeof(mi);
    std::string mod = "<unknown>";
    DWORD64 rva = 0;
    if (SymGetModuleInfo64(process, sf.AddrPC.Offset, &mi)) {
        mod = mi.ModuleName;
        rva = sf.AddrPC.Offset - mi.BaseOfImage;
    }
    char line[1024];
    std::snprintf(line, sizeof(line),
                  "  #%02d 0x%llx  %s  %s+0x%llx\n", index,
                  static_cast<unsigned long long>(sf.AddrPC.Offset),
                  name.c_str(), mod.c_str(),
                  static_cast<unsigned long long>(rva));
    log << line;
}

void WriteMiniDump(const std::filesystem::path& path,
                   const EXCEPTION_POINTERS* ep) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    if (ep != nullptr) {
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = const_cast<EXCEPTION_POINTERS*>(ep);
        mei.ClientPointers = FALSE;
    }
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      static_cast<MINIDUMP_TYPE>(
                          MiniDumpWithIndirectlyReferencedMemory |
                          MiniDumpScanMemory),
                      ep != nullptr ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

// Writes crash.dmp + crash.log + state.json into dir, marks pending.txt,
// rotates old reports, optionally restarts the process.
void WriteReport(const EXCEPTION_POINTERS* ep, const char* kind,
                 int restart_depth) {
    const auto root = CrashesRoot();
    const auto dir = root / NowStamp();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return;

    WriteMiniDump(dir / "crash.dmp", ep);

    // --- crash.log ---
    {
        std::ofstream log(dir / "crash.log", std::ios::binary | std::ios::trunc);
        if (log) {
            wchar_t exe_w[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe_w, MAX_PATH);
            log << "kind: " << kind << "\n";
            log << "time: " << NowStamp() << "\n";
            log << "exe:  " << WideToUtf8(exe_w) << "\n";
            log << "pid:  " << GetCurrentProcessId() << "\n";
            log << "tid:  " << GetCurrentThreadId() << "\n";
            log << "uptime_ms: "
                << static_cast<unsigned long long>(
                       GetTickCount64() - g_start_tick)
                << "\n";
            {
                std::lock_guard<std::mutex> lk(g_state_mutex);
                log << "project_id: " << g_project_id << "\n";
                log << "project: " << g_project_name << "\n";
            }
            if (ep != nullptr && ep->ExceptionRecord != nullptr) {
                const auto* rec = ep->ExceptionRecord;
                log << "exception_code: 0x"
                    << std::hex << rec->ExceptionCode << std::dec << "\n";
                log << "exception_flags: 0x" << std::hex
                    << rec->ExceptionFlags << std::dec << "\n";
                log << "exception_addr: 0x" << std::hex
                    << reinterpret_cast<uintptr_t>(rec->ExceptionAddress)
                    << std::dec << "\n";
                if (rec->NumberParameters > 0) {
                    for (DWORD i = 0; i < rec->NumberParameters && i < 4;
                         ++i) {
                        log << "param[" << i << "]: 0x" << std::hex
                            << rec->ExceptionInformation[i] << std::dec
                            << "\n";
                    }
                }
            }
            CONTEXT ctx{};
            const CONTEXT* c = nullptr;
            if (ep != nullptr && ep->ContextRecord != nullptr) {
                c = ep->ContextRecord;
            } else {
                RtlCaptureContext(&ctx);
                c = &ctx;
            }
            log << "rip: 0x" << std::hex
                << static_cast<unsigned long long>(c->Rip) << std::dec
                << "\n";
            log << "rsp: 0x" << std::hex
                << static_cast<unsigned long long>(c->Rsp) << std::dec
                << "\n";
            log << "rbp: 0x" << std::hex
                << static_cast<unsigned long long>(c->Rbp) << std::dec
                << "\n";
            // Module containing the fault address (dbghelp resolves
            // name + base — no psapi dependency).
            {
                const auto addr = ep != nullptr && ep->ExceptionRecord
                                      ? reinterpret_cast<uintptr_t>(
                                            ep->ExceptionRecord
                                                ->ExceptionAddress)
                                      : 0;
                if (addr != 0) {
                    IMAGEHLP_MODULE64 mi{};
                    mi.SizeOfStruct = sizeof(mi);
                    if (SymGetModuleInfo64(GetCurrentProcess(), addr,
                                           &mi)) {
                        log << "module: " << mi.ModuleName
                            << " +0x" << std::hex
                            << (addr - mi.BaseOfImage) << std::dec
                            << "\n";
                    }
                }
            }
            // Stack walk (best-effort — symbols may be partial).
            {
                HANDLE thread = OpenThread(THREAD_GET_CONTEXT |
                                               THREAD_QUERY_INFORMATION,
                                           FALSE, GetCurrentThreadId());
                if (thread != nullptr) {
                    CONTEXT walk_ctx = *c;
                    log << "stack:\n";
                    for (int i = 0; i < 64; ++i) {
                        LogStackFrame(log, IMAGE_FILE_MACHINE_AMD64,
                                      GetCurrentProcess(), thread,
                                      &walk_ctx, i);
                    }
                    CloseHandle(thread);
                }
            }
            // Raw stack scan: release builds have no frame pointers, so
            // StackWalk64 often stops at frame 0. Scan qwords at RSP for
            // values that land inside a loaded module — probable return
            // addresses — and log module+RVA for each.
            {
                log << "stackscan (qwords pointing into modules):\n";
                constexpr size_t kScanQwords = 128;
                std::vector<uintptr_t> words(kScanQwords, 0);
                SIZE_T got = 0;
                const auto* rsp_addr =
                    reinterpret_cast<const void*>(c->Rsp);
                if (ReadProcessMemory(GetCurrentProcess(), rsp_addr,
                                      words.data(),
                                      words.size() * sizeof(uintptr_t),
                                      &got) && got > 0) {
                    const size_t n = got / sizeof(uintptr_t);
                    int hits = 0;
                    for (size_t i = 0; i < n && hits < 48; ++i) {
                        const uintptr_t v = words[i];
                        if (v < 0x10000 || v > 0x00007FFFFFFFFFFF) continue;
                        IMAGEHLP_MODULE64 mi{};
                        mi.SizeOfStruct = sizeof(mi);
                        if (!SymGetModuleInfo64(GetCurrentProcess(), v, &mi)) {
                            continue;
                        }
                        char line[512];
                        std::snprintf(
                            line, sizeof(line),
                            "  stk[%03zu] 0x%llx  %s+0x%llx\n", i,
                            static_cast<unsigned long long>(v),
                            mi.ModuleName,
                            static_cast<unsigned long long>(v - mi.BaseOfImage));
                        log << line;
                        ++hits;
                    }
                }
            }
        }
    }

    // --- state.json ---
    {
        std::ofstream js(dir / "state.json",
                         std::ios::binary | std::ios::trunc);
        if (js) {
            std::lock_guard<std::mutex> lk(g_state_mutex);
            js << "{\n"
               << "  \"time\": \"" << NowStamp() << "\",\n"
               << "  \"kind\": \"" << kind << "\",\n"
               << "  \"uptime_ms\": "
               << (GetTickCount64() - g_start_tick) << ",\n"
               << "  \"project_id\": " << g_project_id << ",\n"
               << "  \"project\": \"" << g_project_name << "\"\n"
               << "}\n";
        }
    }

    // --- pending marker for the next launch ---
    // Report dirs are ASCII timestamps — .string() is lossless here.
    WriteTextFile(root / "pending.txt", dir.string() + "\n");

    RotateReports(root);

    // --- silent auto-restart (bounded by depth) ---
    if (restart_depth < kMaxRestartDepth) {
        wchar_t exe_w[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe_w, MAX_PATH) != 0) {
            std::wstring cmd = L"\"";
            cmd += exe_w;
            cmd += L"\" ";
            cmd += GetCommandLineW();  // original args preserved
            cmd += L" --crash-depth=";
            cmd += std::to_wstring(restart_depth + 1);
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::vector<wchar_t> buf(cmd.begin(), cmd.end());
            buf.push_back(L'\0');
            if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
                               FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
}

// Reads --crash-depth=N from the process command line (GetCommandLineW —
// no argv plumbing needed inside the handler).
int CrashDepth() {
    const wchar_t* cmd = GetCommandLineW();
    if (cmd == nullptr) return 0;
    const std::wstring s(cmd);
    const std::wstring needle = L"--crash-depth=";
    const auto pos = s.find(needle);
    if (pos == std::wstring::npos) return 0;
    int depth = 0;
    for (size_t i = pos + needle.size();
         i < s.size() && s[i] >= L'0' && s[i] <= L'9'; ++i) {
        depth = depth * 10 + (s[i] - L'0');
    }
    return depth;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
    if (g_in_handler.exchange(true)) {
        return EXCEPTION_EXECUTE_HANDLER;  // recursive crash — bail fast
    }
    WriteReport(ep, "exception", CrashDepth());
    ExitProcess(ep->ExceptionRecord != nullptr
                    ? static_cast<UINT>(ep->ExceptionRecord->ExceptionCode)
                    : 0xE0C0FFEE);
    return EXCEPTION_EXECUTE_HANDLER;  // unreachable
}

void TerminateHandler() {
    if (g_in_handler.exchange(true)) {
        std::abort();
    }
    WriteReport(nullptr, "terminate", CrashDepth());
    std::abort();
}

void PurecallHandler() {
    if (g_in_handler.exchange(true)) {
        std::abort();
    }
    WriteReport(nullptr, "purecall", CrashDepth());
    std::abort();
}

void InvalidParamHandler(const wchar_t*, const wchar_t*,
                         const wchar_t*, unsigned int, uintptr_t) {
    if (g_in_handler.exchange(true)) {
        std::abort();
    }
    WriteReport(nullptr, "invalid_param", CrashDepth());
    std::abort();
}

} // namespace

std::filesystem::path CrashesRoot() {
    return XER::Engine::Cache::CacheManager::Instance().Persistent().root() /
           "crashes";
}

void Install() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) {
        return;
    }
    g_start_tick = GetTickCount64();

    // Symbols: deferred loads keep Install cheap; dbghelp resolves module
    // names + offsets during report writing.
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                  SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    SetUnhandledExceptionFilter(UnhandledFilter);
    std::set_terminate(TerminateHandler);
    _set_purecall_handler(PurecallHandler);
    _set_invalid_parameter_handler(InvalidParamHandler);
}

std::optional<std::filesystem::path> ConsumePendingReport() {
    const auto marker = CrashesRoot() / "pending.txt";
    std::ifstream f(marker, std::ios::binary);
    if (!f) return std::nullopt;
    std::string line;
    std::getline(f, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    std::error_code ec;
    std::filesystem::remove(marker, ec);
    if (line.empty()) return std::nullopt;
    return std::filesystem::path(Utf8ToWide(line));
}

void OpenReportFolder(const std::filesystem::path& dir) {
    const std::wstring w = dir.wstring();
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

void SetSessionProject(std::int64_t id, std::string name) {
    std::lock_guard<std::mutex> lk(g_state_mutex);
    g_project_id = id;
    g_project_name = std::move(name);
}

int RestartDepth(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const std::string needle = "--crash-depth=";
        if (a.rfind(needle, 0) == 0) {
            return std::atoi(a.c_str() + needle.size());
        }
    }
    return 0;
}

} // namespace XER::App::Crash
