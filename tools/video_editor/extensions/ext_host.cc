// ext_host.cc — see ext_host.h for the contract.
#include "ext_host.h"

#include "app_runtime/js_engine.h"
#include "app_runtime/native_modules.h"
#include "cef/cef_ext_scheme.h"
#include "script_host.h"
#include "tz/tz.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

namespace XER::VideoEditor {
namespace {

namespace apprt = AppRuntime;

// ---- minimal TOML ------------------------------------------------------
// The extension manifest only needs flat [section] tables, string and
// string-array values. A full TOML parser would pull a dependency for
// no benefit; anything outside this surface fails parsing so a clever
// manifest can't smuggle semantics past the verifier.

std::string TomlUnquote(const std::string& s, bool* ok) {
    *ok = s.size() >= 2 && s.front() == '"' && s.back() == '"';
    if (!*ok) return {};
    std::string out;
    out.reserve(s.size());
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 2 < s.size()) {
            const char n = s[i + 1];
            if (n == '"' || n == '\\') { out.push_back(n); ++i; continue; }
            if (n == 'n') { out.push_back('\n'); ++i; continue; }
            if (n == 't') { out.push_back('\t'); ++i; continue; }
        }
        out.push_back(c);
    }
    return out;
}

bool TomlParseArray(const std::string& s,
                    std::vector<std::string>* out) {
    std::string t = s;
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') {
        return false;
    }
    t = t.substr(1, t.size() - 2);
    // Split on top-level commas honoring quotes.
    out->clear();
    std::string tok;
    bool q = false;
    for (const char c : t) {
        if (c == '"') q = !q;
        if (c == ',' && !q) {
            const auto b = tok.find_first_not_of(" \t");
            const auto e = tok.find_last_not_of(" \t");
            if (b != std::string::npos) {
                bool ok = false;
                auto v = TomlUnquote(tok.substr(b, e - b + 1), &ok);
                if (!ok) return false;
                out->push_back(std::move(v));
            }
            tok.clear();
            continue;
        }
        tok.push_back(c);
    }
    const auto b = tok.find_first_not_of(" \t");
    const auto e = tok.find_last_not_of(" \t");
    if (b != std::string::npos) {
        bool ok = false;
        auto v = TomlUnquote(tok.substr(b, e - b + 1), &ok);
        if (!ok) return false;
        out->push_back(std::move(v));
    }
    return true;
}

bool ParseManifest(const std::string& text, ExtManifest* out,
                   std::string* error) {
    std::string section;
    std::istringstream in(text);
    std::string line;
    size_t lineno = 0;
    auto fail = [&](const std::string& msg) {
        if (error) {
            *error = "manifest.toml:" + std::to_string(lineno) +
                     ": " + msg;
        }
        return false;
    };
    while (std::getline(in, line)) {
        ++lineno;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const auto e = line.find_last_not_of(" \t\r");
        const std::string s = line.substr(b, e - b + 1);
        if (s.front() == '[' && s.back() == ']') {
            section = s.substr(1, s.size() - 2);
            continue;
        }
        const auto eq = s.find('=');
        if (eq == std::string::npos) {
            return fail("expected key = value");
        }
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        const auto kb = key.find_first_not_of(" \t");
        const auto ke = key.find_last_not_of(" \t");
        const auto vb = val.find_first_not_of(" \t");
        const auto ve = val.find_last_not_of(" \t");
        key = key.substr(kb, ke - kb + 1);
        val = val.substr(vb, ve - vb + 1);
        if (key.empty() || val.empty()) {
            return fail("empty key or value");
        }

        bool ok = false;
        const auto str_or = [&] {
            return TomlUnquote(val, &ok);
        };
        const auto arr_or = [&] {
            std::vector<std::string> v;
            ok = TomlParseArray(val, &v);
            return v;
        };

        if (section == "extension") {
            const std::string v = str_or();
            if (!ok) return fail("expected \"string\" for " + key);
            if (key == "id") out->id = v;
            else if (key == "name") out->name = v;
            else if (key == "version") out->version = v;
            else if (key == "entry") out->entry = v;
            else if (key == "ui") out->ui = v;
            // unknown keys tolerated
        } else if (section == "files" && key == "requires") {
            out->required = arr_or();
            if (!ok) return fail("expected [\"a\", ...] for requires");
        } else if (section == "native") {
            auto v = arr_or();
            if (!ok) return fail("expected array for " + key);
            if (key == "dlls") out->native_dll = std::move(v);
            else if (key == "lua") out->native_lua = std::move(v);
            else if (key == "py") out->native_py = std::move(v);
        }
        // Other sections/keys are ignored on purpose.
    }
    if (out->id.empty()) return fail("[extension] id is required");
    if (!Tz::TzIsSafeName(out->entry)) {
        return fail("unsafe entry path: " + out->entry);
    }
    if (!out->ui.empty() && !Tz::TzIsSafeName(out->ui)) {
        return fail("unsafe ui path: " + out->ui);
    }
    return true;
}

// JSON escape for strings flowing into the app dispatcher.
std::string JsonEsc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string ToStr(const apprt::OutValue& v) {
    switch (v.kind) {
        case apprt::OutValue::Kind::String:
        case apprt::OutValue::Kind::Json:
            return v.str;
        case apprt::OutValue::Kind::Number: {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", v.num);
            return buf;
        }
        case apprt::OutValue::Kind::Bool:
            return v.boolean ? "true" : "false";
        default:
            return {};
    }
}

bool InList(const std::vector<std::string>& list,
            const std::string& member) {
    return std::find(list.begin(), list.end(), member) != list.end();
}

}  // namespace

// ---- ExtHost ------------------------------------------------------------

ExtHost& ExtHost::Shared() {
    static ExtHost host;
    return host;
}

std::string ExtHost::NativeCacheDir(const std::string& ext_id) {
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    return (base / "xer_ext_cache" / ext_id).string();
}

ExtHost::Loaded* ExtHost::FindLoaded(const std::string& id) {
    for (auto& l : loaded_) {
        if (l.ext.manifest.id == id) return &l;
    }
    return nullptr;
}

bool ExtHost::Load(const std::string& path, std::string* error) {
    Extension ext;
    ext.path = path;

    // 1. Parse the archive + manifest first — the member-level verify
    //    needs the manifest's declared requirements.
    std::string open_err;
    auto archive = Tz::TzArchive::Open(path, &open_err);
    if (!archive) {
        if (error) *error = open_err;
        ext.verify_errors.push_back(open_err);
        entries_.push_back(std::move(ext));
        return false;
    }
    std::string manifest_text;
    if (!archive->Read("manifest.toml", &manifest_text)) {
        const std::string m = "manifest.toml missing";
        if (error) *error = m;
        ext.verify_errors.push_back(m);
        entries_.push_back(std::move(ext));
        return false;
    }
    std::string parse_err;
    if (!ParseManifest(manifest_text, &ext.manifest, &parse_err)) {
        if (error) *error = parse_err;
        ext.verify_errors.push_back(parse_err);
        entries_.push_back(std::move(ext));
        return false;
    }

    // 2. Required members: the manifest's own declarations plus the
    //    fixed contract (entry point + declared native payloads).
    std::vector<std::string> required = ext.manifest.required;
    required.push_back(ext.manifest.entry);
    for (const auto& n : ext.manifest.native_dll) required.push_back(n);
    for (const auto& n : ext.manifest.native_lua) required.push_back(n);
    for (const auto& n : ext.manifest.native_py) required.push_back(n);

    auto report = Tz::TzVerify(path, required);
    ext.verify_errors = report.errors;
    ext.verified = report.ok;
    entries_.push_back(ext);
    if (!report.ok) {
        if (error) {
            *error = report.errors.empty()
                         ? "verification failed"
                         : report.errors.front();
        }
        return false;
    }

    // 3. Verified — keep the archive in memory and expose it to the
    //    xer-ext:// scheme so the UI renders straight out of the blob.
    Loaded l;
    l.ext = std::move(ext);
    l.archive = std::shared_ptr<Tz::TzArchive>(std::move(archive));
    const std::string id = l.ext.manifest.id;
    if (Loaded* prev = FindLoaded(id)) {
        *prev = std::move(l);
    } else {
        loaded_.push_back(std::move(l));
    }
    Cef::ExtSchemeRegister(id, FindLoaded(id)->archive);
    return true;
}

void ExtHost::Scan(const std::string& extensions_dir) {
    entries_.clear();
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(extensions_dir, ec)) return;
    for (const auto& de : fs::directory_iterator(extensions_dir, ec)) {
        if (!de.is_regular_file(ec)) continue;
        if (de.path().extension() != ".xexrj") continue;
        std::string err;
        Load(de.path().string(), &err);
    }
}

std::vector<std::uint8_t> ExtHost::ReadMember(
    const std::string& id, const std::string& member) {
    Loaded* l = FindLoaded(id);
    std::vector<std::uint8_t> out;
    if (l != nullptr) l->archive->Read(member, &out);
    return out;
}

std::string ExtHost::ExtractForNative(Loaded& loaded,
                                      const std::string& member,
                                      std::string* error) {
    const Tz::TzEntry* e = loaded.archive->Find(member);
    if (e == nullptr) {
        if (error) *error = "member not in archive: " + member;
        return {};
    }
    const std::string dir = NativeCacheDir(loaded.ext.manifest.id);
    if (dir.empty()) {
        if (error) *error = "cannot resolve temp dir";
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string dest =
        (std::filesystem::path(dir) /
         std::filesystem::path(member).filename()).string();

    // Reuse the cached copy only when its content matches the archive
    // crc — a stale or tampered cache file is always overwritten.
    bool reuse = false;
    {
        std::ifstream probe(dest, std::ios::binary | std::ios::ate);
        if (probe) {
            const auto size = probe.tellg();
            if (static_cast<std::uint64_t>(size) == e->size) {
                probe.seekg(0);
                std::vector<std::uint8_t> have(
                    static_cast<size_t>(size));
                probe.read(reinterpret_cast<char*>(have.data()), size);
                reuse = probe.good() &&
                        Tz::TzCrc32(have.data(), have.size()) == e->crc32;
            }
        }
    }
    if (!reuse) {
        std::vector<std::uint8_t> bytes;
        if (!loaded.archive->Read(member, &bytes)) {
            if (error) *error = "cannot read member: " + member;
            return {};
        }
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            if (error) *error = "cannot write cache file: " + dest;
            return {};
        }
    }
    return dest;
}

// ---- the xer_ext module --------------------------------------------------

void ExtHost::InstallModule(Loaded& loaded, RunResult* sink) {
    running_ = &loaded;
    sink_ = sink;

    apprt::JsEngine* engine = ScriptHost::Shared().Engine(nullptr);
    if (engine == nullptr) return;

    engine->RegisterModule(
        "xer_ext", [this](apprt::JsEngine& e) {
            const int obj = e.NewObject();
            if (obj < 0) return;
            using apprt::OutValue;

            e.DefineFunction(
                obj, "log",
                [this](const std::vector<OutValue>& args) {
                    if (sink_ != nullptr) {
                        for (size_t i = 0; i < args.size(); ++i) {
                            if (i) sink_->output += ' ';
                            sink_->output += ToStr(args[i]);
                        }
                        sink_->output += '\n';
                    }
                    return apprt::UndefVal();
                });
            e.DefineFunction(
                obj, "setStatus",
                [this](const std::vector<OutValue>& args) {
                    if (action_fn_) {
                        action_fn_("status",
                                   "{\"text\":\"" +
                                       JsonEsc(args.empty()
                                                   ? std::string()
                                                   : ToStr(args[0])) +
                                       "\"}");
                    }
                    return apprt::UndefVal();
                });
            e.DefineFunction(
                obj, "projectInfo",
                [this](const std::vector<OutValue>&) {
                    if (action_fn_) {
                        return apprt::StrVal(
                            action_fn_("project_info", "{}"));
                    }
                    return apprt::StrVal("{}");
                });
            e.DefineFunction(
                obj, "call",
                [this](const std::vector<OutValue>& args) {
                    if (!action_fn_ || args.empty()) {
                        return apprt::StrVal(
                            "{\"error\":\"xer_ext.call unavailable\"}");
                    }
                    const std::string arg =
                        args.size() > 1 ? ToStr(args[1]) : "{}";
                    return apprt::StrVal(
                        action_fn_(ToStr(args[0]), arg));
                });
            e.DefineFunction(
                obj, "id",
                [this](const std::vector<OutValue>&) {
                    return apprt::StrVal(
                        running_ ? running_->ext.manifest.id
                                 : std::string());
                });
            // listFiles() -> JSON array string (flat OutValue surface
            // has no array kind, so we hand JS text it can parse).
            e.DefineFunction(
                obj, "listFiles",
                [this](const std::vector<OutValue>&) {
                    std::string json = "[";
                    if (running_ != nullptr) {
                        bool first = true;
                        for (const auto& n : running_->archive->Names()) {
                            if (!first) json += ',';
                            json += "\"" + JsonEsc(n) + "\"";
                            first = false;
                        }
                    }
                    json += ']';
                    return apprt::StrVal(json);
                });
            // readFile(name) — archive members only; scripts/, static/
            // and any declared member are readable, everything else is
            // rejected by the archive's safe-name rules anyway.
            e.DefineFunction(
                obj, "readFile",
                [this](const std::vector<OutValue>& args) {
                    if (running_ == nullptr || args.empty()) {
                        return apprt::StrVal("");
                    }
                    std::string text;
                    running_->archive->Read(ToStr(args[0]), &text);
                    return apprt::StrVal(text);
                });
            e.DefineFunction(
                obj, "openUi",
                [this](const std::vector<OutValue>& args) {
                    if (running_ == nullptr || !open_ui_fn_) {
                        return apprt::BoolVal(false);
                    }
                    std::string ui = running_->ext.manifest.ui;
                    if (!args.empty() && !ToStr(args[0]).empty()) {
                        ui = ToStr(args[0]);
                    }
                    if (ui.empty() ||
                        !running_->archive->Has(ui)) {
                        return apprt::BoolVal(false);
                    }
                    open_ui_fn_(Cef::ExtSchemeUrl(
                        running_->ext.manifest.id, ui));
                    return apprt::BoolVal(true);
                });
            // ---- native payloads ------------------------------------
            e.DefineFunction(
                obj, "loadNative",
                [this](const std::vector<OutValue>& args) {
                    if (running_ == nullptr || args.empty()) {
                        return apprt::StrVal(
                            "{\"error\":\"loadNative unavailable\"}");
                    }
                    const std::string member =
                        Tz::TzNormalizeName(ToStr(args[0]));
                    if (!InList(running_->ext.manifest.native_dll,
                                member)) {
                        return apprt::StrVal(
                            "{\"error\":\"payload not allowlisted in "
                            "[native] dlls\"}");
                    }
                    std::string err;
                    const std::string path =
                        ExtractForNative(*running_, member, &err);
                    if (path.empty()) {
                        return apprt::StrVal("{\"error\":\"" +
                                             JsonEsc(err) + "\"}");
                    }
#ifdef _WIN32
                    HMODULE h = LoadLibraryA(path.c_str());
                    if (h == nullptr) {
                        return apprt::StrVal(
                            "{\"error\":\"LoadLibrary failed for " +
                            JsonEsc(member) + "\"}");
                    }
                    // Optional init hook — the extension's dll can
                    // receive a JSON context {"id": ..., "dir": ...}.
                    using InitFn = int (*)(const char*);
                    if (auto* init = reinterpret_cast<InitFn>(
                            GetProcAddress(h, "xer_ext_init"))) {
                        const std::string ctx =
                            "{\"id\":\"" +
                            JsonEsc(running_->ext.manifest.id) +
                            "\",\"dir\":\"" +
                            JsonEsc(NativeCacheDir(
                                running_->ext.manifest.id)) +
                            "\"}";
                        const int rc = init(ctx.c_str());
                        if (rc != 0) {
                            return apprt::StrVal(
                                "{\"error\":\"xer_ext_init returned " +
                                std::to_string(rc) + "\"}");
                        }
                    }
                    return apprt::StrVal("{\"loaded\":\"" +
                                         JsonEsc(member) + "\"}");
#else
                    return apprt::StrVal(
                        "{\"error\":\"native dll loading is "
                        "Windows-only\"}");
#endif
                });
            // .lua / .py run as subprocesses — the archive bytes are
            // extracted to the crc-checked cache, then spawned via the
            // interpreter found on PATH; stdout returns to the script.
            auto run_interpreter =
                [this](const std::vector<std::string>& allow,
                       const char* const* interpreters,
                       size_t interp_count,
                       const std::vector<OutValue>& args)
                -> OutValue {
                if (running_ == nullptr || args.empty()) {
                    return apprt::StrVal(
                        "{\"error\":\"interpreter unavailable\"}");
                }
                const std::string member =
                    Tz::TzNormalizeName(ToStr(args[0]));
                if (!InList(allow, member)) {
                    return apprt::StrVal(
                        "{\"error\":\"script not allowlisted in "
                        "manifest [native]\"}");
                }
                std::string err;
                const std::string path =
                    ExtractForNative(*running_, member, &err);
                if (path.empty()) {
                    return apprt::StrVal("{\"error\":\"" +
                                         JsonEsc(err) + "\"}");
                }
#ifdef _WIN32
                std::string interp;
                for (size_t i = 0; i < interp_count; ++i) {
                    // SearchPathA honors PATH and returns "" on miss.
                    char found[MAX_PATH]{};
                    if (SearchPathA(nullptr, interpreters[i], ".exe",
                                    MAX_PATH, found, nullptr) > 0) {
                        interp = found;
                        break;
                    }
                }
                if (interp.empty()) {
                    return apprt::StrVal(
                        "{\"error\":\"no interpreter on PATH\"}");
                }
                const std::string cmd =
                    "\"" + interp + "\" \"" + path + "\"";
                FILE* pipe = _popen(cmd.c_str(), "r");
                if (pipe == nullptr) {
                    return apprt::StrVal(
                        "{\"error\":\"spawn failed\"}");
                }
                std::string out_text;
                char buf[512];
                while (fgets(buf, sizeof buf, pipe) != nullptr) {
                    out_text += buf;
                }
                const int rc = _pclose(pipe);
                return apprt::StrVal(
                    "{\"exit\":" + std::to_string(rc) +
                    ",\"output\":\"" + JsonEsc(out_text) + "\"}");
#else
                return apprt::StrVal(
                    "{\"error\":\"interpreter execution is "
                    "Windows-only\"}");
#endif
            };
            static const char* kLua[] = {"lua", "lua54", "luajit"};
            static const char* kPy[] = {"python", "py", "python3"};
            e.DefineFunction(
                obj, "runLua",
                [this, run_interpreter](
                    const std::vector<OutValue>& args) {
                    return run_interpreter(
                        running_ ? running_->ext.manifest.native_lua
                                 : std::vector<std::string>{},
                        kLua, std::size(kLua), args);
                });
            e.DefineFunction(
                obj, "runPython",
                [this, run_interpreter](
                    const std::vector<OutValue>& args) {
                    return run_interpreter(
                        running_ ? running_->ext.manifest.native_py
                                 : std::vector<std::string>{},
                        kPy, std::size(kPy), args);
                });
        });
}

ExtHost::RunResult ExtHost::Run(const std::string& id) {
    RunResult result;
    Loaded* l = FindLoaded(id);
    if (l == nullptr) {
        result.error = "extension '" + id + "' is not loaded";
        return result;
    }
    std::string source;
    if (!l->archive->Read(l->ext.manifest.entry, &source)) {
        result.error = "cannot read entry " + l->ext.manifest.entry;
        return result;
    }

    apprt::JsEngine* engine = ScriptHost::Shared().Engine(nullptr);
    if (engine == nullptr) {
        result.error = "js engine unavailable";
        return result;
    }
    InstallModule(*l, &result);
    std::string js_err;
    const bool ok = engine->RunSource(
        source, id + ":" + l->ext.manifest.entry, &js_err);
    running_ = nullptr;
    sink_ = nullptr;
    if (!ok) {
        result.error = js_err;
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace XER::VideoEditor
