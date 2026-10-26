#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

#ifdef ENABLE_V8
#include <v8.h>
#endif

#include "helper/jsoncpp_helper.h"
#include "cache/cache_manager.h"

namespace Engine::Helper {

// ---------------------------------------------------------------------------
// Dict — generic key-value storage with JSON serialization,
// async operations, and cache integration
// ---------------------------------------------------------------------------

class Dict {
public:
    // Type-safe value retrieval
    std::optional<bool> GetBool(const std::string& key) const;
    std::optional<int> GetInt(const std::string& key) const;
    std::optional<double> GetDouble(const std::string& key) const;
    std::optional<std::string> GetString(const std::string& key) const;
    std::optional<std::vector<std::string>> GetStringArray(const std::string& key) const;

    // Type-safe value setting
    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int value);
    void SetDouble(const std::string& key, double value);
    void SetString(const std::string& key, const std::string& value);
    void SetStringArray(const std::string& key, const std::vector<std::string>& value);

    // Nested dict operations
    Dict* GetOrCreateDict(const std::string& key);
    const Dict* GetDict(const std::string& key) const;
    bool HasDict(const std::string& key) const;

    // Generic operations
    bool HasKey(const std::string& key) const;
    void Remove(const std::string& key);
    void Clear();
    size_t Size() const;
    std::vector<std::string> Keys() const;

    // JSON serialization (via jsoncpp_helper)
    Json::Value ToJson() const;
    bool FromJson(const Json::Value& json);
    std::string ToJsonString() const;
    bool FromJsonString(const std::string& json_str, std::string* error_out);

    // File operations
    bool SaveToFile(const std::string& filepath, std::string* error_out);
    bool LoadFromFile(const std::string& filepath, std::string* error_out);

    // Cache integration
    bool SaveToCache(absl::string_view scope,
                     absl::string_view key,
                     absl::string_view name,
                     std::string* error_out);
    bool LoadFromCache(absl::string_view scope,
                       absl::string_view key,
                       absl::string_view name,
                       std::string* error_out);

    // Async operations (via async_bridge)
    void AsyncSaveToFile(v8::Isolate* isolate,
                         v8::Local<v8::Function> callback,
                         v8::Local<v8::Context> context,
                         const std::string& filepath);
    void AsyncLoadFromFile(v8::Isolate* isolate,
                          v8::Local<v8::Function> callback,
                          v8::Local<v8::Context> context,
                          const std::string& filepath);

    // Merge operations
    void Merge(const Dict& other);
    void MergeJson(const Json::Value& json);

    // Query operations
    std::vector<std::string> FindKeysMatching(const std::string& pattern) const;
    Dict* GetNestedPath(const std::vector<std::string>& path);
    const Dict* GetNestedPath(const std::vector<std::string>& path) const;

    // Default values
    bool GetBool(const std::string& key, bool default_value) const;
    int GetInt(const std::string& key, int default_value) const;
    double GetDouble(const std::string& key, double default_value) const;
    std::string GetString(const std::string& key, const std::string& default_value) const;

    // Factory
    static std::unique_ptr<Dict> Create();
    static std::unique_ptr<Dict> FromJsonFile(const std::string& filepath, std::string* error_out);

private:
    Dict() = default;

    // Internal storage
    std::map<std::string, std::variant<bool, int, double, std::string, 
                                        std::vector<std::string>, std::unique_ptr<Dict>>> data_;

    // Helper for variant visiting
    struct ValueToJsonVisitor;
    struct JsonToValueConverter;
};

// Helper for V8 integration
v8::Local<v8::Object> DictToV8Object(v8::Isolate* isolate, const Dict& dict);
std::unique_ptr<Dict> V8ObjectToDict(v8::Isolate* isolate, v8::Local<v8::Object> obj);

}  // namespace Engine::Helper
