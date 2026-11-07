// Application-level NodeGraph node types:
//  - audio.import / audio.export  — WAV file IO via CodecIO::WavPcmCodec
//  - script.process               — Lua buffer scripting (vendored lua 5.4)
//  - plugin.vst3                  — VST3 effect hosting via Vst3ExternalHost
//
// These register into NodeTypeRegistry::Shared() so they appear in the
// editor palette alongside the built-in nodes.

#include "app/node_graph_app_nodes.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/effects_rack/custom_effect_algorithms.h"
#include "audio/effects_rack/custom_effect_processor.h"
#include "audio/effects_rack/custom_effect_struct.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "audio/plugin_wrappers/vst3_external_host.h"
#include "content/network/http/http_sync_request.h"
#include "content/network/socket/ssl_client_socket.h"
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/websocket/websocket_handshake_stream.h"
#include "content/network/websocket/websocket_stream.h"
#include "content/s3/s3_client.h"
#include "node_graph/node_graph_executor.h"
#include "node_graph/node_graph_registry.h"

#ifdef XER_HAS_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// wingdi.h maps GetObject → GetObjectA; S3Client::GetObject must stay intact.
#ifdef GetObject
#undef GetObject
#endif
#endif

namespace XER::App::NodeGraphApp {
namespace NG = XER::NodeGraph;
namespace {

using VT = NG::ValueType;

NG::PortSpec In(const std::string& name, VT type, NG::Value def = NG::Value()) {
    return {name, type, std::move(def)};
}
NG::PortSpec Out(const std::string& name, VT type) { return {name, type, NG::Value()}; }

NG::NodeTypeDescriptor Make(std::string type, std::string title, std::string category,
                            std::string desc, std::vector<NG::PortSpec> inputs,
                            std::vector<NG::PortSpec> outputs, NG::EvaluateFn fn,
                            std::vector<NG::ParamSpec> params = {}, bool pure = true) {
    NG::NodeTypeDescriptor d;
    d.type = std::move(type);
    d.title = std::move(title);
    d.category = std::move(category);
    d.description = std::move(desc);
    d.inputs = std::move(inputs);
    d.outputs = std::move(outputs);
    d.evaluate = std::move(fn);
    d.params = std::move(params);
    d.pure = pure;
    return d;
}

// ── File IO helpers ────────────────────────────────────────────────────────

bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0);
    out.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size));
}

bool WriteFileBytes(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    return static_cast<bool>(f.write(reinterpret_cast<const char*>(data.data()),
                                     static_cast<std::streamsize>(data.size())));
}

// Deinterleave codec output (interleaved float frames) into channel arrays.
std::vector<NG::Value::Array> Deinterleave(const std::vector<float>& interleaved,
                                           int channels) {
    std::vector<NG::Value::Array> chans(static_cast<std::size_t>(std::max(1, channels)));
    if (channels <= 0 || interleaved.empty()) return chans;
    const std::size_t frames = interleaved.size() / static_cast<std::size_t>(channels);
    for (auto& c : chans) c.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < channels && ch < static_cast<int>(chans.size()); ++ch) {
            chans[static_cast<std::size_t>(ch)][i] =
                interleaved[i * static_cast<std::size_t>(channels) +
                             static_cast<std::size_t>(ch)];
        }
    }
    return chans;
}

NG::Value::Array MonoDownmix(const std::vector<NG::Value::Array>& chans) {
    if (chans.empty()) return {};
    if (chans.size() == 1) return chans.front();
    const std::size_t n = chans.front().size();
    NG::Value::Array out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double acc = 0;
        for (const auto& c : chans) acc += i < c.size() ? c[i] : 0.0;
        out[i] = acc / static_cast<double>(chans.size());
    }
    return out;
}

// ── VST3 host registry ─────────────────────────────────────────────────────

struct Vst3NodeState {
    std::unique_ptr<XER::Engine::Audio::Plugin::Vst3ExternalHost> host;
    std::string loaded_path;
    double sample_rate = 44100.0;
    uint32_t max_block = 8192;
    std::map<std::uint32_t, float> applied_params;
    bool editor_open = false;
};

std::mutex g_vst3_mutex;
std::map<NG::NodeId, Vst3NodeState> g_vst3_hosts;

// Caller must hold g_vst3_mutex — VST3 hosts are not thread-safe and the
// map itself must not be touched concurrently with node evaluation.
Vst3NodeState& Vst3StateFor(NG::NodeId id) {
    return g_vst3_hosts[id];
}

// ── Built-in FX rack effects ───────────────────────────────────────────────

struct FxNodeState {
    std::unique_ptr<XER::Engine::Audio::FX::CustomEffectProcessor> proc;
    std::string signature;
};

std::mutex g_fx_mutex;
std::map<NG::NodeId, FxNodeState> g_fx_states;

FxNodeState& FxStateFor(NG::NodeId id) {
    std::lock_guard<std::mutex> lock(g_fx_mutex);
    return g_fx_states[id];
}

std::string FxTitle(const std::string& algo) {
    std::string t;
    bool up = true;
    for (char ch : algo) {
        if (ch == '_') {
            t += ' ';
            up = true;
            continue;
        }
        t += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))
                : ch;
        up = false;
    }
    return t;
}

// ── Lua scripting ──────────────────────────────────────────────────────────

#ifdef XER_HAS_LUA

struct LuaNodeState {
    lua_State* L = nullptr;
    std::string loaded_source;   // code text or file path marker
    std::string loaded_hash;
    bool script_ok = false;
    std::string error;
};

std::mutex g_lua_mutex;
std::map<NG::NodeId, LuaNodeState> g_lua_states;

std::string HashString(const std::string& s) {
    // FNV-1a — cheap change detection for script reload.
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return std::to_string(h);
}

void PushValue(lua_State* L, const NG::Value& v);

void PushArray(lua_State* L, const NG::Value::Array& arr) {
    lua_createtable(L, static_cast<int>(arr.size()), 0);
    for (std::size_t i = 0; i < arr.size(); ++i) {
        lua_pushnumber(L, arr[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
}

void PushValue(lua_State* L, const NG::Value& v) {
    switch (v.Type()) {
    case VT::kNumber: lua_pushnumber(L, v.AsNumber()); break;
    case VT::kBool: lua_pushboolean(L, v.AsBool()); break;
    case VT::kString: lua_pushlstring(L, v.AsString().data(), v.AsString().size()); break;
    case VT::kArray: PushArray(L, v.AsArray()); break;
    default: lua_pushnil(L); break;
    }
}

NG::Value PopValue(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
    case LUA_TNUMBER: return NG::Value(lua_tonumber(L, idx));
    case LUA_TBOOLEAN: return NG::Value(lua_toboolean(L, idx) != 0);
    case LUA_TSTRING: {
        std::size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        return NG::Value(std::string(s != nullptr ? s : "", len));
    }
    case LUA_TTABLE: {
        NG::Value::Array arr;
        const lua_Integer n = luaL_len(L, idx);
        arr.reserve(static_cast<std::size_t>(n));
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, idx, i);
            if (lua_isnumber(L, -1)) arr.push_back(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        return NG::Value(std::move(arr));
    }
    default: return NG::Value();
    }
}

// Contract: script defines `function process(inputs, params)` where inputs is
// a table {signal=..., x=..., y=..., z=...} and params holds the node's
// parameters. Return value may be a table (assigned back to signal) or a
// number/bool/string assigned to the "result" output.
bool EvalLua(NG::EvalContext& c, const std::string& source_text,
             bool source_is_file) {
    const NG::NodeId id = c.node ? c.node->id : 0;
    std::lock_guard<std::mutex> lock(g_lua_mutex);
    LuaNodeState& st = g_lua_states[id];
    const std::string hash = HashString(source_is_file ? "F:" + source_text : source_text);

    if (st.L == nullptr) {
        st.L = luaL_newstate();
        if (st.L == nullptr) {
            c.error = "lua: failed to create state";
            return false;
        }
        luaL_openlibs(st.L);
    }
    if (!st.script_ok || st.loaded_hash != hash) {
        // (Re)load the script chunk.
        std::string code;
        if (source_is_file) {
            std::ifstream f(source_text, std::ios::binary);
            if (!f) {
                c.error = "lua: cannot open script file: " + source_text;
                return false;
            }
            code.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        } else {
            code = source_text;
        }
        if (luaL_loadbuffer(st.L, code.data(), code.size(), "=nodegraph") != LUA_OK) {
            st.error = lua_tostring(st.L, -1);
            lua_pop(st.L, 1);
            c.error = "lua: " + st.error;
            return false;
        }
        if (lua_pcall(st.L, 0, 0, 0) != LUA_OK) {
            st.error = lua_tostring(st.L, -1);
            lua_pop(st.L, 1);
            c.error = "lua: " + st.error;
            return false;
        }
        st.loaded_hash = hash;
        st.script_ok = true;
        st.error.clear();
    }

    // Build inputs table.
    lua_newtable(st.L);
    for (const char* name : {"signal", "x", "y", "z"}) {
        PushValue(st.L, c.Input(name));
        lua_setfield(st.L, -2, name);
    }
    // params table: scalar node params only.
    lua_newtable(st.L);
    if (c.node) {
        for (const auto& [k, v] : c.node->params) {
            if (v.Type() == VT::kNumber || v.Type() == VT::kBool ||
                v.Type() == VT::kString) {
                PushValue(st.L, v);
                lua_setfield(st.L, -2, k.c_str());
            }
        }
    }

    lua_getglobal(st.L, "process");
    if (!lua_isfunction(st.L, -1)) {
        lua_pop(st.L, 1);
        c.error = "lua: no global 'process' function";
        return false;
    }
    // Stack: inputs params fn  ->  rotate fn below its args: fn inputs params
    lua_insert(st.L, -3);
    const int rc = lua_pcall(st.L, 2, 1, 0);
    if (rc != LUA_OK) {
        st.error = lua_tostring(st.L, -1);
        lua_pop(st.L, 1);
        c.error = "lua: " + st.error;
        return false;
    }
    // Result: number/bool/string -> "result"; table -> back into inputs? Assign
    // the returned table's "signal" field to the signal output if present.
    if (lua_istable(st.L, -1)) {
        lua_getfield(st.L, -1, "signal");
        if (!lua_isnil(st.L, -1)) {
            c.Output("signal", PopValue(st.L, -1));
        }
        lua_pop(st.L, 1);
        lua_getfield(st.L, -1, "result");
        if (!lua_isnil(st.L, -1)) {
            c.Output("result", PopValue(st.L, -1));
        }
        lua_pop(st.L, 1);
    } else {
        c.Output("result", PopValue(st.L, -1));
    }
    lua_pop(st.L, 1);
    return true;
}

#endif  // XER_HAS_LUA

// Registers one fx.<algorithm> node type wrapping CustomEffectProcessor.
// Effect parameters become node params (inspector-editable); the processor
// re-initializes only when the parameter signature changes.
void RegisterFxNode(const std::string& algo) {
    const auto proto =
        XER::Engine::Audio::FX::CreateDefaultCustomEffectNode(algo);
    std::vector<NG::ParamSpec> params;
    params.push_back({"sampleRate", NG::Value(44100.0), {}});
    for (const auto& p : proto.parameters) {
        params.push_back({p.name, NG::Value(static_cast<double>(p.value)), {}});
    }
    NG::NodeTypeRegistry::Shared().Register(Make(
        "fx." + algo, FxTitle(algo), "Effects",
        "Built-in " + FxTitle(algo) +
            " effect (mono buffer; apply per channel for stereo).",
        {In("signal", VT::kArray, NG::Value(NG::Value::Array{}))},
        {Out("signal", VT::kArray)},
        [algo](NG::EvalContext& c) {
            auto sig_in = c.Input("signal").AsArray();
            if (sig_in.empty()) {
                c.Output("signal", std::move(sig_in));
                return true;
            }
            const NG::NodeId id = c.node ? c.node->id : 0;
            const float sr = static_cast<float>(std::clamp(
                c.Param("sampleRate").AsNumber(), 8000.0, 384000.0));

            // Rebuild the effect definition with inspector param overrides.
            auto ce = XER::Engine::Audio::FX::CreateDefaultCustomEffectNode(algo);
            std::string sig = algo + "|" + std::to_string(sr);
            for (auto& p : ce.parameters) {
                if (c.node) {
                    auto it = c.node->params.find(p.name);
                    if (it != c.node->params.end()) {
                        p.value = static_cast<float>(it->second.AsNumber());
                    }
                }
                sig += "|" + p.name + "=" + std::to_string(p.value);
            }
            FxNodeState& st = FxStateFor(id);
            if (st.proc == nullptr) {
                st.proc =
                    std::make_unique<XER::Engine::Audio::FX::CustomEffectProcessor>();
            }
            if (st.signature != sig) {
                std::string err;
                if (!st.proc->Initialize(ce, sr, 4096, &err)) {
                    c.error = "fx." + algo + ": " + err;
                    return false;
                }
                st.signature = sig;
            }
            std::vector<float> in(sig_in.begin(), sig_in.end());
            std::vector<float> out;
            std::string err;
            if (!st.proc->ProcessBuffer(in, 1024, &out, &err)) {
                c.error = "fx." + algo + ": " + err;
                return false;
            }
            c.Output("signal",
                     NG::Value::Array(out.begin(), out.end()));
            return true;
        },
        std::move(params), /*pure=*/false));
}

// Shared VST3 evaluate used by plugin.vst3 and plugin.vst3_view — the view
// node is identical DSP-wise; the editor treats its type as "open the plugin
// GUI automatically after a run".
bool EvalVst3Plugin(NG::EvalContext& c) {
    // VST3 plugins are not thread-safe: serialize load/process/param calls
    // and protect g_vst3_hosts against concurrent node evaluation.
    std::lock_guard<std::mutex> vst3_lock(g_vst3_mutex);
    const NG::NodeId id = c.node ? c.node->id : 0;
    const std::string path = c.Param("path").AsString();
    if (path.empty()) { c.error = "plugin.vst3: empty path"; return false; }
    const double sr = std::max(8000.0, c.Param("sampleRate").AsNumber());
    auto& st = Vst3StateFor(id);
    if (st.host == nullptr) {
        st.host = std::make_unique<XER::Engine::Audio::Plugin::Vst3ExternalHost>();
    }
    if (st.loaded_path != path || !st.host->IsLoaded()) {
        std::string err;
        if (!st.host->Load(path, sr, st.max_block, &err)) {
            c.error = "plugin.vst3: " + err;
            return false;
        }
        st.loaded_path = path;
        st.sample_rate = sr;
        st.applied_params.clear();
    }
    // Apply exposed normalized params when they change.
    for (int i = 0; i < 8; ++i) {
        const std::string pname = "p" + std::to_string(i);
        auto it = c.node->params.find(pname);
        if (it == c.node->params.end()) continue;
        const float v = static_cast<float>(std::clamp(it->second.AsNumber(), 0.0, 1.0));
        const auto cur = st.applied_params.find(static_cast<std::uint32_t>(i));
        if (cur == st.applied_params.end() || cur->second != v) {
            st.host->SetParameter(static_cast<std::uint32_t>(i), v);
            st.applied_params[static_cast<std::uint32_t>(i)] = v;
        }
    }
    auto sig = c.Input("signal").AsArray();
    if (sig.empty()) { c.Output("signal", std::move(sig)); return true; }
    std::vector<float> in(sig.begin(), sig.end());
    std::vector<float> out(in.size());
    if (!st.host->Process(in.data(), out.data(),
                         static_cast<std::uint32_t>(in.size()))) {
        c.error = "plugin.vst3: process failed";
        return false;
    }
    NG::Value::Array result(out.begin(), out.end());
    c.Output("signal", std::move(result));
    return true;
}

// ── Networking (src/content/network + libcurl + OpenSSL) ───────────────────

std::vector<std::string> SplitLines(const std::string& s) {
    auto lines = XER::Engine::Helper::String::SplitLines(s);
    lines.erase(std::remove_if(lines.begin(), lines.end(),
        [](const std::string& line) { return line.empty(); }), lines.end());
    return lines;
}

struct HttpResult {
    long status = 0;
    std::string body;
    std::string error;
    bool ok = false;
};

// Thin adapter over the shared transport in content/network — all curl
// usage lives there (XER::network::http::SyncRequest).
HttpResult HttpRequest(const std::string& method, const std::string& url,
                       const std::string& body,
                       const std::vector<std::string>& headers,
                       long timeout_ms) {
    const auto r = XER::network::http::SyncRequest(method, url, body, headers,
                                              timeout_ms);
    HttpResult out;
    out.status = r.status;
    out.body = std::move(r.body);
    out.error = std::move(r.error);
    out.ok = r.transport_ok;
    return out;
}

// ── WebSocket send (blocking socket on a worker thread, bounded wait) ───────

struct WsSendResult {
    // Written by the worker thread before finished is released; read by the
    // evaluator only after observing finished == true.
    std::atomic<bool> ok{false};
    std::atomic<bool> finished{false};
    std::string error;
};

bool ParseWebSocketUrl(const std::string& url, bool* tls,
                       std::string* host, uint16_t* port,
                       std::string* path) {
    std::string rest;
    if (url.rfind("wss://", 0) == 0) {
        *tls = true;
        rest = url.substr(6);
    } else if (url.rfind("ws://", 0) == 0) {
        *tls = false;
        rest = url.substr(5);
    } else {
        return false;
    }
    const std::size_t slash = rest.find('/');
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    *path = slash == std::string::npos ? "/" : rest.substr(slash);
    const std::size_t colon = authority.rfind(':');
    std::string h = authority;
    uint16_t p = *tls ? 443 : 80;
    if (colon != std::string::npos && colon > 0) {
        h = authority.substr(0, colon);
        try {
            p = static_cast<uint16_t>(std::stoi(authority.substr(colon + 1)));
        } catch (...) {
            return false;
        }
    }
    if (h.empty()) return false;
    *host = h;
    *port = p;
    return true;
}

}  // namespace

void RegisterAppNodes() {
    auto& reg = NG::NodeTypeRegistry::Shared();

    // ── Audio file import ────────────────────────────────────────────────
    reg.Register(Make(
        "audio.import", "Import Audio", "Audio",
        "Loads a WAV file (16-bit PCM). path input or 'path' param; "
        "maxSeconds clamps the decoded region.",
        {In("path", VT::kString, NG::Value(""))},
        {Out("signal", VT::kArray), Out("left", VT::kArray), Out("right", VT::kArray),
         Out("sampleRate", VT::kNumber), Out("channels", VT::kNumber),
         Out("frames", VT::kNumber)},
        [](NG::EvalContext& c) {
            std::string path = c.Input("path").AsString();
            if (path.empty()) path = c.Param("path").AsString();
            if (path.empty()) { c.error = "audio.import: empty path"; return false; }
            std::vector<std::uint8_t> bytes;
            if (!ReadFileBytes(path, bytes)) {
                c.error = "audio.import: cannot read " + path;
                return false;
            }
            XER::Engine::Audio::CodecIO::WavPcmCodec codec;
            std::vector<float> pcm;
            int sr = 0, ch = 0;
            if (!codec.Decode16(bytes.data(), bytes.size(), pcm, &sr, &ch) || pcm.empty()) {
                c.error = "audio.import: decode failed (WAV PCM16 only)";
                return false;
            }
            const double max_sec = std::max(0.01, c.Param("maxSeconds").AsNumber());
            const std::size_t max_frames =
                static_cast<std::size_t>(std::min<double>(
                    pcm.size() / static_cast<std::size_t>(std::max(1, ch)),
                    max_sec * static_cast<double>(std::max(1, sr))));
            const std::size_t keep = max_frames * static_cast<std::size_t>(std::max(1, ch));
            std::vector<float> trimmed(pcm.begin(), pcm.begin() +
                                       static_cast<std::ptrdiff_t>(std::min(keep, pcm.size())));
            auto chans = Deinterleave(trimmed, ch);
            c.Output("signal", MonoDownmix(chans));
            if (chans.size() > 0) c.Output("left", chans[0]);
            if (chans.size() > 1) c.Output("right", chans[1]);
            else c.Output("right", chans.empty() ? NG::Value::Array{} : chans[0]);
            c.Output("sampleRate", static_cast<double>(sr));
            c.Output("channels", static_cast<double>(ch));
            c.Output("frames", static_cast<double>(trimmed.size() / std::max(1, ch)));
            return true;
        },
        {{"path", NG::Value(""), {}}, {"maxSeconds", NG::Value(60.0), {}}},
        /*pure=*/false));

    // ── Audio file export ────────────────────────────────────────────────
    reg.Register(Make(
        "audio.export", "Export Audio", "Audio",
        "Writes a signal array to 16-bit PCM WAV. 'path' input or param.",
        {In("signal", VT::kArray, NG::Value(NG::Value::Array{})),
         In("path", VT::kString, NG::Value(""))},
        {Out("frames", VT::kNumber), Out("ok", VT::kBool)},
        [](NG::EvalContext& c) {
            std::string path = c.Input("path").AsString();
            if (path.empty()) path = c.Param("path").AsString();
            if (path.empty()) { c.error = "audio.export: empty path"; return false; }
            const auto sig = c.Input("signal").AsArray();
            const int sr = static_cast<int>(std::clamp(
                c.Param("sampleRate").AsNumber(), 1000.0, 768000.0));
            std::vector<float> pcm(sig.begin(), sig.end());
            XER::Engine::Audio::CodecIO::WavPcmCodec codec;
            std::vector<std::uint8_t> wav;
            if (!codec.Encode16(pcm.data(), pcm.size(), wav, sr, 1)) {
                c.error = "audio.export: encode failed";
                c.Output("ok", false);
                return true;
            }
            const bool ok = WriteFileBytes(path, wav);
            c.Output("frames", static_cast<double>(pcm.size()));
            c.Output("ok", ok);
            if (!ok) c.error = "audio.export: cannot write " + path;
            return true;
        },
        {{"path", NG::Value(""), {}}, {"sampleRate", NG::Value(44100.0), {}}},
        /*pure=*/false));

    // Stereo export: left/right -> interleaved WAV.
    reg.Register(Make(
        "audio.export_stereo", "Export Stereo", "Audio",
        "Writes left/right arrays to interleaved 16-bit PCM WAV.",
        {In("left", VT::kArray, NG::Value(NG::Value::Array{})),
         In("right", VT::kArray, NG::Value(NG::Value::Array{})),
         In("path", VT::kString, NG::Value(""))},
        {Out("frames", VT::kNumber), Out("ok", VT::kBool)},
        [](NG::EvalContext& c) {
            std::string path = c.Input("path").AsString();
            if (path.empty()) path = c.Param("path").AsString();
            if (path.empty()) { c.error = "audio.export_stereo: empty path"; return false; }
            const auto l = c.Input("left").AsArray();
            const auto rr = c.Input("right").AsArray();
            const std::size_t n = std::max(l.size(), rr.size());
            std::vector<float> pcm(n * 2);
            for (std::size_t i = 0; i < n; ++i) {
                pcm[i * 2] = i < l.size() ? static_cast<float>(l[i]) : 0.0f;
                pcm[i * 2 + 1] = i < rr.size() ? static_cast<float>(rr[i]) : 0.0f;
            }
            const int sr = static_cast<int>(std::clamp(
                c.Param("sampleRate").AsNumber(), 1000.0, 768000.0));
            XER::Engine::Audio::CodecIO::WavPcmCodec codec;
            std::vector<std::uint8_t> wav;
            if (!codec.Encode16(pcm.data(), n, wav, sr, 2)) {
                c.error = "audio.export_stereo: encode failed";
                c.Output("ok", false);
                return true;
            }
            const bool ok = WriteFileBytes(path, wav);
            c.Output("frames", static_cast<double>(n));
            c.Output("ok", ok);
            if (!ok) c.error = "audio.export_stereo: cannot write " + path;
            return true;
        },
        {{"path", NG::Value(""), {}}, {"sampleRate", NG::Value(44100.0), {}}},
        /*pure=*/false));

#ifdef XER_HAS_LUA
    // ── Lua script processing ────────────────────────────────────────────
    reg.Register(Make(
        "script.process", "Lua Script", "Script",
        "Runs a Lua chunk on the audio buffer. Contract:\n"
        "  function process(inputs, params)\n"
        "    inputs.signal -> array of samples (1-based Lua table)\n"
        "    params -> table of node parameters\n"
        "    return { signal = {...}, result = value }\n"
        "Source: 'inline' uses the 'code' param, 'file' loads 'file' path.",
        {In("signal", VT::kArray, NG::Value(NG::Value::Array{})),
         In("x", VT::kAny, NG::Value(0.0)), In("y", VT::kAny, NG::Value(0.0)),
         In("z", VT::kAny, NG::Value(0.0))},
        {Out("signal", VT::kArray), Out("result", VT::kAny)},
        [](NG::EvalContext& c) {
            const std::string mode = c.Param("source").AsString();
            const std::string src = mode == "file" ? c.Param("file").AsString()
                                                   : c.Param("code").AsString();
            if (src.empty()) { c.Output("signal", c.Input("signal")); return true; }
            return EvalLua(c, src, mode == "file");
        },
        {{"source", NG::Value("inline"), {"inline", "file"}},
         {"code", NG::Value("-- process(inputs, params)\nreturn inputs.signal"), {}},
         {"file", NG::Value(""), {}}},
        /*pure=*/false));
#endif

    // ── VST3 plugin node ─────────────────────────────────────────────────
    const std::vector<NG::ParamSpec> vst3_params = {
        {"path", NG::Value(""), {}},
        {"sampleRate", NG::Value(44100.0), {}},
        {"p0", NG::Value(0.0), {}}, {"p1", NG::Value(0.0), {}},
        {"p2", NG::Value(0.0), {}}, {"p3", NG::Value(0.0), {}},
        {"p4", NG::Value(0.0), {}}, {"p5", NG::Value(0.0), {}},
        {"p6", NG::Value(0.0), {}}, {"p7", NG::Value(0.0), {}}};
    reg.Register(Make(
        "plugin.vst3", "VST3 Plugin", "Plugin",
        "Processes the signal through an external VST3 plugin "
        "(mono->stereo bus, first output channel). Params p0..p7 set "
        "normalized plugin parameters by index. Right-click the node -> "
        "Open Plugin Editor for the plugin GUI.",
        {In("signal", VT::kArray, NG::Value(NG::Value::Array{}))},
        {Out("signal", VT::kArray)},
        [](NG::EvalContext& c) { return EvalVst3Plugin(c); },
        vst3_params,
        /*pure=*/false));

    // VST3 View: same DSP, but the editor auto-opens the plugin GUI after
    // a run (the widget scans for this node type).
    reg.Register(Make(
        "plugin.vst3_view", "VST3 View", "Plugin",
        "VST3 plugin node whose editor window opens automatically "
        "inside the graph canvas after Run (Ctrl+R). Same parameters "
        "as plugin.vst3: path, sampleRate, p0..p7.",
        {In("signal", VT::kArray, NG::Value(NG::Value::Array{}))},
        {Out("signal", VT::kArray)},
        [](NG::EvalContext& c) { return EvalVst3Plugin(c); },
        vst3_params,
        /*pure=*/false));

    // ── Network: S3 object storage (content/s3 — AWS SigV4) ──────────────
    // Shared connection params for every net.s3_* / net.send_s3 node.
    const auto s3_conn_params = [] {
        std::vector<NG::ParamSpec> p;
        p.push_back({"endpoint", NG::Value("s3.amazonaws.com"), {}});
        p.push_back({"bucket", NG::Value(""), {}});
        p.push_back({"region", NG::Value("us-east-1"), {}});
        p.push_back({"accessKey", NG::Value(""), {}});
        p.push_back({"secretKey", NG::Value(""), {}});
        p.push_back({"sessionToken", NG::Value(""), {}});
        p.push_back({"pathStyle", NG::Value(0.0), {}});   // /bucket/key URLs
        p.push_back({"useHttp", NG::Value(0.0), {}});     // http instead of https
        p.push_back({"timeoutMs", NG::Value(60000.0), {}});
        return p;
    };
    const auto s3_params = [&](std::vector<NG::ParamSpec> extra) {
        auto p = s3_conn_params();
        p.insert(p.end(), std::make_move_iterator(extra.begin()),
                 std::make_move_iterator(extra.end()));
        return p;
    };
    const auto s3_config_from = [](const NG::EvalContext& c) {
        XER::Content::S3::S3Config cfg;
        cfg.endpoint = c.Param("endpoint").AsString();
        cfg.region = c.Param("region").AsString();
        if (cfg.region.empty()) cfg.region = "us-east-1";
        cfg.access_key = c.Param("accessKey").AsString();
        cfg.secret_key = c.Param("secretKey").AsString();
        cfg.session_token = c.Param("sessionToken").AsString();
        cfg.path_style = c.Param("pathStyle").AsNumber() > 0.5;
        cfg.use_http = c.Param("useHttp").AsNumber() > 0.5;
        cfg.timeout_ms = static_cast<long>(std::clamp(
            c.Param("timeoutMs").AsNumber(), 500.0, 300000.0));
        return cfg;
    };
    const auto s3_need_conn = [](NG::EvalContext& c) -> bool {
        if (c.Param("endpoint").AsString().empty() ||
            c.Param("bucket").AsString().empty() ||
            c.Param("accessKey").AsString().empty() ||
            c.Param("secretKey").AsString().empty()) {
            c.error = "s3: endpoint/bucket/accessKey/secretKey required";
            return false;
        }
        return true;
    };

    // ── Network: HTTP request ────────────────────────────────────────────
    reg.Register(Make(
        "net.http_request", "HTTP Request", "Network",
        "Sends an HTTP request via libcurl. 'url' input or param; 'body' "
        "input is the payload. 'headers' param: one \"Name: value\" per "
        "line. Runs synchronously — keep timeoutMs reasonable.",
        {In("url", VT::kString, NG::Value("")),
         In("body", VT::kString, NG::Value(""))},
        {Out("status", VT::kNumber), Out("body", VT::kString),
         Out("ok", VT::kBool)},
        [](NG::EvalContext& c) {
            std::string url = c.Input("url").AsString();
            if (url.empty()) url = c.Param("url").AsString();
            if (url.empty()) { c.error = "net.http_request: empty url"; return false; }
            std::string method = c.Param("method").AsString();
            if (method.empty()) method = "GET";
            const std::string body = c.Input("body").AsString();
            const long timeout_ms = static_cast<long>(std::clamp(
                c.Param("timeoutMs").AsNumber(), 500.0, 120000.0));
            const auto hdrs = SplitLines(c.Param("headers").AsString());
            const auto r = HttpRequest(method, url, body, hdrs, timeout_ms);
            c.Output("status", static_cast<double>(r.status));
            c.Output("body", r.body);
            c.Output("ok", r.ok);
            if (!r.ok) c.error = "net.http_request: " + r.error;
            return true;
        },
        {{"url", NG::Value(""), {}},
         {"method", NG::Value("GET"), {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"}},
         {"headers", NG::Value(""), {}},
         {"timeoutMs", NG::Value(10000.0), {}}},
        /*pure=*/false));

    // ── Network: WebSocket send ──────────────────────────────────────────
    reg.Register(Make(
        "net.websocket_send", "WebSocket Send", "Network",
        "Opens a WebSocket (ws:// or wss://), sends 'message' as a text "
        "frame and closes. Runs on a worker thread bounded by timeoutMs; "
        "the node waits up to that long for the result.",
        {In("url", VT::kString, NG::Value("")),
         In("message", VT::kString, NG::Value(""))},
        {Out("ok", VT::kBool)},
        [](NG::EvalContext& c) {
            std::string url = c.Input("url").AsString();
            if (url.empty()) url = c.Param("url").AsString();
            if (url.empty()) { c.error = "net.websocket_send: empty url"; return false; }
            const std::string message = c.Input("message").AsString();
            const long timeout_ms = static_cast<long>(std::clamp(
                c.Param("timeoutMs").AsNumber(), 500.0, 60000.0));

            auto res = std::make_shared<WsSendResult>();
            std::thread([res, url, message]() {
                bool tls = false;
                std::string host, path;
                uint16_t port = 0;
                if (!ParseWebSocketUrl(url, &tls, &host, &port, &path)) {
                    res->error = "bad ws(s):// url";
                    res->finished = true;
                    return;
                }
                std::unique_ptr<XER::network::transport::TransportClientSocket> sock;
                if (tls) {
                    sock = std::make_unique<XER::network::socket::SslClientSocket>();
                } else {
                    sock = std::make_unique<XER::network::socket::TcpClientSocket>();
                }
                std::string err;
                const std::string hostport =
                    host + ":" + std::to_string(port);
                if (!sock->Connect(host, port, &err)) {
                    res->error = "connect: " + err;
                    res->finished = true;
                    return;
                }
                const std::string origin =
                    std::string(tls ? "https://" : "http://") + hostport;
                if (!XER::network::websocket::WebSocketHandshakeStream::DoHandshake(
                        sock.get(), hostport, path, origin, &err)) {
                    res->error = "handshake: " + err;
                    res->finished = true;
                    return;
                }
                XER::network::websocket::WebSocketStream ws(std::move(sock));
                if (!ws.SendText(message, &err)) {
                    res->error = "send: " + err;
                } else {
                    res->ok = true;
                }
                ws.Close();
                res->finished = true;
            }).detach();

            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(timeout_ms);
            while (!res->finished.load() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
            if (!res->finished.load()) {
                c.error = "net.websocket_send: timeout";
                c.Output("ok", false);
                return true;
            }
            c.Output("ok", res->ok.load());
            if (!res->ok) c.error = "net.websocket_send: " + res->error;
            return true;
        },
        {{"url", NG::Value(""), {}},
         {"timeoutMs", NG::Value(8000.0), {}}},
        /*pure=*/false));

    // ── Network: S3 object storage ───────────────────────────────────────
    // All S3 nodes share the connection params and go through
    // XER::Content::S3::S3Client (AWS SigV4, path-style, presign support).

    // PUT upload: 'data' input or 'filePath' param wins when set.
    reg.Register(Make(
        "net.send_s3", "S3 Upload", "Network",
        "PUTs an object to an S3-compatible bucket (AWS SigV4). "
        "Payload: 'data' input (string) or 'filePath' param (raw file bytes). "
        "pathStyle=1 gives /bucket/key URLs for MinIO and friends.",
        {In("data", VT::kString, NG::Value(""))},
        {Out("status", VT::kNumber), Out("ok", VT::kBool),
         Out("etag", VT::kString)},
        [&](NG::EvalContext& c) {
            const std::string key = c.Param("key").AsString();
            if (key.empty() || !s3_need_conn(c)) return false;
            XER::Content::S3::S3Client client(s3_config_from(c));
            const std::string bucket = c.Param("bucket").AsString();
            const std::string fpath = c.Param("filePath").AsString();
            std::string content_type = c.Param("contentType").AsString();
            if (content_type.empty()) content_type = "application/octet-stream";
            XER::Content::S3::S3Result r;
            if (!fpath.empty()) {
                r = client.PutObjectFile(bucket, key, fpath, content_type);
            } else {
                const std::string payload = c.Input("data").AsString();
                r = client.PutObject(bucket, key, payload, content_type);
            }
            c.Output("status", static_cast<double>(r.status));
            c.Output("ok", r.ok);
            const auto et = r.headers.find("etag");
            c.Output("etag", et != r.headers.end() ? et->second : "");
            if (!r.ok) {
                c.error = "net.send_s3: " +
                          (r.error.empty() ? "HTTP " + std::to_string(r.status)
                                           : r.error);
            }
            return true;
        },
        s3_params({{"key", NG::Value(""), {}},
                   {"filePath", NG::Value(""), {}},
                   {"contentType", NG::Value("application/octet-stream"), {}}}),
        /*pure=*/false));

    // GET download: 'filePath' param set → writes file; else 'data' output.
    reg.Register(Make(
        "net.s3_get", "S3 Download", "Network",
        "GETs an object from an S3-compatible bucket. 'filePath' param "
        "writes raw bytes to disk; when empty the bytes come out on 'data'.",
        {},
        {Out("data", VT::kString), Out("status", VT::kNumber),
         Out("ok", VT::kBool)},
        [&](NG::EvalContext& c) {
            const std::string key = c.Param("key").AsString();
            if (key.empty() || !s3_need_conn(c)) return false;
            XER::Content::S3::S3Client client(s3_config_from(c));
            const std::string bucket = c.Param("bucket").AsString();
            const std::string fpath = c.Param("filePath").AsString();
            XER::Content::S3::S3Result r;
            if (!fpath.empty()) {
                r = client.GetObjectToFile(bucket, key, fpath);
            } else {
                std::string data;
                r = client.GetObject(bucket, key, &data);
                c.Output("data", data);
            }
            c.Output("status", static_cast<double>(r.status));
            c.Output("ok", r.ok);
            if (!r.ok) {
                c.error = "net.s3_get: " +
                          (r.error.empty() ? "HTTP " + std::to_string(r.status)
                                           : r.error);
            }
            return true;
        },
        s3_params({{"key", NG::Value(""), {}},
                   {"filePath", NG::Value(""), {}}}),
        /*pure=*/false));

    // HEAD object: existence + size/etag metadata.
    reg.Register(Make(
        "net.s3_head", "S3 Head", "Network",
        "HEADs an object: ok=1 when it exists (HTTP 200). Also reports "
        "size and etag from response headers.",
        {},
        {Out("ok", VT::kBool), Out("status", VT::kNumber),
         Out("size", VT::kNumber), Out("etag", VT::kString)},
        [&](NG::EvalContext& c) {
            const std::string key = c.Param("key").AsString();
            if (key.empty() || !s3_need_conn(c)) return false;
            XER::Content::S3::S3Client client(s3_config_from(c));
            const auto r = client.HeadObject(c.Param("bucket").AsString(), key);
            c.Output("status", static_cast<double>(r.status));
            c.Output("ok", r.ok);
            const auto cl = r.headers.find("content-length");
            c.Output("size", cl != r.headers.end()
                                ? std::strtod(cl->second.c_str(), nullptr)
                                : 0.0);
            const auto et = r.headers.find("etag");
            c.Output("etag", et != r.headers.end() ? et->second : "");
            return true;
        },
        s3_params({{"key", NG::Value(""), {}}}),
        /*pure=*/false));

    // DELETE object.
    reg.Register(Make(
        "net.s3_delete", "S3 Delete", "Network",
        "DELETEs an object from an S3-compatible bucket (204 = removed).",
        {},
        {Out("ok", VT::kBool), Out("status", VT::kNumber)},
        [&](NG::EvalContext& c) {
            const std::string key = c.Param("key").AsString();
            if (key.empty() || !s3_need_conn(c)) return false;
            XER::Content::S3::S3Client client(s3_config_from(c));
            const auto r = client.DeleteObject(c.Param("bucket").AsString(), key);
            c.Output("status", static_cast<double>(r.status));
            c.Output("ok", r.ok);
            if (!r.ok) {
                c.error = "net.s3_delete: " +
                          (r.error.empty() ? "HTTP " + std::to_string(r.status)
                                           : r.error);
            }
            return true;
        },
        s3_params({{"key", NG::Value(""), {}}}),
        /*pure=*/false));

    // ListObjectsV2: 'keys' = one key per line, 'count', 'truncated'.
    reg.Register(Make(
        "net.s3_list", "S3 List", "Network",
        "Lists objects in a bucket (ListObjectsV2). 'prefix' filters keys; "
        "'maxKeys' caps the total (S3 pages of 1000 are followed). "
        "Output 'keys' is newline-separated.",
        {},
        {Out("keys", VT::kString), Out("count", VT::kNumber),
         Out("truncated", VT::kBool), Out("ok", VT::kBool),
         Out("status", VT::kNumber)},
        [&](NG::EvalContext& c) {
            if (!s3_need_conn(c)) return false;
            XER::Content::S3::S3Client client(s3_config_from(c));
            const int max_keys = static_cast<int>(std::clamp(
                c.Param("maxKeys").AsNumber(), 1.0, 10000.0));
            std::vector<XER::Content::S3::S3ObjectMeta> objects;
            bool truncated = false;
            const auto r = client.ListObjectsV2(
                c.Param("bucket").AsString(), c.Param("prefix").AsString(),
                max_keys, &objects, &truncated);
            std::string keys;
            for (const auto& o : objects) {
                if (!keys.empty()) keys += '\n';
                keys += o.key;
            }
            c.Output("keys", keys);
            c.Output("count", static_cast<double>(objects.size()));
            c.Output("truncated", truncated);
            c.Output("status", static_cast<double>(r.status));
            c.Output("ok", r.ok);
            if (!r.ok) {
                c.error = "net.s3_list: " +
                          (r.error.empty() ? "HTTP " + std::to_string(r.status)
                                           : r.error);
            }
            return true;
        },
        s3_params({{"prefix", NG::Value(""), {}},
                   {"maxKeys", NG::Value(200.0), {}}}),
        /*pure=*/false));

    // Presigned GET URL — shareable link, no further auth needed.
    reg.Register(Make(
        "net.s3_presign", "S3 Presign", "Network",
        "Builds a presigned GET URL (SigV4 query auth) valid for "
        "'expiresSec' seconds (max 7 days). Hand the 'url' output to "
        "net.http_request or an external downloader.",
        {},
        {Out("url", VT::kString), Out("ok", VT::kBool)},
        [&](NG::EvalContext& c) {
            const std::string key = c.Param("key").AsString();
            if (key.empty() || !s3_need_conn(c)) return false;
            XER::Content::S3::S3Client client(s3_config_from(c));
            const long expires = static_cast<long>(std::clamp(
                c.Param("expiresSec").AsNumber(), 60.0, 604800.0));
            c.Output("url", client.PresignGetUrl(
                                 c.Param("bucket").AsString(), key, expires));
            c.Output("ok", true);
            return true;
        },
        s3_params({{"key", NG::Value(""), {}},
                   {"expiresSec", NG::Value(3600.0), {}}}),
        /*pure=*/false));

    // ── Built-in FX rack effects as graph nodes ──────────────────────────
    // One fx.<algorithm> node type per built-in effect; "clap_plugin" is
    // covered by plugin.vst3 / plugin.clap paths instead.
    for (const auto& algo : XER::Engine::Audio::FX::ListCustomEffectAlgorithms()) {
        if (algo == "clap_plugin") continue;
        RegisterFxNode(algo);
    }
}

bool OpenPluginEditor(std::uint64_t node_id, void* parent_hwnd,
                      float canvas_x, float canvas_y,
                      float canvas_w, float canvas_h,
                      std::string* error_out) {
#ifdef _WIN32
    if (parent_hwnd == nullptr) {
        if (error_out) *error_out = "no parent window";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_vst3_mutex);
    auto it = g_vst3_hosts.find(node_id);
    if (it == g_vst3_hosts.end() || it->second.host == nullptr ||
        !it->second.host->IsLoaded()) {
        if (error_out) *error_out = "plugin not loaded — run the graph first";
        return false;
    }
    Vst3NodeState* st = &it->second;
    if (!st->host->HasEditorView() && !st->host->CreateEditorView(error_out)) {
        return false;
    }
    const float dpi = static_cast<float>(GetDpiForWindow(
        static_cast<HWND>(parent_hwnd))) / 96.0f;
    const int l = static_cast<int>(canvas_x * dpi);
    const int t = static_cast<int>(canvas_y * dpi);
    const int r = static_cast<int>((canvas_x + canvas_w) * dpi);
    const int b = static_cast<int>((canvas_y + canvas_h) * dpi);
    if (!st->host->AttachEditorView(parent_hwnd, l, t, r, b, error_out)) {
        return false;
    }
    st->editor_open = true;
    return true;
#else
    (void)node_id; (void)parent_hwnd;
    (void)canvas_x; (void)canvas_y; (void)canvas_w; (void)canvas_h;
    if (error_out) *error_out = "plugin editors are Windows-only";
    return false;
#endif
}

void CloseAllPluginEditors() {
    std::lock_guard<std::mutex> lock(g_vst3_mutex);
    for (auto& [id, st] : g_vst3_hosts) {
        if (st.host) {
            st.host->RemoveEditorView();
            st.editor_open = false;
        }
    }
#ifdef XER_HAS_LUA
    // EvalLua holds g_lua_mutex for the whole lua_pcall — take the same
    // lock so a state is never closed while a node is evaluating it.
    {
        std::lock_guard<std::mutex> lua_lock(g_lua_mutex);
        for (auto& [id, st] : g_lua_states) {
            if (st.L) {
                lua_close(st.L);
                st.L = nullptr;
            }
        }
        g_lua_states.clear();
    }
#endif
}

}  // namespace XER::App::NodeGraphApp
