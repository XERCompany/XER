#pragma once
#include <memory>
#include <re2/re2.h>
#include <string>
namespace XBuild {
class RegexFilter {
public:
    bool Parse(const std::string& expression, std::string& error);
    bool Matches(const std::string& name) const;
private:
    std::unique_ptr<RE2> regex_;
};
}
