#pragma once
#include "model.h"
#include <string>
#include <vector>

namespace XBuild {

struct LinkResult {
    bool success = false;
    std::string output;
    std::string error;
    std::string executable;
};

// Link object files into an executable
bool LinkTarget(const Project& project,
                const Json::Value& target,
                const std::vector<std::string>& object_files,
                LinkResult& result);

// Link multiple targets
bool LinkTargets(const Project& project,
                 const Json::Value& targets,
                 const std::vector<std::vector<std::string>>& object_files,
                 std::vector<LinkResult>& results);

} // namespace XBuild
