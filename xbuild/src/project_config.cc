#include "project_config.h"
#include "file_io.h"

#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/none_of.hpp>
#include <re2/re2.h>

#include <initializer_list>
#include <memory>
#include <utility>

namespace XBuild {
namespace {

enum class Kind { String, NonEmptyString, Bool, Integer, PositiveInteger, Strings, Array };

struct Field {
    const char* name;
    Kind kind;
    bool required = false;
};

bool Fail(const std::string& path, const std::string& message, std::string& error) {
    error = absl::StrCat(path, ": ", message);
    return false;
}

bool NonEmpty(const Json::Value& value) {
    static const RE2 non_blank("[^ \\t\\r\\n]");
    return value.isString() && RE2::PartialMatch(value.asString(), non_blank);
}

std::string Index(const std::string& path, Json::ArrayIndex index) {
    return absl::StrCat(path, "[", index, "]");
}

bool CheckValue(const Json::Value& value, Kind kind,
                const std::string& path, std::string& error) {
    switch (kind) {
    case Kind::String:
        return value.isString() || Fail(path, "expected a string", error);
    case Kind::NonEmptyString:
        return NonEmpty(value) || Fail(path, "expected a non-empty string", error);
    case Kind::Bool:
        return value.isBool() || Fail(path, "expected a boolean", error);
    case Kind::Integer:
    case Kind::PositiveInteger:
        return (value.isIntegral() && value.isInt() &&
                value.asInt() >= (kind == Kind::PositiveInteger ? 1 : 0)) ||
               Fail(path, "expected an integer in " +
                    std::string(kind == Kind::PositiveInteger ? "[1, 2147483647]" :
                                                               "[0, 2147483647]"), error);
    case Kind::Array:
    case Kind::Strings:
        if (!value.isArray()) {
            return Fail(path, "expected an array", error);
        }
        if (kind == Kind::Strings) {
            for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
                if (!NonEmpty(value[i])) {
                    return Fail(Index(path, i), "expected a non-empty string", error);
                }
            }
        }
        return true;
    }
    return Fail(path, "unsupported field type", error);
}

bool CheckObject(const Json::Value& object, const std::string& path,
                 std::initializer_list<Field> fields, std::string& error) {
    if (!object.isObject()) {
        return Fail(path, "expected an object", error);
    }
    for (const auto& field : fields) {
        const std::string field_path = path + "." + field.name;
        if (!object.isMember(field.name)) {
            if (field.required) {
                return Fail(field_path, "required field is missing", error);
            }
        } else if (!CheckValue(object[field.name], field.kind, field_path, error)) {
            return false;
        }
    }
    for (const auto& name : object.getMemberNames()) {
        if (ranges::none_of(fields, [&name](const Field& field) { return name == field.name; })) {
            return Fail(path + "." + name, "unknown field", error);
        }
    }
    return true;
}

bool CheckSource(const Json::Value& source, const std::string& path, std::string& error) {
    if (!CheckObject(source, path, {
            {"input", Kind::NonEmptyString}, {"tool", Kind::NonEmptyString},
            {"out", Kind::NonEmptyString}, {"start_post", Kind::String},
            {"end_post", Kind::String}, {"glob_files", Kind::Bool},
            {"async_sleep", Kind::Integer}, {"header", Kind::Bool},
            {"scope_support", Kind::Strings}, {"glob_aware_worker", Kind::Bool},
            {"glob_input", Kind::Strings}}, error)) {
        return false;
    }
    const bool input = source.isMember("input");
    const bool sleep = source.isMember("async_sleep");
    const bool worker = source.get("glob_aware_worker", false).asBool();
    if (static_cast<int>(input) + static_cast<int>(sleep) + static_cast<int>(worker) != 1) {
        return Fail(path, "expected exactly one of input, async_sleep or glob_aware_worker=true", error);
    }
    if (sleep && source.size() != 1) {
        return Fail(path, "async_sleep must be a separate source entry", error);
    }
    if (worker && (!source.isMember("glob_input") || source["glob_input"].empty())) {
        return Fail(path + ".glob_input", "glob worker requires a non-empty array", error);
    }
    if (!worker && source.isMember("glob_input")) {
        return Fail(path + ".glob_input", "requires glob_aware_worker=true", error);
    }
    if (source.isMember("tool")) {
        const auto tool = source["tool"].asString();
        if (tool != "clang" && tool != "msvc") {
            return Fail(path + ".tool", "expected clang or msvc", error);
        }
    }
    return true;
}

bool CheckScopes(const Json::Value& scopes, std::string& error) {
    absl::flat_hash_set<std::string> names;
    for (Json::ArrayIndex i = 0; i < scopes.size(); ++i) {
        const auto& scope = scopes[i];
        const std::string path = Index("$.scopes", i);
        if (!CheckObject(scope, path, {
                {"custom_scope", Kind::NonEmptyString, true},
                {"system", Kind::Array, true}}, error)) {
            return false;
        }
        if (!names.insert(scope["custom_scope"].asString()).second) {
            return Fail(path + ".custom_scope", "duplicate scope name", error);
        }
        const auto& system = scope["system"];
        for (Json::ArrayIndex j = 0; j < system.size(); ++j) {
            const std::string action_path = Index(path + ".system", j);
            if (!system[j].isObject() || system[j].size() != 1 ||
                !system[j].isMember("if")) {
                return Fail(action_path, "expected an object containing only 'if'", error);
            }
            if (!CheckObject(system[j]["if"], action_path + ".if", {
                    {"IN", Kind::NonEmptyString, true},
                    {"TRUE", Kind::String, true},
                    {"ELSE", Kind::String}}, error)) {
                return false;
            }
        }
    }
    return true;
}

bool ValidateMockPrototype(const Json::Value& prototype, const std::string& path, std::string& error) {
    if (!CheckObject(prototype, path, {
            {"name", Kind::NonEmptyString, true},
            {"type", Kind::NonEmptyString, true},
            {"static", Kind::Bool},
            {"value", Kind::String},
            {"functionArgs", Kind::Array},
            {"functionReturnType", Kind::String},
            {"return", Kind::Array}}, error)) {
        return false;
    }
    const auto type = prototype["type"].asString();
    if (type != "int" && type != "function" && type != "string" && type != "bool" && type != "float" && type != "double") {
        return Fail(path + ".type", "expected int, function, string, bool, float or double", error);
    }
    if (type == "function" && !prototype.isMember("functionReturnType")) {
        return Fail(path + ".functionReturnType", "required for function type", error);
    }
    return true;
}

bool ValidateMockNamespace(const Json::Value& ns, const std::string& path, std::string& error) {
    if (!CheckObject(ns, path, {
            {"prototypeVariants", Kind::Array, true}}, error)) {
        return false;
    }
    const auto& variants = ns["prototypeVariants"];
    for (Json::ArrayIndex i = 0; i < variants.size(); ++i) {
        if (!ValidateMockPrototype(variants[i], Index(path + ".prototypeVariants", i), error)) {
            return false;
        }
    }
    return true;
}

bool ValidateMockTarget(const Json::Value& target, const std::string& path, std::string& error) {
    if (!CheckObject(target, path, {
            {"name", Kind::NonEmptyString, true},
            {"lock", Kind::Bool},
            {"constructor", Kind::Array, true}}, error)) {
        return false;
    }
    const auto& constructor = target["constructor"];
    for (Json::ArrayIndex i = 0; i < constructor.size(); ++i) {
        const auto& item = constructor[i];
        const std::string item_path = Index(path + ".constructor", i);
        if (!item.isObject() || !item.isMember("Namespace")) {
            return Fail(item_path, "expected object with Namespace", error);
        }
        if (!ValidateMockNamespace(item["Namespace"], item_path + ".Namespace", error)) {
            return false;
        }
    }
    return true;
}

bool ValidateMockTargets(const Json::Value& mock_targets, std::string& error) {
    for (Json::ArrayIndex i = 0; i < mock_targets.size(); ++i) {
        if (!ValidateMockTarget(mock_targets[i], Index("$.mock_targets", i), error)) {
            return false;
        }
    }
    return true;
}

bool Validate(const Json::Value& root, std::string& error) {
    if (!CheckObject(root, "$", {
            {"includes", Kind::Strings}, {"defines", Kind::Strings},
            {"require", Kind::Strings}, {"link_libs", Kind::Strings},
            {"depot_tools_enabled", Kind::Bool},
            {"depot_tools_sync_jobs", Kind::PositiveInteger},
            {"depot_tools_fetch", Kind::Strings},
            {"depot_tools_make_environment_data", Kind::Bool},
            {"depot_tools_output_fetching", Kind::NonEmptyString},
            {"debug", Kind::Bool}, {"terimal_prefix", Kind::String},
            {"terimal_enable_color", Kind::Bool}, {"terimal_verbose", Kind::Bool},
            {"terimal_show_time", Kind::Bool}, {"terimal_show_progress", Kind::Bool},
            {"terimal_sandbox", Kind::NonEmptyString},
            {"features_flags", Kind::Strings}, {"vcpkg_enabled", Kind::Bool},
            {"vcpkg_triplet", Kind::NonEmptyString}, {"vcpkg_enable_manifest", Kind::Bool},
            {"directory_link_libs", Kind::Strings}, {"scopes", Kind::Array},
            {"targets", Kind::Array, true}, {"output", Kind::NonEmptyString},
            {"libs", Kind::Strings}, {"vcpkg_packages", Kind::Strings},
            {"vcpkg_include_dirs", Kind::Strings}, {"vcpkg_link_dirs", Kind::Strings},
            {"build_dir", Kind::NonEmptyString}, {"jobs", Kind::PositiveInteger},
            {"timeout_ms", Kind::Integer}, {"tool", Kind::NonEmptyString},
            {"mock_generate_gen_cpp", Kind::Bool}, {"mock_targets", Kind::Array},
            {"optimization_level", Kind::Integer}, {"cpp_standard", Kind::NonEmptyString},
            {"warnings_as_errors", Kind::Bool}, {"all_warnings", Kind::Bool},
            {"extra_warnings", Kind::Bool}, {"rtti", Kind::Bool}, {"exceptions", Kind::Bool},
            {"address_sanitizer", Kind::Bool}, {"memory_sanitizer", Kind::Bool},
            {"thread_sanitizer", Kind::Bool}, {"undefined_sanitizer", Kind::Bool},
            {"lto", Kind::Bool}, {"lto_mode", Kind::String}, {"pic", Kind::Bool},
            {"optimize_size", Kind::Bool}, {"fast_math", Kind::Bool},
            {"target_triple", Kind::String}, {"target_cpu", Kind::String},
            {"extra_compile_flags", Kind::Strings}, {"extra_link_flags", Kind::Strings}}, error)) {
        return false;
    }
    if (root.isMember("scopes") && !CheckScopes(root["scopes"], error)) {
        return false;
    }
    if (root.isMember("mock_targets") && !ValidateMockTargets(root["mock_targets"], error)) {
        return false;
    }
    const auto& targets = root["targets"];
    if (targets.empty()) {
        return Fail("$.targets", "expected at least one target", error);
    }
    absl::flat_hash_set<std::string> names;
    for (Json::ArrayIndex i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        const std::string path = Index("$.targets", i);
        if (!CheckObject(target, path, {
                {"name", Kind::NonEmptyString, true}, {"define_target", Kind::Strings},
                {"include_target", Kind::Strings}, {"links_libs_target", Kind::Strings},
                {"sources", Kind::Array, true}}, error)) {
            return false;
        }
        if (!names.insert(target["name"].asString()).second) {
            return Fail(path + ".name", "duplicate target name", error);
        }
        const auto& sources = target["sources"];
        if (sources.empty()) {
            return Fail(path + ".sources", "expected at least one source entry", error);
        }
        for (Json::ArrayIndex j = 0; j < sources.size(); ++j) {
            if (!CheckSource(sources[j], Index(path + ".sources", j), error)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool LoadProjectConfig(const std::filesystem::path& path,
                       Json::Value& output,
                       std::string& error) {
    error.clear();
    std::string contents;
    if (!ReadFile(path, contents, error)) {
        return false;
    }
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["allowComments"] = true;
    builder["allowTrailingCommas"] = false;
    builder["failIfExtra"] = true;
    builder["rejectDupKeys"] = true;
    builder["stackLimit"] = 128;
    Json::Value document;
    std::string parse_error;
    bool parsed = false;
    try {
        const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        parsed = reader->parse(contents.data(), contents.data() + contents.size(),
                               &document, &parse_error);
    } catch (const Json::Exception& exception) {
        parse_error = exception.what();
    }
    if (!parsed) {
        error = absl::StrCat(path.string(), ": Invalid JSON: ", parse_error);
        return false;
    }
    if (!Validate(document, error)) {
        error = path.string() + ": " + error;
        return false;
    }
    output = std::move(document);
    return true;
}

} // namespace XBuild
