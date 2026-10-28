#pragma once
#include <map>
#include <string>
#include <vector>

namespace XBuild {
class Environment {
public:
    static Environment Capture();
    bool Get(const std::string& name, std::string& value) const;
    void Set(const std::string& name, const std::string& value);
    void PrependPath(const std::string& directory);
    std::vector<std::string> Entries() const;
private:
    std::map<std::string, std::string> values_;
};
}
