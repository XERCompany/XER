#ifndef XBUILD_PROJECT_CONFIG_H
#define XBUILD_PROJECT_CONFIG_H

#include <filesystem>
#include <string>

#include <json/json.h>

namespace XBuild {

// Load JSON (with comments) and validate the supported project structure.
// Expressions and paths are preserved verbatim; no build actions are executed.
// On failure, output is unchanged and error contains the file and JSON path
// (or the parser's line/column diagnostics).
bool LoadProjectConfig(const std::filesystem::path& path,
                       Json::Value& output,
                       std::string& error);

} // namespace XBuild

#endif // XBUILD_PROJECT_CONFIG_H
