#pragma once
#include "environment.h"
#include <filesystem>
#include <json/json.h>
#include <string>
#include <vector>

namespace XBuild {
namespace fs = std::filesystem;
struct Project {
    Json::Value document;
    fs::path file;
    fs::path root;
    fs::path build_dir;
    unsigned jobs = 1;
    unsigned timeout_ms = 0;
    bool debug = false;
    bool allow_execution = true;
    std::string tool = "clang";
};
struct Command {
    std::string program;
    std::vector<std::string> arguments;
    fs::path directory;
};
struct SourceUnit {
    fs::path input;
    fs::path output;
    std::string tool;
    std::string before;
    std::string after;
    bool header = false;
    unsigned delay_ms = 0;
    bool delay = false;
};
enum class ActionKind { Compile, Link, Delay };
struct Action {
    std::string id;
    ActionKind kind = ActionKind::Compile;
    Command command;
    std::vector<std::size_t> dependencies;
    std::vector<fs::path> inputs;
    fs::path output;
    fs::path dependency_file;
    std::string tool;
    std::string before;
    std::string after;
    unsigned delay_ms = 0;
};
struct BuildPlan {
    Project project;
    Environment environment;
    std::vector<Action> actions;
    std::vector<fs::path> include_roots;
    std::vector<fs::path> runtime_dirs;
};
std::vector<std::string> Strings(const Json::Value& value);
bool LoadProject(const fs::path& file, Project& project, std::string& error);
}
