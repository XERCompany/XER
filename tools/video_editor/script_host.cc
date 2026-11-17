// Implementation of the Community Scripts host — see script_host.h
// for the contract. The manifest loader follows the same jsoncpp
// conventions as app_runtime/ui_manifest.cc LoadToolbarJson:
// soft-fail on missing/malformed files, skip malformed elements,
// never let a crafted file escape as an exception.
#include "script_host.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <utility>

#include <json/json.h>

#include "app_runtime/app_runtime.h"
#include "app_runtime/native_modules.h"

namespace XER::VideoEditor {
namespace {

namespace apprt = AppRuntime;

std::filesystem::path Utf8ToPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string PathToUtf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()),
                       u8.size());
}

// Minimal escaper for embedding a string inside a JSON string literal
// — enough for the small {"text": ...} / error payloads built here.
std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string ErrorJson(const std::string& message) {
    return "{\"ok\":false,\"error\":\"" + JsonEscape(message) + "\"}";
}

// A manifest `file` must stay inside the scripts dir: no absolute
// paths, no root name (drive letter) and no ".." component.
bool IsSafeScriptFile(const std::string& file) {
    if (file.empty()) return false;
    const std::filesystem::path p = Utf8ToPath(file);
    if (p.is_absolute() || p.has_root_name()) return false;
    for (const auto& component : p) {
        if (component == "..") return false;
    }
    return true;
}

// Renders an OutValue for the log sink — mirrors ArgToString in
// app_runtime/native_modules.cc (kept local: that helper is in an
// anonymous namespace and not exported).
std::string OutValueToString(const apprt::OutValue& v) {
    switch (v.kind) {
        case apprt::OutValue::Kind::String:
        case apprt::OutValue::Kind::Json:
            return v.str;
        case apprt::OutValue::Kind::Number: {
            if (v.num > -9.007199254740992e15 &&
                v.num < 9.007199254740992e15) {
                const long long i = static_cast<long long>(v.num);
                if (static_cast<double>(i) == v.num)
                    return std::to_string(i);
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", v.num);
            return buf;
        }
        case apprt::OutValue::Kind::Bool:
            return v.boolean ? "true" : "false";
        case apprt::OutValue::Kind::Handler:
            return "[object Function]";
        case apprt::OutValue::Kind::Undefined:
        default:
            return "undefined";
    }
}

}  // namespace

ScriptHost& ScriptHost::Shared() {
    static ScriptHost host;
    return host;
}

void ScriptHost::SetActionFn(ActionFn fn) {
    action_fn_ = std::move(fn);
}

bool ScriptHost::LoadManifest(const std::string& scripts_dir,
                              std::string* error) {
    entries_.clear();
    scripts_dir_.clear();

    const std::filesystem::path dir = Utf8ToPath(scripts_dir);
    const std::filesystem::path manifest = dir / "scripts.json";
    std::error_code ec;
    if (!std::filesystem::exists(manifest, ec)) {
        if (error) {
            *error =
                "scripts.json not found in '" + scripts_dir + "'";
        }
        return false;
    }
    std::ifstream in(manifest, std::ios::binary);
    if (!in.is_open()) {
        if (error) *error = "cannot open '" + PathToUtf8(manifest) + "'";
        return false;
    }

    Json::CharReaderBuilder reader_builder;
    Json::Value root;
    std::string parse_errors;
    bool parsed = false;
    try {
        parsed = Json::parseFromStream(reader_builder, in, &root,
                                       &parse_errors);
    } catch (const Json::Exception& e) {
        if (error) *error = e.what();
        return false;
    } catch (...) {
        // Never-throw contract, same as LoadToolbarJson.
        if (error) *error = "scripts.json: malformed JSON";
        return false;
    }
    if (!parsed) {
        if (error) *error = "scripts.json: " + parse_errors;
        return false;
    }
    if (!root.isArray()) {
        if (error) {
            *error = "scripts.json: top level must be an array of "
                     "script entries";
        }
        return false;
    }

    for (const Json::Value& elem : root) {
        if (!elem.isObject()) continue;  // stray scalars are skipped
        ScriptEntry entry;
        if (elem["id"].isString()) entry.id = elem["id"].asString();
        if (elem["title"].isString())
            entry.title = elem["title"].asString();
        if (elem["description"].isString())
            entry.description = elem["description"].asString();
        if (elem["file"].isString())
            entry.file = elem["file"].asString();
        if (elem["category"].isString())
            entry.category = elem["category"].asString();
        if (elem["shortcut"].isString())
            entry.shortcut = elem["shortcut"].asString();

        if (entry.id.empty() || !IsSafeScriptFile(entry.file))
            continue;  // unusable or unsafe — rejected, not fatal

        bool duplicate = false;
        for (const ScriptEntry& existing : entries_) {
            if (existing.id == entry.id) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) entries_.push_back(std::move(entry));
    }

    scripts_dir_ = scripts_dir;
    return true;
}

std::string ScriptHost::ResolvePath(const ScriptEntry& entry) const {
    return PathToUtf8(Utf8ToPath(scripts_dir_) /
                      Utf8ToPath(entry.file));
}

apprt::JsEngine* ScriptHost::Engine(std::string* error) {
    if (apprt::JsEngine* shared = apprt::SharedBootEngine()) {
        return shared;
    }
    if (!owned_engine_) {
        std::string create_error;
        owned_engine_ = apprt::JsEngine::Create(&create_error);
        if (!owned_engine_) {
            if (error) *error = "js engine: " + create_error;
            return nullptr;
        }
        // The private engine gets the same bootstrap modules Boot()
        // installs, so scripts can use app_super_concat & friends.
        apprt::RegisterAll(*owned_engine_);
    }
    return owned_engine_.get();
}

void ScriptHost::EnsureEditorModule(apprt::JsEngine& engine) {
    if (module_engine_ == &engine) return;
    engine.RegisterModule(
        "xer_editor",
        [this](apprt::JsEngine& e) { BuildEditorModule(e); });
    module_engine_ = &engine;
}

std::string ScriptHost::Dispatch(const std::string& action,
                                 const std::string& arg_json) {
    if (!action_fn_) {
        return ErrorJson("xer_editor: no action handler installed");
    }
    try {
        return action_fn_(action, arg_json);
    } catch (const std::exception& e) {
        return ErrorJson("action '" + action +
                         "' threw: " + e.what());
    } catch (...) {
        return ErrorJson("action '" + action +
                         "' threw an unknown exception");
    }
}

apprt::OutValue ScriptHost::EditorCall(
    const std::vector<apprt::OutValue>& args) {
    if (args.empty() || args[0].str.empty()) {
        return apprt::StrVal(
            ErrorJson("xer_editor.call: action name expected"));
    }
    const std::string arg_json =
        args.size() > 1 && !args[1].str.empty() ? args[1].str : "{}";
    return apprt::StrVal(Dispatch(args[0].str, arg_json));
}

void ScriptHost::BuildEditorModule(apprt::JsEngine& engine) {
    using apprt::OutValue;
    const int obj = engine.NewObject();
    if (obj < 0) return;

    engine.DefineFunction(
        obj, "call", [this](const std::vector<OutValue>& args) {
            return EditorCall(args);
        });
    engine.DefineFunction(
        obj, "projectInfo", [this](const std::vector<OutValue>&) {
            return apprt::StrVal(Dispatch("project_info", "{}"));
        });
    engine.DefineFunction(
        obj, "setStatus", [this](const std::vector<OutValue>& args) {
            const std::string text =
                args.empty() ? std::string() : OutValueToString(args[0]);
            return apprt::StrVal(
                Dispatch("status",
                         "{\"text\":\"" + JsonEscape(text) + "\"}"));
        });
    engine.DefineFunction(
        obj, "log", [this](const std::vector<OutValue>& args) {
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) run_output_ += ' ';
                run_output_ += OutValueToString(args[i]);
            }
            run_output_ += '\n';
            return apprt::UndefVal();
        });
}

ScriptHost::RunResult ScriptHost::Run(const std::string& id) {
    RunResult result;

    const ScriptEntry* entry = nullptr;
    for (const ScriptEntry& e : entries_) {
        if (e.id == id) {
            entry = &e;
            break;
        }
    }
    if (entry == nullptr) {
        result.error = "script '" + id + "' is not in the manifest";
        return result;
    }

    // Defense in depth: LoadManifest already rejected unsafe `file`
    // values, but re-check before touching the file system, then
    // verify the normalised path still sits under the scripts dir.
    if (!IsSafeScriptFile(entry->file)) {
        result.error = "script '" + id + "' has an unsafe file path";
        return result;
    }
    const std::filesystem::path dir_norm =
        Utf8ToPath(scripts_dir_).lexically_normal();
    const std::filesystem::path file_norm =
        Utf8ToPath(ResolvePath(*entry)).lexically_normal();
    auto dir_it = dir_norm.begin();
    auto file_it = file_norm.begin();
    for (; dir_it != dir_norm.end(); ++dir_it, ++file_it) {
        if (file_it == file_norm.end() || *file_it != *dir_it) break;
    }
    if (dir_it != dir_norm.end()) {
        result.error = "script '" + id + "' escapes the scripts dir";
        return result;
    }

    std::string error;
    apprt::JsEngine* engine = Engine(&error);
    if (engine == nullptr) {
        result.error = error;
        return result;
    }
    EnsureEditorModule(*engine);

    run_output_.clear();
    std::string run_error;
    result.ok = engine->RunFile(ResolvePath(*entry), &run_error);
    result.output = run_output_;
    if (!result.ok) result.error = std::move(run_error);
    return result;
}

}  // namespace XER::VideoEditor
