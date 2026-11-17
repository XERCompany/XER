// shell_commands.cc — see shell_commands.h. Registry + built-ins are
// CEF-free (they compile and are testable without XER_HAS_CEF); only
// the process-message plumbing at the bottom needs CEF.
#include "shell_commands.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include <json/json.h>

#include "network/net_client.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(XER_HAS_CEF)
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#endif

namespace XER::Cef {
namespace {

std::mutex g_mutex;
std::map<std::string, ShellCommandHandler> g_commands;
ShellCommandHandler g_host_hook;

// ---- JSON helpers -----------------------------------------------------

Json::Value ParseArgs(const std::string& args_json) {
    if (args_json.empty()) {
        return Json::Value(Json::objectValue);
    }
    Json::Value v;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(args_json);
    if (!Json::parseFromStream(rb, ss, &v, &errs)) {
        Json::Value err(Json::objectValue);
        err["__parse_error"] = errs;
        return err;
    }
    return v;
}

std::string WriteJson(const Json::Value& v) {
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string OkJson() {
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    return WriteJson(v);
}

std::string ErrJson(const std::string& what) {
    Json::Value v(Json::objectValue);
    v["ok"] = false;
    v["error"] = what;
    return WriteJson(v);
}

std::string ArgStr(const Json::Value& args, const char* key,
                   const std::string& def = {}) {
    const Json::Value& v = args[key];
    return v.isString() ? v.asString() : def;
}
long ArgLong(const Json::Value& args, const char* key, long def) {
    const Json::Value& v = args[key];
    return v.isIntegral() ? static_cast<long>(v.asInt64()) : def;
}

// ---- built-in commands -------------------------------------------------
// Each returns a JSON object string. Handlers run on worker threads —
// blocking (net.fetch) is expected.

std::string NetResponseJson(const XER::Network::NetResponse& r) {
    Json::Value v(Json::objectValue);
    v["ok"] = r.ok;
    v["status"] = static_cast<Json::Int64>(r.status);
    v["body"] = r.body;
    if (!r.error.empty()) v["error"] = r.error;
    Json::Value h(Json::objectValue);
    for (const auto& [k, val] : r.headers) h[k] = val;
    v["headers"] = h;
    return WriteJson(v);
}

// net.get {url, timeout_ms?}
std::string CmdNetGet(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string url = ArgStr(a, "url");
    if (url.empty()) return ErrJson("net.get: 'url' required");
    return NetResponseJson(
        XER::Network::Get(url, ArgLong(a, "timeout_ms", 60000)));
}

// net.fetch {url, method?, body?, headers?: [..], timeout_ms?}
std::string CmdNetFetch(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string url = ArgStr(a, "url");
    if (url.empty()) return ErrJson("net.fetch: 'url' required");
    std::vector<std::string> headers;
    for (const auto& h : a["headers"]) {
        if (h.isString()) headers.push_back(h.asString());
    }
    return NetResponseJson(XER::Network::Fetch(
        ArgStr(a, "method", "GET"), url, ArgStr(a, "body"),
        headers, ArgLong(a, "timeout_ms", 60000)));
}

// net.download {url, path, timeout_ms?}
std::string CmdNetDownload(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string url = ArgStr(a, "url");
    const std::string path = ArgStr(a, "path");
    if (url.empty() || path.empty()) {
        return ErrJson("net.download: 'url' and 'path' required");
    }
    std::string err;
    if (!XER::Network::DownloadFile(
            url, path, ArgLong(a, "timeout_ms", 120000), &err)) {
        return ErrJson(err);
    }
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    v["path"] = path;
    std::error_code ec;
    v["size"] = static_cast<Json::Int64>(
        std::filesystem::file_size(path, ec));
    return WriteJson(v);
}

// fs.read {path} — text files up to 16 MB.
std::string CmdFsRead(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string path = ArgStr(a, "path");
    if (path.empty()) return ErrJson("fs.read: 'path' required");
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return ErrJson("fs.read: " + ec.message());
    if (size > 16u * 1024 * 1024) {
        return ErrJson("fs.read: file exceeds 16 MB limit");
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) return ErrJson("fs.read: cannot open " + path);
    std::string text(size, '\0');
    f.read(text.data(), static_cast<std::streamsize>(size));
    text.resize(static_cast<std::size_t>(f.gcount()));
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    v["text"] = text;
    return WriteJson(v);
}

// fs.write {path, text}
std::string CmdFsWrite(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string path = ArgStr(a, "path");
    if (path.empty()) return ErrJson("fs.write: 'path' required");
    const std::string text = ArgStr(a, "text");
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return ErrJson("fs.write: cannot open " + path);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) return ErrJson("fs.write: write failed");
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    v["size"] = static_cast<Json::Int64>(text.size());
    return WriteJson(v);
}

// fs.exists {path}
std::string CmdFsExists(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string path = ArgStr(a, "path");
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    std::error_code ec;
    v["exists"] = !path.empty() &&
                  std::filesystem::exists(path, ec);
    return WriteJson(v);
}

// fs.list {path} — non-recursive directory listing.
std::string CmdFsList(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string path = ArgStr(a, "path", ".");
    std::error_code ec;
    Json::Value entries(Json::arrayValue);
    for (const auto& de :
         std::filesystem::directory_iterator(path, ec)) {
        Json::Value e(Json::objectValue);
        e["name"] = de.path().filename().string();
        e["type"] = de.is_directory(ec)    ? "dir"
                    : de.is_regular_file(ec) ? "file"
                                             : "other";
        std::error_code ec2;
        e["size"] = static_cast<Json::Int64>(
            de.is_regular_file(ec2)
                ? static_cast<long long>(de.file_size(ec2))
                : 0);
        entries.append(e);
        if (entries.size() >= 10000) break;   // pathological dirs
    }
    if (ec) return ErrJson("fs.list: " + ec.message());
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    v["entries"] = entries;
    return WriteJson(v);
}

// fs.mkdir {path}
std::string CmdFsMkdir(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string path = ArgStr(a, "path");
    if (path.empty()) return ErrJson("fs.mkdir: 'path' required");
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) return ErrJson("fs.mkdir: " + ec.message());
    return OkJson();
}

// fs.remove {path} — single file or empty directory only.
std::string CmdFsRemove(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string path = ArgStr(a, "path");
    if (path.empty()) return ErrJson("fs.remove: 'path' required");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) return ErrJson("fs.remove: " + ec.message());
    return OkJson();
}

// sys.info {}
std::string CmdSysInfo(const std::string&) {
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    v["os"] = "windows";
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    v["cpus"] = static_cast<int>(si.dwNumberOfProcessors);
    v["arch"] = si.wProcessorArchitecture ==
                        PROCESSOR_ARCHITECTURE_AMD64
                    ? "x64"
                    : si.wProcessorArchitecture ==
                              PROCESSOR_ARCHITECTURE_ARM64
                          ? "arm64"
                          : "other";
    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) {
        v["memory_mb"] = static_cast<Json::Int64>(
            ms.ullTotalPhys / (1024 * 1024));
        v["memory_free_mb"] = static_cast<Json::Int64>(
            ms.ullAvailPhys / (1024 * 1024));
    }
    wchar_t host[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(host, &n)) {
        char buf[256]{};
        WideCharToMultiByte(CP_UTF8, 0, host, -1, buf, sizeof buf,
                            nullptr, nullptr);
        v["host"] = buf;
    }
#endif
    return WriteJson(v);
}

// sys.env {name}
std::string CmdSysEnv(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string name = ArgStr(a, "name");
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    const char* val = name.empty() ? nullptr : std::getenv(name.c_str());
    v["value"] = val != nullptr ? val : "";
    return WriteJson(v);
}

// xer.log <text> — fire-and-forget page logging to the debug console.
std::string CmdXerLog(const std::string& args_json) {
    const Json::Value a = ParseArgs(args_json);
    const std::string text =
        a.isString() ? a.asString()
                     : a.isObject() ? ArgStr(a, "text") : args_json;
#if defined(_WIN32)
    OutputDebugStringA(("[xer] " + text + "\n").c_str());
#endif
    return OkJson();
}

// xer.version {}
std::string CmdXerVersion(const std::string&) {
    Json::Value v(Json::objectValue);
    v["ok"] = true;
    v["product"] = "XERBrowserShell";
    v["version"] = "1.0";
    return WriteJson(v);
}

// editor.* — forwarded to the host-app hook when registered.
std::string CmdEditorPassthrough(const std::string& name,
                                 const std::string& args_json) {
    ShellCommandHandler hook;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        hook = g_host_hook;
    }
    if (!hook) {
        return ErrJson("editor.* called but the host app registered "
                       "no shell hook");
    }
    Json::Value a = ParseArgs(args_json);
    Json::Value wrapped(Json::objectValue);
    wrapped["action"] = name;
    wrapped["args"] = a;
    return hook(WriteJson(wrapped));
}

struct Registrar {
    Registrar() { RegisterBuiltinShellCommands(); }
};
Registrar g_registrar;

}  // namespace

void RegisterShellCommand(const std::string& name,
                          ShellCommandHandler handler) {
    if (name.empty()) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (handler) {
        g_commands[name] = std::move(handler);
    } else {
        g_commands.erase(name);
    }
}

void UnregisterShellCommand(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_commands.erase(name);
}

ShellCommandHandler FindShellCommand(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_mutex);
    const auto it = g_commands.find(name);
    return it == g_commands.end() ? nullptr : it->second;
}

void SetShellHostHook(ShellCommandHandler hook) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_host_hook = std::move(hook);
}

void RegisterBuiltinShellCommands() {
    RegisterShellCommand("xer.version", &CmdXerVersion);
    RegisterShellCommand("xer.log", &CmdXerLog);
    RegisterShellCommand("net.get", &CmdNetGet);
    RegisterShellCommand("net.fetch", &CmdNetFetch);
    RegisterShellCommand("net.download", &CmdNetDownload);
    RegisterShellCommand("fs.read", &CmdFsRead);
    RegisterShellCommand("fs.write", &CmdFsWrite);
    RegisterShellCommand("fs.exists", &CmdFsExists);
    RegisterShellCommand("fs.list", &CmdFsList);
    RegisterShellCommand("fs.mkdir", &CmdFsMkdir);
    RegisterShellCommand("fs.remove", &CmdFsRemove);
    RegisterShellCommand("sys.info", &CmdSysInfo);
    RegisterShellCommand("sys.env", &CmdSysEnv);
}

#if defined(XER_HAS_CEF)
namespace {

// Replies to one pending xer.call — runs on the CEF UI thread.
void SendShellResult(CefRefPtr<CefFrame> frame, int id, bool ok,
                     CefString payload) {
    CefRefPtr<CefProcessMessage> msg =
        CefProcessMessage::Create("xer_result");
    CefRefPtr<CefListValue> a = msg->GetArgumentList();
    a->SetInt(0, id);
    a->SetBool(1, ok);
    a->SetString(2, payload);
    frame->SendProcessMessage(PID_RENDERER, msg);
}

}  // namespace

bool HandleShellMessage(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefProcessMessage> message) {
    if (message->GetName() != "xer_call") {
        return false;
    }
    CefRefPtr<CefListValue> a = message->GetArgumentList();
    if (!a || a->GetSize() < 3) {
        return true;   // malformed — swallow, nothing to reply on
    }
    const int id = a->GetInt(0);
    const std::string command = a->GetString(1).ToString();
    const std::string args = a->GetString(2).ToString();

    ShellCommandHandler handler = FindShellCommand(command);
    const bool editor_cmd = command.rfind("editor.", 0) == 0;
    if (!handler && !editor_cmd) {
        CefPostTask(TID_UI,
                    base::BindOnce(&SendShellResult, frame, id, false,
                                   ErrJson("unknown command: " + command)));
        return true;
    }

    // Blocking work (net.fetch, fs.read, ...) runs off the UI thread;
    // the reply is posted back. frame is a CefRefPtr — safe to hold.
    std::thread([frame, id, command, args, handler, editor_cmd] {
        std::string result;
        try {
            result = editor_cmd
                         ? CmdEditorPassthrough(command, args)
                         : handler(args);
        } catch (const std::exception& e) {
            result = ErrJson(std::string("handler threw: ") + e.what());
        } catch (...) {
            result = ErrJson("handler threw");
        }
        CefPostTask(TID_UI, base::BindOnce(&SendShellResult, frame, id,
                                           true, result));
    }).detach();
    return true;
}
#endif  // XER_HAS_CEF

}  // namespace XER::Cef
