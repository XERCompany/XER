#pragma once
#include "environment.h"
#include <string>
#include <vector>
namespace XBuild {
bool Expand(const std::string& input, const Environment& environment, std::string& output, std::string& error);
bool ExpandList(const std::vector<std::string>& input, const Environment& environment,
                std::vector<std::string>& output, std::string& error);
}
