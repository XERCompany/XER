#include "jsoncpp_helper.h"
#include <fstream>
#include <sstream>

namespace XER::Engine::Helper {

Json::Value JsonCppHelper::Parse(const std::string& json_str) {
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

    std::string errMsg;
    Json::Value root;
    if (!reader->parse(json_str.c_str(), json_str.c_str() + json_str.size(),
                       &root, &errMsg)) {
        throw std::runtime_error(errMsg);
    }
    return root;
}

Json::Value JsonCppHelper::ParseFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return Parse(buffer.str());
}

bool JsonCppHelper::WriteToFile(const Json::Value& root, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filepath);
    }

    Json::StreamWriterBuilder writerBuilder;
    std::unique_ptr<Json::StreamWriter> writer(writerBuilder.newStreamWriter());
    writer->write(root, &file);

    return true;
}

std::string JsonCppHelper::ToJsonString(const Json::Value& root) {
    Json::StreamWriterBuilder builder;
    std::string output = Json::writeString(builder, root);
    return output;
}

std::vector<std::pair<std::string, std::string>>
JsonCppHelper::GetTopLevelKeys(const Json::Value& root) {
    std::vector<std::pair<std::string, std::string>> keys;

    if (!root.isObject()) {
        return keys;
    }

    for (const auto& key : root.getMemberNames()) {
        keys.push_back({key, root[key].asString()});
    }

    return keys;
}

std::map<std::string, std::string> 
JsonCppHelper::GetNestedValues(const Json::Value& root, 
                               const std::vector<std::string>& path) {
    std::map<std::string, std::string> values;
    
    if (path.empty()) {
        return values;
    }
    
    Json::Value current = root;
    for (size_t i = 0; i < path.size(); ++i) {
        const std::string& key = path[i];
        
        if (!current.isObject() || !current.isMember(key)) {
            values[path[0]] = "null";
            break;
        }

        current = current[key];
    }

    values[path[0]] = current.asString();
    return values;
}

void JsonCppHelper::SetNestedValue(Json::Value& root, 
                                   const std::vector<std::string>& path,
                                   const std::string& value) {
    if (path.empty()) {
        root = Json::Value(value);
        return;
    }
    
    Json::Value current = root;
    for (size_t i = 0; i < path.size() - 1; ++i) {
        const std::string& key = path[i];
        
        if (!current.isObject()) {
            current = Json::Value();
        } else if (!current.isMember(key)) {
            current[key] = Json::Value();
        }
        current = current[key];
    }

    const std::string& lastKey = path.back();
    if (current.isObject() && current.isMember(lastKey)) {
        current[lastKey] = value;
    } else {
        current = Json::Value(value);
    }
}

bool JsonCppHelper::GetBool(const Json::Value& value, bool default_value) {
    return value.isBool() ? value.asBool() : default_value;
}

int JsonCppHelper::GetInt(const Json::Value& value, int default_value) {
    if (value.isInt()) {
        return value.asInt();
    } else if (value.isDouble()) {
        return static_cast<int>(value.asDouble());
    }
    return default_value;
}

double JsonCppHelper::GetDouble(const Json::Value& value, double default_value) {
    if (value.isDouble()) {
        return value.asDouble();
    } else if (value.isInt()) {
        return static_cast<double>(value.asInt());
    }
    return default_value;
}

std::string JsonCppHelper::GetString(const Json::Value& value, 
                                     const std::string& default_value) {
    if (value.isString()) {
        return value.asString();
    }
    return default_value;
}

bool JsonCppHelper::ContainsKey(const Json::Value& root, const std::string& key) {
    if (!root.isObject()) {
        return false;
    }
    return root.isMember(key);
}

bool JsonCppHelper::IsObject(const Json::Value& value) {
    return value.isObject();
}

bool JsonCppHelper::IsArray(const Json::Value& value) {
    return value.isArray();
}

bool JsonCppHelper::IsString(const Json::Value& value) {
    return value.isString();
}

bool JsonCppHelper::IsNumber(const Json::Value& value) {
    return value.isInt() || value.isDouble();
}

std::string JsonCppHelper::EscapeJsonString(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:   escaped += c; break;
        }
    }
    return escaped;
}

}  // namespace XER::Engine::Helper
