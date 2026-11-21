// EngineScanVST3 — VST3 module scanner with per-module process isolation.
//
// Orchestrator mode (default):
//   Enumerates every *.vst3 module under the scan roots, then probes each
//   module in a SEPARATE child copy of this executable
//   (`EngineScanVST3.exe --probe <module>`). Probes run concurrently on the
//   async_io thread pool, bounded by --jobs (default: CPU core count).
//   A plugin that crashes or hangs kills only its worker process; the scan
//   survives and records the failure.
//
// Worker mode:
//   EngineScanVST3.exe --probe <module_path>
//   Loads exactly one module binary, prints a JSON object on stdout
//   ({"xer_probe":1,"ok":true,...}) and exits. Never invoke directly.
//
// Usage:
//   EngineScanVST3.exe [root ...] [--out report.json] [--pretty]
//                      [--jobs N] [--timeout MS] [--in-process]

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "async_io/io_thread_pool.h"
#include "audio/plugin_wrappers/vst3_plugin_scanner.h"
#include "helper/string_utils.h"
#include "helper/win_job.h"

namespace {

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    return out;
}

std::optional<std::string> JsonGetString(const std::string& json,
                                         const std::string& key) {
    const std::string needle = "\"" + key + "\":\"";
    const size_t start = json.find(needle);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    size_t i = start + needle.size();
    std::string out;
    while (i < json.size()) {
        const char ch = json[i];
        if (ch == '"') {
            return out;
        }
        if (ch == '\\' && i + 1 < json.size()) {
            const char esc = json[++i];
            switch (esc) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':  // skip \uXXXX — ASCII fields don't produce it
                i += 4;
                break;
            default: out += esc; break;
            }
            ++i;
            continue;
        }
        out += ch;
        ++i;
    }
    return std::nullopt;
}

// ---- worker mode ---------------------------------------------------------

// Prints the probe result of a single module as one JSON object on stdout.
// Exit code: 0 on success, 2 when the module yields no plugin class.
int RunProbeMode(const std::string& module_path) {
    XER::Audio::Plugin::Vst3PluginInfo info;
    info.module_path = module_path;
    if (!XER::Audio::Plugin::ProbeVst3Module(module_path, &info)) {
        std::printf("{\"xer_probe\":1,\"ok\":false}\n");
        return 2;
    }
    std::printf("{\"xer_probe\":1,\"ok\":true,"
                "\"name\":\"%s\",\"vendor\":\"%s\","
                "\"category\":\"%s\",\"version\":\"%s\"}\n",
                JsonEscape(info.name).c_str(), JsonEscape(info.vendor).c_str(),
                JsonEscape(info.category).c_str(),
                JsonEscape(info.version).c_str());
    return 0;
}

// ---- subprocess runner ---------------------------------------------------

struct ProbeRun {
    int exit_code = -1;
    bool timed_out = false;
    bool spawn_failed = false;
    std::string output;
};

// Runs `exe --probe <module>` capturing stdout. A dedicated reader thread
// drains the pipe so a chatty plugin can't deadlock on the 4KB pipe buffer;
// the main worker waits with a timeout and hard-kills the child on expiry.
ProbeRun RunProbeProcess(const std::wstring& exe, const std::string& module,
                         DWORD timeout_ms) {
    ProbeRun run;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        run.spawn_failed = true;
        return run;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + exe + L"\" --probe \"" +
                       XER::Helper::String::Utf8ToWide(module) + L"\"";

    // Plugin noise on stderr must not interleave with the worker's JSON
    // line — sink it to NUL (inheritable via sa).
    HANDLE nul = CreateFileW(L"NUL:", GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = write_pipe;
    si.hStdError = nul;

    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(
        nullptr, cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr, nullptr, &si, &pi);
    if (nul != nullptr && nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    CloseHandle(write_pipe);  // child owns its copy now
    if (!created) {
        CloseHandle(read_pipe);
        run.spawn_failed = true;
        return run;
    }

    // Group the probe worker with the orchestrator: killing the scan
    // (Task Manager / console close) must reap every --probe child —
    // a hung plugin must not outlive the scan holding module locks.
    XER::Helper::WinJobGroup::Shared().Assign(pi.hProcess);

    std::thread reader([read_pipe, &run] {
        char buf[8192];
        DWORD n = 0;
        while (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
            run.output.append(buf, n);
        }
    });

    const DWORD waited = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (waited == WAIT_TIMEOUT) {
        run.timed_out = true;
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    } else {
        DWORD code = 0;
        if (GetExitCodeProcess(pi.hProcess, &code)) {
            run.exit_code = static_cast<int>(code);
        }
    }

    reader.join();  // killed/exited child closes the write end -> EOF
    CloseHandle(read_pipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return run;
}

// ---- orchestration -------------------------------------------------------

struct ProbeOutcome {
    XER::Audio::Plugin::Vst3ModuleCandidate candidate;
    XER::Audio::Plugin::Vst3PluginInfo info;
    bool ok = false;
    std::string failure;  // human-readable reason when !ok
};

std::mutex g_log_mutex;
std::atomic<size_t> g_done{0};

ProbeOutcome ProbeOne(const std::wstring& exe,
                      const XER::Audio::Plugin::Vst3ModuleCandidate& candidate,
                      size_t total, DWORD timeout_ms, bool in_process) {
    ProbeOutcome outcome;
    outcome.candidate = candidate;
    outcome.info.path = candidate.path;
    outcome.info.module_path = candidate.module_path;

    if (in_process) {
        // Debug/legacy path: probe in this process (a crash kills the scan).
        outcome.ok =
            XER::Audio::Plugin::ProbeVst3Module(candidate.module_path,
                                              &outcome.info);
        if (!outcome.ok) {
            outcome.failure = "no plugin class in factory";
        }
    } else {
        const ProbeRun run = RunProbeProcess(exe, candidate.module_path,
                                             timeout_ms);
        if (run.spawn_failed) {
            outcome.failure = "failed to spawn worker";
        } else if (run.timed_out) {
            outcome.failure = "probe timeout";
        } else if (run.output.find("\"xer_probe\":1") ==
                   std::string::npos) {
            // No JSON at all: the worker died before printing — that IS
            // the crash case (clean exits always emit the marker).
            outcome.failure = run.exit_code != 0
                ? "worker crashed (exit " + std::to_string(run.exit_code) + ")"
                : "worker produced no probe result";
        } else if (run.output.find("\"ok\":true") == std::string::npos) {
            outcome.failure = "no plugin class in factory";
        } else {
            auto get = [&](const char* key) {
                return JsonGetString(run.output, key).value_or("");
            };
            outcome.info.name = get("name");
            outcome.info.vendor = get("vendor");
            outcome.info.category = get("category");
            outcome.info.version = get("version");
            outcome.ok = !outcome.info.name.empty();
            if (!outcome.ok) {
                outcome.failure = "empty plugin name";
            }
        }
    }

    // moduleinfo.json metadata wins over raw factory data — it is the
    // bundle's authoritative declaration of name/vendor/version.
    outcome.info.resources_path = candidate.resources_path;
    if (outcome.ok && candidate.has_module_info) {
        const auto& mi = candidate.module_info;
        if (!mi.name.empty()) outcome.info.name = mi.name;
        if (!mi.vendor.empty()) outcome.info.vendor = mi.vendor;
        if (!mi.version.empty()) outcome.info.version = mi.version;
    }

    const size_t done = g_done.fetch_add(1) + 1;
    std::lock_guard<std::mutex> lk(g_log_mutex);
    std::fprintf(stderr, "[%zu/%zu] %s — %s\n", done, total,
                 outcome.ok ? outcome.info.name.c_str() : "(failed)",
                 outcome.ok ? "ok" : outcome.failure.c_str());
    return outcome;
}

std::string SelfExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(),
                                   static_cast<DWORD>(buf.size()));
    while (len == buf.size()) {  // path longer than MAX_PATH — grow
        buf.resize(buf.size() * 2);
        len = GetModuleFileNameW(nullptr, buf.data(),
                                 static_cast<DWORD>(buf.size()));
    }
    buf.resize(len);
    return XER::Helper::String::WideToUtf8(buf);
}

}  // namespace

int main(int argc, char** argv) {
    // Children inherit this: a crashed plugin must die quietly instead of
    // popping the Windows Error Reporting dialog and stalling the scan.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);

    std::vector<std::string> roots;
    std::string out_path;
    std::string probe_module;
    size_t jobs = std::thread::hardware_concurrency();
    DWORD timeout_ms = 30000;
    bool pretty = false;
    bool in_process = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--pretty") {
            pretty = true;
        } else if (arg == "--probe" && i + 1 < argc) {
            probe_module = argv[++i];
        } else if (arg == "--jobs" && i + 1 < argc) {
            jobs = std::max<size_t>(1, std::atoi(argv[++i]));
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_ms = static_cast<DWORD>(std::max<long>(1000, std::atol(argv[++i])));
        } else if (arg == "--in-process") {
            in_process = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "Usage: EngineScanVST3 [root ...] [--out file.json] [--pretty]\n"
                "                      [--jobs N] [--timeout MS] [--in-process]\n"
                "       EngineScanVST3 --probe <module.vst3>\n");
            return 0;
        } else {
            roots.push_back(arg);
        }
    }

    if (!probe_module.empty()) {
        return RunProbeMode(probe_module);
    }

    if (roots.empty()) {
        roots = XER::Audio::Plugin::DefaultVst3SearchRoots();
    }
    if (jobs == 0) {
        jobs = 4;
    }

    std::fprintf(stderr,
                 "EngineScanVST3: scanning %zu root(s), %zu worker(s), "
                 "timeout %lu ms%s\n",
                 roots.size(), jobs, static_cast<unsigned long>(timeout_ms),
                 in_process ? " (in-process)" : "");
    for (const auto& r : roots) {
        std::fprintf(stderr, "  %s\n", r.c_str());
    }

    auto candidates = XER::Audio::Plugin::EnumerateVst3Modules(roots);
    std::erase_if(candidates, [](const auto& c) {
        return c.module_path.empty();
    });
    std::fprintf(stderr, "EngineScanVST3: %zu module(s) to probe\n",
                 candidates.size());

    const std::wstring self_exe =
        XER::Helper::String::Utf8ToWide(SelfExePath());
    const size_t total = candidates.size();

    std::vector<ProbeOutcome> outcomes;
    {
        XER::AsyncIO::IO::Async::IOThreadPool pool(jobs);
        pool.Start();
        std::vector<std::future<ProbeOutcome>> futures;
        futures.reserve(total);
        for (const auto& candidate : candidates) {
            auto promise =
                std::make_shared<std::promise<ProbeOutcome>>();
            futures.push_back(promise->get_future());
            pool.Enqueue([promise, &self_exe, candidate, total, timeout_ms,
                          in_process]() mutable {
                promise->set_value(ProbeOne(self_exe, candidate, total,
                                            timeout_ms, in_process));
            });
        }
        outcomes.reserve(total);
        for (auto& future : futures) {
            outcomes.push_back(future.get());
        }
        pool.Stop();
    }

    // Build the JSON document — same schema as before plus a failures list.
    std::string json;
    json += "{\"generated_by\":\"EngineScanVST3\",\"roots\":[";
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i) json += ",";
        json += "\"" + JsonEscape(roots[i]) + "\"";
    }
    json += "],\"plugins\":[";
    size_t plugin_count = 0;
    for (const auto& o : outcomes) {
        if (!o.ok) {
            continue;
        }
        const auto& p = o.info;
        if (plugin_count++) json += pretty ? ",\n    " : ",";
        if (pretty && plugin_count == 1) json += "\n    ";
        json += "{\"path\":\"" + JsonEscape(p.path) +
                "\",\"module\":\"" + JsonEscape(p.module_path) +
                "\",\"name\":\"" + JsonEscape(p.name) +
                "\",\"vendor\":\"" + JsonEscape(p.vendor) +
                "\",\"category\":\"" + JsonEscape(p.category) +
                "\",\"version\":\"" + JsonEscape(p.version) +
                "\",\"resources\":\"" + JsonEscape(p.resources_path) +
                "\"}";
    }
    if (pretty && plugin_count > 0) json += "\n  ";
    json += "],\"failures\":[";
    size_t failure_count = 0;
    for (const auto& o : outcomes) {
        if (o.ok) {
            continue;
        }
        if (failure_count++) json += pretty ? ",\n    " : ",";
        if (pretty && failure_count == 1) json += "\n    ";
        json += "{\"path\":\"" + JsonEscape(o.candidate.path) +
                "\",\"module\":\"" + JsonEscape(o.candidate.module_path) +
                "\",\"reason\":\"" + JsonEscape(o.failure) + "\"}";
    }
    if (pretty && failure_count > 0) json += "\n  ";
    json += "]}\n";

    std::fprintf(stderr,
                 "EngineScanVST3: %zu plugin class(es) found, "
                 "%zu module(s) failed\n",
                 plugin_count, failure_count);

    FILE* out = stdout;
    if (!out_path.empty()) {
        out = std::fopen(out_path.c_str(), "wb");
        if (!out) {
            std::fprintf(stderr, "EngineScanVST3: cannot open %s\n",
                         out_path.c_str());
            return 1;
        }
    }
    std::fwrite(json.data(), 1, json.size(), out);
    if (out != stdout) std::fclose(out);
    return 0;
}
