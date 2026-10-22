#ifndef XER_JSON_CONFIG_HELPER_H
#define XER_JSON_CONFIG_HELPER_H

#include <filesystem>
#include <string>
#include <vector>
#include <json/json.h>

namespace XER {

// Helper functions for JSON configuration parsing
class JsonConfigHelper {
public:
    // Find a JSON value by trying multiple possible field names
    static const Json::Value* FindJsonConfigValue(
        const Json::Value& root,
        const std::vector<std::string>& names);

    // Get a string value with fallback
    static std::string GetJsonConfigString(
        const Json::Value& root,
        const std::vector<std::string>& names,
        const std::string& fallback = {});

    // Get an integer value with fallback
    static int GetJsonConfigInt(
        const Json::Value& root,
        const std::vector<std::string>& names,
        int fallback);

    // Get a boolean value with fallback
    static bool GetJsonConfigBool(
        const Json::Value& root,
        const std::vector<std::string>& names,
        bool fallback);

    // Get a list of strings (array or CSV)
    static std::vector<std::string> GetJsonConfigStringList(
        const Json::Value& root,
        const std::vector<std::string>& names);

    // Read and parse a JSON config file
    static bool ReadJsonConfigFile(
        const std::filesystem::path& path,
        Json::Value* root_out,
        std::string* error_out);

    // Normalize config token (lowercase, alphanumeric only)
    static std::string NormalizeConfigToken(const std::string& text);

    // Trim whitespace from string
    static std::string TrimCopy(const std::string& text);

    // Parse CSV string into vector
    static void AppendCsvStrings(
        const std::string& text,
        std::vector<std::string>* output);
};

} // namespace XER

#endif // XER_JSON_CONFIG_HELPER_H
