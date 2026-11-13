#include "ecosystem/ecosystem_manifest_parser.h" 
#include "ecosystem/ecosystem_manifest_args.h"
#include "helper/env_utils.h"

#include <json/json.h>
#include <memory>
#include <cstdlib>
#include <regex>
#include <string>

#include <range/v3/algorithm/all_of.hpp>

// ---------------------------------------------------------------------------
// Environment variable substitution: expands ${VAR_NAME} in a string.
// Unknown variables are left as-is.
// ---------------------------------------------------------------------------
namespace {

std::string ExpandEnvVars(const std::string& input) {
    static const std::regex kVarPattern(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})");
    std::string result;
    result.reserve(input.size());

    auto it = input.cbegin();
    std::sregex_iterator match_it(input.cbegin(), input.cend(), kVarPattern);
    const std::sregex_iterator match_end;

    for (; match_it != match_end; ++match_it) {
        const std::smatch& match = *match_it;
        // Append the part before this match.
        result.append(it, input.cbegin() + match.position());
        const std::string var_name = match[1].str();
        const std::string value = XER::Helper::GetEnv(var_name);
        if (!value.empty()) {
            result.append(value);
        } else {
            // Unknown variable — preserve the original token.
            result.append(match[0].str());
        }
        it = input.cbegin() + match.position() + match.length();
    }
    result.append(it, input.cend());
    return result;
}

}  // namespace

bool EcoSystemManifestParser::Parse(const std::string& json_text,
                                    EcoSystemManifest* out,
                                    std::string* error_message) {
    if (out == nullptr) {
        if (error_message) *error_message = "Manifest output target is null";
        return false;
    }

    *out = EcoSystemManifest{};

    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    Json::Value root;
    std::string parse_errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader || !reader->parse(
            json_text.data(),
            json_text.data() + json_text.size(),
            &root,
            &parse_errors)) {
        if (error_message) {
            *error_message = "Failed to parse ecosystem JSON: " + parse_errors;
        }
        return false;
    }

    if (!root.isObject()) {
        if (error_message) {
            *error_message = "ecosystem manifest root must be a JSON object";
        }
        return false;
    }

    const Json::Value script_value = root["script"];
    if (!script_value.isString()) {
        if (error_message) {
            *error_message = "ecosystem manifest must contain string field: script";
        }
        return false;
    }
    out->script_path = ExpandEnvVars(script_value.asString());

    const Json::Value args_value = root["args"];
    if (args_value.isArray()) {
        if (!ranges::all_of(args_value, [](const Json::Value& item) { return item.isString(); })) {
            if (error_message) {
                *error_message = "ecosystem args array must contain only strings";
            }
            return false;
        }

        out->args.reserve(static_cast<size_t>(args_value.size()));
        for (const auto& item : args_value) {
            out->args.push_back(ExpandEnvVars(item.asString()));
        }
        return true;
    }

    if (args_value.isString()) {
        const auto raw_args = EcoSystemManifestArgs::Split(args_value.asString());
        out->args.reserve(raw_args.size());
        for (const auto& arg : raw_args) {
            out->args.push_back(ExpandEnvVars(arg));
        }
    } else if (!args_value.isNull()) {
        if (error_message) {
            *error_message = "ecosystem field 'args' must be string or string[]";
        }
        return false;
    }

    return true;
}

