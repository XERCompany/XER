#pragma once

#include <string>
#include <vector>
#include <map>
#include <json/json_value.h>
#include <json/reader.h>
#include <json/writer.h>

namespace Engine::Helper {

class JsonCppHelper {
public:
    static Json::Value Parse(const std::string& json_str);
    static Json::Value ParseFile(const std::string& filepath);
    
    static bool WriteToFile(const Json::Value& root, const std::string& filepath);
    
    static std::string ToJsonString(const Json::Value& root);
    
    static std::vector<std::pair<std::string, std::string>> GetTopLevelKeys(const Json::Value& root);
    
    static std::map<std::string, std::string> GetNestedValues(
        const Json::Value& root, 
        const std::vector<std::string>& path);
    
    static void SetNestedValue(Json::Value& root, 
                               const std::vector<std::string>& path,
                               const std::string& value);
    
    static bool GetBool(const Json::Value& value, bool default_value = false);
    static int GetInt(const Json::Value& value, int default_value = 0);
    static double GetDouble(const Json::Value& value, double default_value = 0.0);
    static std::string GetString(const Json::Value& value, const std::string& default_value = "");
    
    static bool ContainsKey(const Json::Value& root, const std::string& key);
    static bool IsObject(const Json::Value& value);
    static bool IsArray(const Json::Value& value);
    static bool IsString(const Json::Value& value);
    static bool IsNumber(const Json::Value& value);
    
private:
    static std::string EscapeJsonString(const std::string& str);
};

}  // namespace Engine::Helper
