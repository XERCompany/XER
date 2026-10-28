#include "linker.h"
#include "expressions.h"
#include "path_utils.h"
#include "environment.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>

#include <filesystem>
#include <string>
#include <vector>

namespace XBuild {
namespace fs = std::filesystem;

namespace {

std::string GetLinkerCommand(const std::string& tool) {
    if (tool == "msvc") {
        return "link.exe";
    }
    return "clang++";
}

std::vector<std::string> BuildLinkArgs(const Project& project,
                                        const Json::Value& target,
                                        const std::vector<std::string>& object_files,
                                        const fs::path& output) {
    std::vector<std::string> args;
    const bool is_msvc = project.tool == "msvc";

    args.push_back(GetLinkerCommand(project.tool));

    // Add output file
    if (is_msvc) {
        args.push_back("/OUT:" + output.string());
    } else {
        args.push_back("-o");
        args.push_back(output.string());
    }

    // Add object files
    for (const auto& obj : object_files) {
        args.push_back(obj);
    }

    // Add target-specific libraries
    for (const auto& lib : Strings(target.get("links_libs_target", Json::Value(Json::arrayValue)))) {
        args.push_back(lib);
    }

    // Add project-level libraries
    for (const auto& lib : Strings(project.document["link_libs"])) {
        args.push_back(lib);
    }

    // Add libs from project.json
    for (const auto& lib : Strings(project.document["libs"])) {
        args.push_back(lib);
    }

    // Add vcpkg library directories
    for (const auto& dir : Strings(project.document.get("directory_link_libs", Json::Value(Json::arrayValue)))) {
        Environment env = Environment::Capture();
        std::string expanded_dir;
        std::string error;
        if (Expand(dir, env, expanded_dir, error)) {
            if (is_msvc) {
                args.push_back("/LIBPATH:" + expanded_dir);
            } else {
                args.push_back("-L" + expanded_dir);
            }
        }
    }

    // Add vcpkg include directories to link path
    for (const auto& dir : Strings(project.document.get("vcpkg_link_dirs", Json::Value(Json::arrayValue)))) {
        if (is_msvc) {
            args.push_back("/LIBPATH:" + dir);
        } else {
            args.push_back("-L" + dir);
        }
    }

    // Add library names (without .lib/.a prefix)
    for (const auto& lib : Strings(project.document.get("vcpkg_packages", Json::Value(Json::arrayValue)))) {
        if (is_msvc) {
            args.push_back(lib + ".lib");
        } else {
            args.push_back("-l" + lib);
        }
    }

    // Add debug flags
    if (project.debug) {
        if (is_msvc) {
            args.push_back("/DEBUG");
        } else {
            args.push_back("-g");
        }
    }

    return args;
}

std::string JoinArgs(const std::vector<std::string>& args) {
    std::string result;
    for (const auto& arg : args) {
        if (!result.empty()) result += " ";
        // Quote arguments with spaces
        if (arg.find(' ') != std::string::npos) {
            result += "\"" + arg + "\"";
        } else {
            result += arg;
        }
    }
    return result;
}

bool ExecuteCommand(const std::vector<std::string>& args,
                    std::string& output,
                    std::string& error) {
    std::string cmd = JoinArgs(args);

    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        error = "Failed to execute command: " + cmd;
        return false;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exit_code = _pclose(pipe);
    if (exit_code != 0) {
        error = "Command failed with exit code: " + std::to_string(exit_code);
        return false;
    }

    return true;
}

} // namespace

bool LinkTarget(const Project& project,
                const Json::Value& target,
                const std::vector<std::string>& object_files,
                LinkResult& result) {
    // Generate output path
    fs::path output_dir = project.build_dir / "bin";
    std::error_code ec;
    fs::create_directories(output_dir, ec);

    fs::path output_file;
    if (project.document.isMember("output")) {
        output_file = AbsolutePath(project.root, project.document["output"].asString());
    } else {
        output_file = output_dir / (target["name"].asString() + ".exe");
    }

    // Build link arguments
    std::vector<std::string> args = BuildLinkArgs(project, target, object_files, output_file);

    // Execute linking
    bool success = ExecuteCommand(args, result.output, result.error);

    result.success = success;
    result.executable = output_file.string();

    return success;
}

bool LinkTargets(const Project& project,
                 const Json::Value& targets,
                 const std::vector<std::vector<std::string>>& object_files,
                 std::vector<LinkResult>& results) {
    results.resize(targets.size());

    for (Json::ArrayIndex i = 0; i < targets.size(); ++i) {
        if (!LinkTarget(project, targets[i], object_files[i], results[i])) {
            return false;
        }
    }

    return true;
}

} // namespace XBuild
