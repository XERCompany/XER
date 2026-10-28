#include "model.h"
#include "path_utils.h"
#include "project_config.h"
#include <algorithm>
#include <thread>

namespace XBuild {
std::vector<std::string> Strings(const Json::Value& value) {
    std::vector<std::string> result;
    for (const auto& item : value) result.push_back(item.asString());
    return result;
}
bool LoadProject(const fs::path& file, Project& project, std::string& error) {
    if (!LoadProjectConfig(file, project.document, error)) return false;
    project.file = AbsolutePath(fs::current_path(), file);
    project.root = project.file.parent_path();
    const auto& doc = project.document;
    project.build_dir = AbsolutePath(project.root, doc.get("build_dir", ".xbuild").asString());
    if (!IsWithin(project.build_dir, project.root) || project.build_dir == project.root) {
        error = "build_dir must be a dedicated subdirectory of the project.";
        return false;
    }
    project.jobs = doc.get("jobs", std::clamp(std::thread::hardware_concurrency(), 1u, 16u)).asUInt();
    project.timeout_ms = doc.get("timeout_ms", 0).asUInt();
    project.debug = doc.get("debug", false).asBool();
    project.tool = doc.get("tool", "clang").asString();
    const auto policy = doc.get("terimal_sandbox", "Allow").asString();
    if (policy != "Allow" && policy != "Deny") {
        error = "terimal_sandbox must be Allow or Deny.";
        return false;
    }
    project.allow_execution = policy == "Allow";
    for (const auto& name : Strings(doc["require"])) {
        if (name != "async" && name != "regex" && name != "env" &&
            name != "vcpkg" && name != "depot_tools") {
            error = "Unknown required module: " + name;
            return false;
        }
    }
    return true;
}
}
