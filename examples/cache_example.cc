#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>

namespace xer {
namespace cache {

struct CacheEntry {
    std::string key;
    std::string value;
    std::chrono::minutes ttl;
};

class CacheManager {
public:
    void PutTemporary(const std::string& key,
                      const std::string& value,
                      std::chrono::minutes ttl) {
        entries_[key] = CacheEntry{key, value, ttl};
    }

    bool GetTemporary(const std::string& key, std::string* out) const {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            return false;
        }
        *out = it->second.value;
        return true;
    }

private:
    std::unordered_map<std::string, CacheEntry> entries_;
};

}  // namespace cache
}  // namespace xer

int main() {
    xer::cache::CacheManager cache;
    cache.PutTemporary("session_token", "abc123", std::chrono::minutes(30));

    std::string token;
    if (cache.GetTemporary("session_token", &token)) {
        std::cout << "Token: " << token << "\n";
    } else {
        std::cout << "Token not found\n";
    }

    return 0;
}
