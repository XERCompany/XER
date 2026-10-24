#pragma once

#include "openai_types.h"
#include <range/v3/all.hpp>
#include <absl/strings/string_view.h>
#include <absl/strings/str_split.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_replace.h>
#include <absl/strings/numbers.h>
#include <absl/strings/escaping.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/synchronization/mutex.h>
#include <absl/synchronization/notification.h>
#include <re2/re2.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace Content::OpenAI::Utils {

/**
 * String utilities using absl and re2
 */
class StringUtils {
public:
    // Tokenization using absl
    static std::vector<std::string> Tokenize(absl::string_view text, absl::string_view delimiter = " ");
    
    // Split using absl
    static std::vector<std::string> Split(absl::string_view text, absl::string_view delimiter);
    
    // Join using absl
    static std::string Join(const std::vector<std::string>& parts, absl::string_view delimiter);
    
    // Replace using absl
    static std::string Replace(absl::string_view text, absl::string_view old, absl::string_view new_str);
    
    // Trim whitespace
    static std::string Trim(absl::string_view text);
    
    // Case conversion
    static std::string ToLower(absl::string_view text);
    static std::string ToUpper(absl::string_view text);
    
    // Number parsing
    static bool ParseInt(absl::string_view text, int* output);
    static bool ParseFloat(absl::string_view text, float* output);
    
    // URL encoding/decoding
    static std::string URLEncode(absl::string_view text);
    static std::string URLDecode(absl::string_view text);
    
    // Base64 encoding/decoding
    static std::string Base64Encode(absl::string_view data);
    static std::string Base64Decode(absl::string_view encoded);
    
    // Regex matching using re2
    static bool RegexMatch(absl::string_view text, absl::string_view pattern);
    static std::vector<std::string> RegexExtract(absl::string_view text, absl::string_view pattern);
};

/**
 * JSON utilities (simplified using absl and re2)
 */
class JsonUtils {
public:
    // Escape JSON string
    static std::string EscapeString(absl::string_view str);
    
    // Format JSON key-value
    static std::string FormatKeyValue(absl::string_view key, absl::string_view value);
    
    // Format JSON array
    static std::string FormatArray(const std::vector<std::string>& items);
    
    // Parse simple JSON (simplified)
    static std::string ExtractValue(absl::string_view json, absl::string_view key);
};

/**
 * Processing utilities using range-v3
 */
class RangeUtils {
public:
    // Filter and transform using ranges
    template<typename Container, typename Pred, typename Transform>
    static auto FilterTransform(const Container& c, Pred pred, Transform transform) {
        return c | ranges::views::filter(pred) | ranges::views::transform(transform);
    }
    
    // Map using ranges
    template<typename Container, typename Transform>
    static auto Map(const Container& c, Transform transform) {
        return c | ranges::views::transform(transform);
    }
    
    // Filter using ranges
    template<typename Container, typename Pred>
    static auto Filter(const Container& c, Pred pred) {
        return c | ranges::views::filter(pred);
    }
    
    // Reduce using ranges
    template<typename Container, typename Init, typename Op>
    static auto Reduce(const Container& c, Init init, Op op) {
        return ranges::accumulate(c, init, op);
    }
    
    // Sort using ranges
    template<typename Container, typename Comp>
    static auto Sort(Container c, Comp comp) {
        ranges::sort(c, comp);
        return c;
    }
    
    // Take first N elements
    template<typename Container>
    static auto Take(Container c, size_t n) {
        return c | ranges::views::take(n);
    }
    
    // Skip first N elements
    template<typename Container>
    static auto Skip(Container c, size_t n) {
        return c | ranges::views::drop(n);
    }
    
    // Distinct elements
    template<typename Container>
    static auto Distinct(Container c) {
        return c | ranges::views::unique;
    }
};

/**
 * Thread-safe container using absl
 */
class ThreadSafeMap {
public:
    void Insert(const std::string& key, const std::string& value);
    bool Get(const std::string& key, std::string* value) const;
    bool Erase(const std::string& key);
    void Clear();
    size_t Size() const;
    std::vector<std::string> Keys() const;

private:
    mutable absl::Mutex mutex_;
    absl::flat_hash_map<std::string, std::string> map_;
};

/**
 * Async utilities using absl notification
 */
class AsyncUtils {
public:
    static void Wait(absl::Notification& notification);
    static bool WaitFor(absl::Notification& notification, absl::Duration timeout);
    static void Notify(absl::Notification& notification);
};

/**
 * Chat message utilities
 */
class ChatUtils {
public:
    // Format messages for API
    static std::string FormatMessages(const std::vector<ChatMessage>& messages);
    
    // Extract content from message
    static std::string ExtractText(const ChatMessage& message);
    
    // Validate message
    static bool ValidateMessage(const ChatMessage& message);
    
    // Estimate token count (simplified)
    static int EstimateTokens(absl::string_view text);
    
    // Truncate to max tokens
    static std::string TruncateToTokens(absl::string_view text, int max_tokens);
};

/**
 * Validation utilities
 */
class ValidationUtils {
public:
    // Validate API key format
    static bool ValidateAPIKey(absl::string_view key);
    
    // Validate model name
    static bool ValidateModel(absl::string_view model);
    
    // Validate URL
    static bool ValidateURL(absl::string_view url);
    
    // Sanitize user input
    static std::string SanitizeInput(absl::string_view input);
    
    // Validate file path
    static bool ValidateFilePath(absl::string_view path);
    
    // Check for malicious patterns
    static bool ContainsMaliciousPattern(absl::string_view text);
};

}  // namespace Content::OpenAI::Utils
