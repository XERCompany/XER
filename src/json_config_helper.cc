#include "json_config_helper.h"
#include "helper/string_utils.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace XER {

const Json::Value* JsonConfigHelper::FindJsonConfigValue(
    const Json::Value& root,
    const std::vector<std::string>& names) {
    if (!root.isObject()) {
        return nullptr;
    }
    for (const std::string& name : names) {
        if (root.isMember(name)) {
            return &root[name];
        }
    }
    return nullptr;
}

std::string JsonConfigHelper::GetJsonConfigString(
    const Json::Value& root,
    const std::vector<std::string>& names,
    const std::string& fallback) {
    const Json::Value* value = FindJsonConfigValue(root, names);
    if (value == nullptr || value->isNull()) {
        return fallback;
    }
    if (value->isString()) {
        return value->asString();
    }
    return fallback;
}

int JsonConfigHelper::GetJsonConfigInt(
    const Json::Value& root,
    const std::vector<std::string>& names,
    int fallback) {
    const Json::Value* value = FindJsonConfigValue(root, names);
    if (value == nullptr || value->isNull()) {
        return fallback;
    }
    if (value->isInt()) {
        return value->asInt();
    }
    if (value->isUInt()) {
        return static_cast<int>(value->asUInt());
    }
    if (value->isString()) {
        try {
            return std::stoi(value->asString());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool JsonConfigHelper::GetJsonConfigBool(
    const Json::Value& root,
    const std::vector<std::string>& names,
    bool fallback) {
    const Json::Value* value = FindJsonConfigValue(root, names);
    if (value == nullptr || value->isNull()) {
        return fallback;
    }
    if (value->isBool()) {
        return value->asBool();
    }
    if (value->isString()) {
        const std::string normalized = NormalizeConfigToken(value->asString());
        if (normalized == "true" || normalized == "yes" || normalized == "on" || normalized == "1") {
            return true;
        }
        if (normalized == "false" || normalized == "no" || normalized == "off" || normalized == "0") {
            return false;
        }
    }
    return fallback;
}

std::vector<std::string> JsonConfigHelper::GetJsonConfigStringList(
    const Json::Value& root,
    const std::vector<std::string>& names) {
    std::vector<std::string> values;
    const Json::Value* value = FindJsonConfigValue(root, names);
    if (value == nullptr || value->isNull()) {
        return values;
    }
    if (value->isArray()) {
        for (const Json::Value& item : *value) {
            if (item.isString()) {
                const std::string text = XER::Helper::String::TrimAsciiWhitespaceCopy(item.asString());
                if (!text.empty()) {
                    values.push_back(text);
                }
            }
        }
        return values;
    }
    if (value->isString()) {
        AppendCsvStrings(value->asString(), &values);
    }
    return values;
}

bool JsonConfigHelper::ReadJsonConfigFile(
    const std::filesystem::path& path,
    Json::Value* root_out,
    std::string* error_out) {
    if (root_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "JSON config target is invalid";
        }
        return false;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        if (error_out != nullptr) {
            *error_out = "failed to open JSON config: " + path.string();
        }
        return false;
    }
    Json::CharReaderBuilder builder;
    std::string parse_errors;
    if (!Json::parseFromStream(builder, input, root_out, &parse_errors)) {
        if (error_out != nullptr) {
            *error_out = "failed to parse JSON config: " + parse_errors;
        }
        return false;
    }
    if (!root_out->isObject()) {
        if (error_out != nullptr) {
            *error_out = "JSON config root must be an object";
        }
        return false;
    }
    return true;
}

std::string JsonConfigHelper::NormalizeConfigToken(const std::string& text) {
    const std::string lowered = XER::Helper::String::ToLowerAscii(text);
    std::string normalized;
    normalized.reserve(lowered.size());
    for (unsigned char ch : lowered) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(ch));
        }
    }
    return normalized;
}

void JsonConfigHelper::AppendCsvStrings(
    const std::string& text,
    std::vector<std::string>* output) {
    if (output == nullptr) {
        return;
    }
    std::string token;
    for (char ch : text) {
        if (ch == ',') {
            const std::string trimmed = XER::Helper::String::TrimAsciiWhitespaceCopy(token);
            if (!trimmed.empty()) {
                output->push_back(trimmed);
            }
            token.clear();
            continue;
        }
        token.push_back(ch);
    }
    const std::string trimmed = XER::Helper::String::TrimAsciiWhitespaceCopy(token);
    if (!trimmed.empty()) {
        output->push_back(trimmed);
    }
}

} // namespace XER
