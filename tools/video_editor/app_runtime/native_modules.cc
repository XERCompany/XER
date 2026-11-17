// Implements the native modules runtime.js reaches via NativeImport():
// app_super_winkernel (format / SiEnvironmentMatchPath / GetEnvVar),
// app_super_concat (handle / of) and app_super_net (fetch / download),
// plus the %ENV% path expander and RegisterAll which installs
// everything on the engine.
#include "native_modules.h"

#include "js_engine.h"
#include "network/net_client.h"

#include <json/json.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace XER::VideoEditor::AppRuntime {
namespace {

std::string ExeDirUtf8() {
    static const std::string dir = [] {
#ifdef _WIN32
        std::wstring wide(32768, L'\0');
        DWORD n = GetModuleFileNameW(nullptr, wide.data(),
                                     static_cast<DWORD>(wide.size()));
        if (n == 0 || n >= wide.size()) return std::string();
        wide.resize(n);
        size_t slash = wide.find_last_of(L"\\/");
        if (slash != std::wstring::npos) wide.resize(slash);
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                      static_cast<int>(wide.size()), nullptr,
                                      0, nullptr, nullptr);
        if (len <= 0) return std::string();
        std::string utf8(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                            static_cast<int>(wide.size()), utf8.data(), len,
                            nullptr, nullptr);
        for (char& c : utf8)
            if (c == '\\') c = '/';
        return utf8;
#else
        char buf[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return std::string();
        buf[n] = '\0';
        std::string path(buf);
        size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string()
                                          : path.substr(0, slash);
#endif
    }();
    return dir;
}

std::string ResolveEnvToken(const std::string& token) {
    if (token == "WEAKLY_ROOT_PATH_EXECUTE") return ExeDirUtf8();
    const char* value = std::getenv(token.c_str());
    return value ? std::string(value) : std::string();
}

std::string ArgToString(const OutValue& v) {
    switch (v.kind) {
        case OutValue::Kind::String:
            return v.str;
        case OutValue::Kind::Number: {
            if (v.num > -9.007199254740992e15 &&
                v.num < 9.007199254740992e15) {
                long long i = static_cast<long long>(v.num);
                if (static_cast<double>(i) == v.num)
                    return std::to_string(i);
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", v.num);
            return buf;
        }
        case OutValue::Kind::Bool:
            return v.boolean ? "true" : "false";
        case OutValue::Kind::Handler:
            return "[object Function]";
        case OutValue::Kind::Undefined:
        default:
            return "undefined";
    }
}

// Same escaping rules as js_engine.cc's JsonEscapeInto — that helper is
// file-local there, so module code keeps its own copy.
void JsonEscapeInto(std::string& out, const std::string& s) {
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(
                                      static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
}

std::string ExtractJsonStringField(const std::string& json,
                                   const std::string& field) {
    std::string key = "\"" + field + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + key.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

void BuildWinkernel(JsEngine& engine) {
    int obj = engine.NewObject();
    if (obj < 0) return;
    engine.DefineFunction(obj, "format",
                          [](const std::vector<OutValue>& args) {
                              std::string out;
                              for (const OutValue& a : args)
                                  out += ArgToString(a);
                              return StrVal(out);
                          });
    engine.DefineFunction(obj, "SiEnvironmentMatchPath",
                          [](const std::vector<OutValue>& args) {
                              return StrVal(args.size() > 1
                                                ? ExpandEnvPath(args[1].str)
                                                : std::string());
                          });
    engine.DefineFunction(obj, "GetEnvVar",
                          [](const std::vector<OutValue>& args) {
                              return StrVal(args.empty()
                                                ? std::string()
                                                : ResolveEnvToken(args[0].str));
                          });
}

void BuildConcat(JsEngine& engine) {
    int obj = engine.NewObject();
    if (obj < 0) return;
    engine.DefineFunction(obj, "handle",
                          [](const std::vector<OutValue>& args) {
                              std::string sep;
                              if (!args.empty()) {
                                  const std::string& s = args[0].str;
                                  if (args[0].kind == OutValue::Kind::Json ||
                                      (!s.empty() && s.front() == '{'))
                                      sep = ExtractJsonStringField(s, "join");
                                  else if (args[0].kind ==
                                           OutValue::Kind::String)
                                      sep = s;
                              }
                              std::string out;
                              for (size_t i = 1; i < args.size(); ++i) {
                                  if (i > 1) out += sep;
                                  out += ArgToString(args[i]);
                              }
                              return StrVal(out);
                          });
    engine.DefineFunction(obj, "of",
                          [](const std::vector<OutValue>& args) {
                              std::string out;
                              for (const OutValue& a : args)
                                  out += ArgToString(a);
                              return StrVal(out);
                          });
}

// fetch(url) / fetch(url, {method, body, headers, timeout_ms}) — the
// opts object arrives marshalled as raw JSON text in OutValue::str.
// headers accepts either {"Name": "value"} or ["Name: value", ...].
void ApplyFetchOptions(const OutValue& opts, std::string* method,
                       std::string* body, long* timeout_ms,
                       std::vector<std::string>* headers) {
    if (opts.kind != OutValue::Kind::Json || opts.str.empty()) return;
    Json::Value root;
    std::string errs;
    const std::unique_ptr<Json::CharReader> reader(
        Json::CharReaderBuilder().newCharReader());
    if (!reader->parse(opts.str.data(), opts.str.data() + opts.str.size(),
                       &root, &errs) ||
        !root.isObject()) {
        return;  // malformed opts: keep the GET defaults, don't fail the call
    }
    if (root["method"].isString()) *method = root["method"].asString();
    if (root["body"].isString()) *body = root["body"].asString();
    if (root["timeout_ms"].isNumeric())
        *timeout_ms = static_cast<long>(root["timeout_ms"].asInt64());
    const Json::Value& h = root["headers"];
    if (h.isObject()) {
        for (const std::string& name : h.getMemberNames()) {
            headers->push_back(name + ": " + h[name].asString());
        }
    } else if (h.isArray()) {
        for (const Json::Value& item : h) {
            if (item.isString()) headers->push_back(item.asString());
        }
    }
}

// {"status":N,"body":"...","headers":{...},"ok":B,"error":"..."}
OutValue FetchJsonResult(const XER::Network::NetResponse& r) {
    std::string json = "{\"status\":" + std::to_string(r.status) +
                       ",\"body\":\"";
    JsonEscapeInto(json, r.body);
    json += "\",\"headers\":{";
    bool first = true;
    for (const auto& [name, value] : r.headers) {
        if (!first) json += ',';
        first = false;
        json += '"';
        JsonEscapeInto(json, name);
        json += "\":\"";
        JsonEscapeInto(json, value);
        json += '"';
    }
    json += "},\"ok\":";
    json += r.ok ? "true" : "false";
    json += ",\"error\":\"";
    JsonEscapeInto(json, r.error);
    json += "\"}";
    OutValue out;
    out.kind = OutValue::Kind::Json;
    out.str = std::move(json);
    return out;
}

// {"ok":true,"path":"..."} or {"ok":false,"error":"..."}
OutValue DownloadJsonResult(const std::string& path, bool ok,
                            const std::string& error) {
    std::string json = "{\"ok\":";
    json += ok ? "true,\"path\":\"" : "false,\"error\":\"";
    JsonEscapeInto(json, ok ? path : error);
    json += "\"}";
    OutValue out;
    out.kind = OutValue::Kind::Json;
    out.str = std::move(json);
    return out;
}

void BuildNet(JsEngine& engine) {
    int obj = engine.NewObject();
    if (obj < 0) return;
    // Blocking fetch — SyncRequest bounds the whole exchange by
    // timeout_ms, so a script can stall at worst that long on the UI
    // thread. Callers that need concurrency should queue their own
    // worker instead of growing async machinery here.
    engine.DefineFunction(
        obj, "fetch",
        [](const std::vector<OutValue>& args) {
            const std::string url =
                args.empty() ? std::string() : args[0].str;
            std::string method = "GET";
            std::string body;
            long timeout_ms = 60000;
            std::vector<std::string> headers;
            if (args.size() > 1) {
                ApplyFetchOptions(args[1], &method, &body, &timeout_ms,
                                  &headers);
            }
            return FetchJsonResult(XER::Network::Fetch(
                method, url, body, headers, timeout_ms));
        });
    engine.DefineFunction(
        obj, "download",
        [](const std::vector<OutValue>& args) {
            const std::string url =
                args.empty() ? std::string() : args[0].str;
            const std::string path =
                args.size() > 1 ? args[1].str : std::string();
            long timeout_ms = 120000;
            if (args.size() > 2 &&
                args[2].kind == OutValue::Kind::Number) {
                timeout_ms = static_cast<long>(args[2].num);
            }
            std::string error;
            const bool ok = XER::Network::DownloadFile(url, path,
                                                       timeout_ms, &error);
            return DownloadJsonResult(path, ok, error);
        });
}

}  // namespace

std::string ExpandEnvPath(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    size_t pos = 0;
    while (pos < input.size()) {
        size_t open = input.find('%', pos);
        if (open == std::string::npos) {
            out.append(input, pos, std::string::npos);
            break;
        }
        size_t close = input.find('%', open + 1);
        if (close == std::string::npos || close == open + 1) {
            out.append(input, pos, open + 1 - pos);
            pos = open + 1;
            continue;
        }
        out.append(input, pos, open - pos);
        out += ResolveEnvToken(input.substr(open + 1, close - open - 1));
        pos = close + 1;
    }
    return out;
}

void RegisterAll(JsEngine& engine) {
    engine.RegisterModule("app_super_winkernel", BuildWinkernel);
    engine.RegisterModule("app_super_concat", BuildConcat);
    engine.RegisterModule("app_super_net", BuildNet);
    RegisterExportAppData(engine);
}

}  // namespace XER::VideoEditor::AppRuntime
