#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace XBuild {
std::string PathText(const std::filesystem::path& path);
std::filesystem::path AbsolutePath(const std::filesystem::path& root, const std::filesystem::path& path);
bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& root);
bool IsSourceFile(const std::filesystem::path& path);
bool IsHeaderFile(const std::filesystem::path& path);
bool EnsureDirectory(const std::filesystem::path& path, std::string& error);
std::vector<std::string> SplitPaths(const std::string& text);
std::string SafeName(const std::string& name);
}
