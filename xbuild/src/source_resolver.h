#pragma once
#include "model.h"
namespace XBuild {
bool ResolveSources(const Project& project, const Json::Value& target, const Environment& environment,
                    std::vector<SourceUnit>& sources, std::string& error);
}
