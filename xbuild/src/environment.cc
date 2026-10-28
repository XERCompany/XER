#include "environment.h"
#include <absl/strings/ascii.h>
#include <uv.h>

namespace XBuild {
namespace {
std::string Key(std::string name) {
#ifdef _WIN32
    absl::AsciiStrToUpper(&name);
#endif
    return name;
}
}
Environment Environment::Capture() {
    Environment result;
    uv_env_item_t* items = nullptr;
    int count = 0;
    if (uv_os_environ(&items, &count) == 0) {
        for (int i = 0; i < count; ++i) result.Set(items[i].name, items[i].value);
        uv_os_free_environ(items, count);
    }
    return result;
}
bool Environment::Get(const std::string& name, std::string& value) const {
    const auto found = values_.find(Key(name));
    if (found == values_.end()) return false;
    value = found->second;
    return true;
}
void Environment::Set(const std::string& name, const std::string& value) {
    values_[Key(name)] = value;
}
void Environment::PrependPath(const std::string& directory) {
    std::string path;
    Get("PATH", path);
#ifdef _WIN32
    Set("PATH", directory + ";" + path);
#else
    Set("PATH", directory + ":" + path);
#endif
}
std::vector<std::string> Environment::Entries() const {
    std::vector<std::string> entries;
    for (const auto& [key, value] : values_) entries.push_back(key + "=" + value);
    return entries;
}
}
