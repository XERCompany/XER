#include "openai_utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace Content::OpenAI::Utils {

// StringUtils implementation
std::vector<std::string> StringUtils::Tokenize(absl::string_view text, absl::string_view delimiter) {
    return absl::StrSplit(text, delimiter, absl::SkipEmpty());
}

std::vector<std::string> StringUtils::Split(absl::string_view text, absl::string_view delimiter) {
    return absl::StrSplit(text, delimiter);
}

std::string StringUtils::Join(const std::vector<std::string>& parts, absl::string_view delimiter) {
    return absl::StrJoin(parts, delimiter);
}

std::string StringUtils::Replace(absl::string_view text, absl::string_view old, absl::string_view new_str) {
    return absl::StrReplaceAll(text, {{old, new_str}});
}

std::string StringUtils::Trim(absl::string_view text) {
    std::string result(text);
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), result.end());
    return result;
}

std::string StringUtils::ToLower(absl::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string StringUtils::ToUpper(absl::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return result;
}

bool StringUtils::ParseInt(absl::string_view text, int* output) {
    return absl::SimpleAtoi(text, output);
}

bool StringUtils::ParseFloat(absl::string_view text, float* output) {
    return absl::SimpleAtof(text, output);
}

std::string StringUtils::URLEncode(absl::string_view text) {
    std::string encoded;
    absl::WebSafeBase64Escape(text, &encoded);
    return encoded;
}

std::string StringUtils::URLDecode(absl::string_view text) {
    std::string decoded;
    absl::WebSafeBase64Unescape(text, &decoded);
    return decoded;
}

std::string StringUtils::Base64Encode(absl::string_view data) {
    std::string encoded;
    absl::Base64Escape(data, &encoded);
    return encoded;
}

std::string StringUtils::Base64Decode(absl::string_view encoded) {
    std::string decoded;
    absl::Base64Unescape(encoded, &decoded);
    return decoded;
}

bool StringUtils::RegexMatch(absl::string_view text, absl::string_view pattern) {
    re2::RE2 re(pattern);
    return re2::RE2::FullMatch(text, re);
}

std::vector<std::string> StringUtils::RegexExtract(absl::string_view text, absl::string_view pattern) {
    std::vector<std::string> matches;
    re2::RE2 re(pattern);
    re2::StringPiece input(text);
    
    while (re2::RE2::FindAndConsume(&input, re)) {
        // Extract captured groups
        for (int i = 0; i < re.NumberOfCapturingGroups(); ++i) {
            re2::StringPiece match;
            if (re2::RE2::FindAndConsume(&input, re, &match)) {
                matches.push_back(std::string(match));
            }
        }
    }
    
    return matches;
}

// JsonUtils implementation
#ifdef OPENAI_JSONCPP_AVAILABLE
Json::Value JsonUtils::CreateObject() {
    return Json::Value(Json::objectValue);
}

Json::Value JsonUtils::CreateArray() {
    return Json::Value(Json::arrayValue);
}

void JsonUtils::AddToObject(Json::Value& obj, absl::string_view key, const Json::Value& value) {
    obj[std::string(key)] = value;
}

void JsonUtils::AddToArray(Json::Value& arr, const Json::Value& value) {
    arr.append(value);
}

std::string JsonUtils::ToString(const Json::Value& value, bool styled) {
    Json::StreamWriterBuilder builder;
    if (styled) {
        builder["indentation"] = "  ";
    } else {
        builder["indentation"] = "";
    }
    return Json::writeString(builder, value);
}

Json::Value JsonUtils::Parse(const std::string& json_str) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream iss(json_str);
    
    if (!Json::parseFromStream(builder, iss, &root, &errs)) {
        return Json::Value(Json::nullValue);
    }
    
    return root;
}

Json::Value JsonUtils::ExtractValue(const Json::Value& root, absl::string_view key) {
    std::string key_str(key);
    if (root.isMember(key_str)) {
        return root[key_str];
    }
    return Json::Value(Json::nullValue);
}

Json::Value JsonUtils::ExtractNested(const Json::Value& root, const std::vector<std::string>& path) {
    Json::Value current = root;
    
    for (const auto& key : path) {
        if (current.isMember(key)) {
            current = current[key];
        } else {
            return Json::Value(Json::nullValue);
        }
    }
    
    return current;
}

bool JsonUtils::HasKey(const Json::Value& root, absl::string_view key) {
    return root.isMember(std::string(key));
}

std::string JsonUtils::GetString(const Json::Value& root, absl::string_view key, const std::string& default_value) {
    Json::Value value = ExtractValue(root, key);
    if (value.isString()) {
        return value.asString();
    }
    return default_value;
}

int JsonUtils::GetInt(const Json::Value& root, absl::string_view key, int default_value) {
    Json::Value value = ExtractValue(root, key);
    if (value.isInt()) {
        return value.asInt();
    }
    return default_value;
}

float JsonUtils::GetFloat(const Json::Value& root, absl::string_view key, float default_value) {
    Json::Value value = ExtractValue(root, key);
    if (value.isDouble()) {
        return static_cast<float>(value.asDouble());
    }
    return default_value;
}

bool JsonUtils::GetBool(const Json::Value& root, absl::string_view key, bool default_value) {
    Json::Value value = ExtractValue(root, key);
    if (value.isBool()) {
        return value.asBool();
    }
    return default_value;
}

Json::Value JsonUtils::GetArray(const Json::Value& root, absl::string_view key) {
    Json::Value value = ExtractValue(root, key);
    if (value.isArray()) {
        return value;
    }
    return Json::Value(Json::arrayValue);
}

Json::Value JsonUtils::GetObject(const Json::Value& root, absl::string_view key) {
    Json::Value value = ExtractValue(root, key);
    if (value.isObject()) {
        return value;
    }
    return Json::Value(Json::objectValue);
}

#else
// Fallback string-based JSON (simplified)
std::string JsonUtils::EscapeString(absl::string_view str) {
    std::string escaped;
    absl::CEscape(str, &escaped);
    return escaped;
}

std::string JsonUtils::FormatKeyValue(absl::string_view key, absl::string_view value) {
    return absl::StrFormat("\"%s\":\"%s\"", key, EscapeString(value));
}

std::string JsonUtils::FormatArray(const std::vector<std::string>& items) {
    std::vector<std::string> escaped_items;
    for (const auto& item : items) {
        escaped_items.push_back(absl::StrFormat("\"%s\"", EscapeString(item)));
    }
    return absl::StrFormat("[%s]", absl::StrJoin(escaped_items, ","));
}

std::string JsonUtils::ExtractValue(absl::string_view json, absl::string_view key) {
    std::string pattern = absl::StrFormat("\"%s\"\\s*:\\s*\"([^\"]+)\"", key);
    auto matches = StringUtils::RegexExtract(json, pattern);
    return matches.empty() ? "" : matches[0];
}
#endif

// ThreadSafeMap implementation
void ThreadSafeMap::Insert(const std::string& key, const std::string& value) {
    absl::MutexLock lock(&mutex_);
    map_[key] = value;
}

bool ThreadSafeMap::Get(const std::string& key, std::string* value) const {
    absl::MutexLock lock(&mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        *value = it->second;
        return true;
    }
    return false;
}

bool ThreadSafeMap::Erase(const std::string& key) {
    absl::MutexLock lock(&mutex_);
    return map_.erase(key) > 0;
}

void ThreadSafeMap::Clear() {
    absl::MutexLock lock(&mutex_);
    map_.clear();
}

size_t ThreadSafeMap::Size() const {
    absl::MutexLock lock(&mutex_);
    return map_.size();
}

std::vector<std::string> ThreadSafeMap::Keys() const {
    absl::MutexLock lock(&mutex_);
    std::vector<std::string> keys;
    for (const auto& pair : map_) {
        keys.push_back(pair.first);
    }
    return keys;
}

// AsyncUtils implementation
void AsyncUtils::Wait(absl::Notification& notification) {
    notification.WaitForNotification();
}

bool AsyncUtils::WaitFor(absl::Notification& notification, absl::Duration timeout) {
    return notification.WaitForNotificationWithTimeout(timeout);
}

void AsyncUtils::Notify(absl::Notification& notification) {
    notification.Notify();
}

// ChatUtils implementation
std::string ChatUtils::FormatMessages(const std::vector<ChatMessage>& messages) {
    std::vector<std::string> formatted;
    
    for (const auto& msg : messages) {
        std::string role_str;
        switch (msg.role) {
            case MessageRole::kSystem: role_str = "system"; break;
            case MessageRole::kUser: role_str = "user"; break;
            case MessageRole::kAssistant: role_str = "assistant"; break;
            case MessageRole::kTool: role_str = "tool"; break;
        }
        
        std::string content = ExtractText(msg);
        formatted.push_back(absl::StrFormat("{\"role\":\"%s\",\"content\":\"%s\"}",
            role_str, JsonUtils::EscapeString(content)));
    }
    
    return absl::StrFormat("[%s]", absl::StrJoin(formatted, ","));
}

std::string ChatUtils::ExtractText(const ChatMessage& message) {
    if (message.content.empty()) {
        return "";
    }
    return message.content[0].text;
}

bool ChatUtils::ValidateMessage(const ChatMessage& message) {
    if (message.content.empty()) {
        return false;
    }
    return !message.content[0].text.empty();
}

int ChatUtils::EstimateTokens(absl::string_view text) {
    // Simplified token estimation: ~4 characters per token
    return static_cast<int>(text.length() / 4);
}

std::string ChatUtils::TruncateToTokens(absl::string_view text, int max_tokens) {
    int estimated_chars = max_tokens * 4;
    if (static_cast<int>(text.length()) <= estimated_chars) {
        return std::string(text);
    }
    return std::string(text.substr(0, estimated_chars));
}

// ValidationUtils implementation
bool ValidationUtils::ValidateAPIKey(absl::string_view key) {
    // OpenAI API keys start with "sk-" and are at least 20 characters
    if (key.size() < 20) {
        return false;
    }
    return absl::StartsWith(key, "sk-");
}

bool ValidationUtils::ValidateModel(absl::string_view model) {
    // Valid models: gpt-4, gpt-3.5-turbo, etc.
    static const absl::flat_hash_set<std::string> valid_models = {
        "gpt-4", "gpt-4-turbo", "gpt-4o", "gpt-4o-mini",
        "gpt-3.5-turbo", "gpt-3.5-turbo-16k",
        "text-davinci-003", "text-curie-001", "text-babbage-001", "text-ada-001"
    };
    return valid_models.contains(std::string(model));
}

bool ValidationUtils::ValidateURL(absl::string_view url) {
    // Basic URL validation using regex
    static const re2::RE2 url_pattern(R"(^https?://[^\s/$.?#].[^\s]*$)");
    return re2::RE2::FullMatch(url, url_pattern);
}

std::string ValidationUtils::SanitizeInput(absl::string_view input) {
    std::string sanitized(input);
    
    // Remove potential SQL injection patterns
    static const re2::RE2 sql_pattern(R"(\b(SELECT|INSERT|UPDATE|DELETE|DROP|UNION|OR|AND)\b)", re2::RE2::CaseInsensitive);
    re2::RE2::Replace(&sanitized, sql_pattern, "");
    
    // Remove script tags
    static const re2::RE2 script_pattern(R"(<script[^>]*>.*?</script>)", re2::RE2::CaseInsensitive);
    re2::RE2::Replace(&sanitized, script_pattern, "");
    
    return sanitized;
}

bool ValidationUtils::ValidateFilePath(absl::string_view path) {
    // Basic path validation
    if (path.empty()) {
        return false;
    }
    
    // Check for path traversal
    if (absl::StrContains(path, "..")) {
        return false;
    }
    
    // Check for absolute path (if not allowed)
    if (absl::StartsWith(path, "/") || (path.size() >= 2 && path[1] == ':')) {
        return false;
    }
    
    return true;
}

bool ValidationUtils::ContainsMaliciousPattern(absl::string_view text) {
    // Check for common malicious patterns
    static const std::vector<std::string> malicious_patterns = {
        R"(<script[^>]*>)",
        R"(javascript:)",
        R"(onerror\s*=)",
        R"(onload\s*=)",
        R"(eval\s*\()",
        R"(exec\s*\()",
        R"(system\s*\()",
        R"(shell_exec\s*\()",
        R"(passthru\s*\()"
    };
    
    for (const auto& pattern : malicious_patterns) {
        re2::RE2 re(pattern, re2::RE2::CaseInsensitive);
        if (re2::RE2::PartialMatch(text, re)) {
            return true;
        }
    }
    
    return false;
}

}  // namespace Content::OpenAI::Utils
