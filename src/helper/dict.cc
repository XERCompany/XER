#include "dict.h"
#include <stdexcept>
#include <regex>

#ifdef ENABLE_V8
#include "async_io/io_thread_pool.h"
#include "helper/async_bridge.h"
#endif

namespace Engine::Helper {

// ---------------------------------------------------------------------------
// Type-safe value retrieval
// ---------------------------------------------------------------------------

std::optional<bool> Dict::GetBool(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (std::holds_alternative<bool>(it->second)) {
        return std::get<bool>(it->second);
    }
    return std::nullopt;
}

std::optional<int> Dict::GetInt(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (std::holds_alternative<int>(it->second)) {
        return std::get<int>(it->second);
    }
    if (std::holds_alternative<double>(it->second)) {
        return static_cast<int>(std::get<double>(it->second));
    }
    return std::nullopt;
}

std::optional<double> Dict::GetDouble(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (std::holds_alternative<double>(it->second)) {
        return std::get<double>(it->second);
    }
    if (std::holds_alternative<int>(it->second)) {
        return static_cast<double>(std::get<int>(it->second));
    }
    return std::nullopt;
}

std::optional<std::string> Dict::GetString(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (std::holds_alternative<std::string>(it->second)) {
        return std::get<std::string>(it->second);
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> Dict::GetStringArray(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    if (std::holds_alternative<std::vector<std::string>>(it->second)) {
        return std::get<std::vector<std::string>>(it->second);
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Type-safe value setting
// ---------------------------------------------------------------------------

void Dict::SetBool(const std::string& key, bool value) {
    data_[key] = value;
}

void Dict::SetInt(const std::string& key, int value) {
    data_[key] = value;
}

void Dict::SetDouble(const std::string& key, double value) {
    data_[key] = value;
}

void Dict::SetString(const std::string& key, const std::string& value) {
    data_[key] = value;
}

void Dict::SetStringArray(const std::string& key, const std::vector<std::string>& value) {
    data_[key] = value;
}

// ---------------------------------------------------------------------------
// Nested dict operations
// ---------------------------------------------------------------------------

Dict* Dict::GetOrCreateDict(const std::string& key) {
    auto it = data_.find(key);
    if (it != data_.end() && std::holds_alternative<std::unique_ptr<Dict>>(it->second)) {
        return std::get<std::unique_ptr<Dict>>(it->second).get();
    }
    auto new_dict = std::make_unique<Dict>();
    Dict* ptr = new_dict.get();
    data_[key] = std::move(new_dict);
    return ptr;
}

const Dict* Dict::GetDict(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    if (std::holds_alternative<std::unique_ptr<Dict>>(it->second)) {
        return std::get<std::unique_ptr<Dict>>(it->second).get();
    }
    return nullptr;
}

bool Dict::HasDict(const std::string& key) const {
    return GetDict(key) != nullptr;
}

// ---------------------------------------------------------------------------
// Generic operations
// ---------------------------------------------------------------------------

bool Dict::HasKey(const std::string& key) const {
    return data_.find(key) != data_.end();
}

void Dict::Remove(const std::string& key) {
    data_.erase(key);
}

void Dict::Clear() {
    data_.clear();
}

size_t Dict::Size() const {
    return data_.size();
}

std::vector<std::string> Dict::Keys() const {
    std::vector<std::string> keys;
    for (const auto& pair : data_) {
        keys.push_back(pair.first);
    }
    return keys;
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

struct Dict::ValueToJsonVisitor {
    Json::Value operator()(bool value) const { return Json::Value(value); }
    Json::Value operator()(int value) const { return Json::Value(value); }
    Json::Value operator()(double value) const { return Json::Value(value); }
    Json::Value operator()(const std::string& value) const { return Json::Value(value); }
    Json::Value operator()(const std::vector<std::string>& value) const {
        Json::Value arr(Json::arrayValue);
        for (const auto& str : value) {
            arr.append(Json::Value(str));
        }
        return arr;
    }
    Json::Value operator()(const std::unique_ptr<Dict>& value) const {
        if (value) return value->ToJson();
        return Json::Value(Json::nullValue);
    }
};

Json::Value Dict::ToJson() const {
    Json::Value root(Json::objectValue);
    for (const auto& pair : data_) {
        root[pair.first] = std::visit(ValueToJsonVisitor{}, pair.second);
    }
    return root;
}

bool Dict::FromJson(const Json::Value& json) {
    if (!json.isObject()) return false;

    for (const auto& key : json.getMemberNames()) {
        const Json::Value& value = json[key];

        if (value.isBool()) {
            data_[key] = value.asBool();
        } else if (value.isInt()) {
            data_[key] = value.asInt();
        } else if (value.isDouble()) {
            data_[key] = value.asDouble();
        } else if (value.isString()) {
            data_[key] = value.asString();
        } else if (value.isArray()) {
            std::vector<std::string> arr;
            for (const auto& item : value) {
                if (item.isString()) {
                    arr.push_back(item.asString());
                }
            }
            data_[key] = arr;
        } else if (value.isObject()) {
            auto nested_dict = std::make_unique<Dict>();
            if (nested_dict->FromJson(value)) {
                data_[key] = std::move(nested_dict);
            }
        }
    }
    return true;
}

std::string Dict::ToJsonString() const {
    return JsonCppHelper::ToJsonString(ToJson());
}

bool Dict::FromJsonString(const std::string& json_str, std::string* error_out) {
    try {
        Json::Value root = JsonCppHelper::Parse(json_str);
        return FromJson(root);
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

bool Dict::SaveToFile(const std::string& filepath, std::string* error_out) {
    try {
        return JsonCppHelper::WriteToFile(ToJson(), filepath);
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return false;
    }
}

bool Dict::LoadFromFile(const std::string& filepath, std::string* error_out) {
    try {
        Json::Value root = JsonCppHelper::ParseFile(filepath);
        return FromJson(root);
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Cache integration
// ---------------------------------------------------------------------------

bool Dict::SaveToCache(absl::string_view scope,
                       absl::string_view key,
                       absl::string_view name,
                       std::string* error_out) {
    try {
        std::string json_str = ToJsonString();
        return Cache::CacheManager::Instance().WritePersistentText(
            scope, key, name, json_str, error_out);
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return false;
    }
}

bool Dict::LoadFromCache(absl::string_view scope,
                         absl::string_view key,
                         absl::string_view name,
                         std::string* error_out) {
    try {
        std::string json_str;
        if (!Cache::CacheManager::Instance().ReadPersistentText(
                scope, key, name, &json_str, error_out)) {
            return false;
        }
        return FromJsonString(json_str, error_out);
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Async operations
// ---------------------------------------------------------------------------

#ifdef ENABLE_V8
void Dict::AsyncSaveToFile(v8::Isolate* isolate,
                          v8::Local<v8::Function> callback,
                          v8::Local<v8::Context> context,
                          const std::string& filepath) {
    auto json_data = ToJsonString();
    
    PostAsyncVoid(isolate, callback, context,
        [filepath, json_data](std::string& out_error) -> bool {
            try {
                Json::Value root = JsonCppHelper::Parse(json_data);
                return JsonCppHelper::WriteToFile(root, filepath);
            } catch (const std::exception& e) {
                out_error = e.what();
                return false;
            }
        });
}

void Dict::AsyncLoadFromFile(v8::Isolate* isolate,
                             v8::Local<v8::Function> callback,
                             v8::Local<v8::Context> context,
                             const std::string& filepath) {
    PostAsync<std::string>(isolate, callback, context,
        [filepath](std::string& out_error) -> std::string {
            try {
                Json::Value root = JsonCppHelper::ParseFile(filepath);
                Json::StreamWriterBuilder builder;
                return Json::writeString(builder, root);
            } catch (const std::exception& e) {
                out_error = e.what();
                return "";
            }
        },
        [this](v8::Isolate* iso, const std::string& json_str) -> v8::Local<v8::Value> {
            std::string error;
            if (!FromJsonString(json_str, &error)) {
                return v8::Undefined(iso);
            }
            return DictToV8Object(iso, *this);
        });
}
#endif

// ---------------------------------------------------------------------------
// Merge operations
// ---------------------------------------------------------------------------

void Dict::Merge(const Dict& other) {
    for (const auto& pair : other.data_) {
        data_[pair.first] = pair.second;
    }
}

void Dict::MergeJson(const Json::Value& json) {
    Dict temp;
    if (temp.FromJson(json)) {
        Merge(temp);
    }
}

// ---------------------------------------------------------------------------
// Query operations
// ---------------------------------------------------------------------------

std::vector<std::string> Dict::FindKeysMatching(const std::string& pattern) const {
    std::vector<std::string> matches;
    try {
        std::regex regex_pattern(pattern);
        for (const auto& pair : data_) {
            if (std::regex_search(pair.first, regex_pattern)) {
                matches.push_back(pair.first);
            }
        }
    } catch (const std::regex_error&) {
        // Invalid regex, return empty
    }
    return matches;
}

Dict* Dict::GetNestedPath(const std::vector<std::string>& path) {
    if (path.empty()) return this;
    
    Dict* current = this;
    for (const auto& key : path) {
        current = current->GetOrCreateDict(key);
        if (!current) return nullptr;
    }
    return current;
}

const Dict* Dict::GetNestedPath(const std::vector<std::string>& path) const {
    if (path.empty()) return this;
    
    const Dict* current = this;
    for (const auto& key : path) {
        current = current->GetDict(key);
        if (!current) return nullptr;
    }
    return current;
}

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

bool Dict::GetBool(const std::string& key, bool default_value) const {
    auto opt = GetBool(key);
    return opt.value_or(default_value);
}

int Dict::GetInt(const std::string& key, int default_value) const {
    auto opt = GetInt(key);
    return opt.value_or(default_value);
}

double Dict::GetDouble(const std::string& key, double default_value) const {
    auto opt = GetDouble(key);
    return opt.value_or(default_value);
}

std::string Dict::GetString(const std::string& key, const std::string& default_value) const {
    auto opt = GetString(key);
    return opt.value_or(default_value);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<Dict> Dict::Create() {
    return std::make_unique<Dict>();
}

std::unique_ptr<Dict> Dict::FromJsonFile(const std::string& filepath, std::string* error_out) {
    auto dict = Create();
    if (!dict->LoadFromFile(filepath, error_out)) {
        return nullptr;
    }
    return dict;
}

// ---------------------------------------------------------------------------
// V8 integration helpers
// ---------------------------------------------------------------------------

#ifdef ENABLE_V8
v8::Local<v8::Object> DictToV8Object(v8::Isolate* isolate, const Dict& dict) {
    v8::Local<v8::Object> obj = v8::Object::New(isolate);
    auto keys = dict.Keys();
    
    for (const auto& key : keys) {
        v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate, key.c_str()).ToLocalChecked();
        
        if (auto bool_val = dict.GetBool(key)) {
            obj->Set(isolate->GetCurrentContext(), v8_key, v8::Boolean::New(isolate, *bool_val));
        } else if (auto int_val = dict.GetInt(key)) {
            obj->Set(isolate->GetCurrentContext(), v8_key, v8::Number::New(isolate, *int_val));
        } else if (auto double_val = dict.GetDouble(key)) {
            obj->Set(isolate->GetCurrentContext(), v8_key, v8::Number::New(isolate, *double_val));
        } else if (auto str_val = dict.GetString(key)) {
            obj->Set(isolate->GetCurrentContext(), v8_key, v8::String::NewFromUtf8(isolate, str_val->c_str()).ToLocalChecked());
        } else if (auto arr_val = dict.GetStringArray(key)) {
            v8::Local<v8::Array> arr = v8::Array::New(isolate, arr_val->size());
            for (size_t i = 0; i < arr_val->size(); ++i) {
                arr->Set(isolate->GetCurrentContext(), i, 
                        v8::String::NewFromUtf8(isolate, (*arr_val)[i].c_str()).ToLocalChecked());
            }
            obj->Set(isolate->GetCurrentContext(), v8_key, arr);
        } else if (dict.HasDict(key)) {
            const Dict* nested = dict.GetDict(key);
            if (nested) {
                obj->Set(isolate->GetCurrentContext(), v8_key, DictToV8Object(isolate, *nested));
            }
        }
    }
    
    return obj;
}

std::unique_ptr<Dict> V8ObjectToDict(v8::Isolate* isolate, v8::Local<v8::Object> obj) {
    auto dict = Dict::Create();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    
    v8::Local<v8::Array> property_names = obj->GetPropertyNames(context).ToLocalChecked();
    for (uint32_t i = 0; i < property_names->Length(); ++i) {
        v8::Local<v8::Value> key = property_names->Get(context, i).ToLocalChecked();
        v8::Local<v8::Value> value = obj->Get(context, key).ToLocalChecked();
        
        std::string key_str;
        if (key->IsString()) {
            v8::String::Utf8Value utf8(isolate, key);
            key_str = *utf8;
        }
        
        if (value->IsBoolean()) {
            dict->SetBool(key_str, value->BooleanValue(isolate));
        } else if (value->IsInt32()) {
            dict->SetInt(key_str, value->Int32Value(context).FromMaybe(0));
        } else if (value->IsNumber()) {
            dict->SetDouble(key_str, value->NumberValue(context).FromMaybe(0.0));
        } else if (value->IsString()) {
            v8::String::Utf8Value utf8(isolate, value);
            dict->SetString(key_str, *utf8);
        } else if (value->IsArray()) {
            v8::Local<v8::Array> arr = value.As<v8::Array>();
            std::vector<std::string> str_arr;
            for (uint32_t j = 0; j < arr->Length(); ++j) {
                v8::Local<v8::Value> elem = arr->Get(context, j).ToLocalChecked();
                if (elem->IsString()) {
                    v8::String::Utf8Value utf8_elem(isolate, elem);
                    str_arr.push_back(*utf8_elem);
                }
            }
            dict->SetStringArray(key_str, str_arr);
        } else if (value->IsObject()) {
            auto nested_dict = V8ObjectToDict(isolate, value.As<v8::Object>());
            auto dict_ptr = nested_dict.get();
            // Store the nested dict - need to move it into the variant
            // This is a bit tricky with the variant, so we'll just merge the values
            for (const auto& nk : nested_dict->Keys()) {
                if (auto nv = nested_dict->GetString(nk)) {
                    dict->SetString(key_str + "." + nk, *nv);
                }
            }
        }
    }
    
    return dict;
}
#endif

}  // namespace Engine::Helper
