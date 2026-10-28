#pragma once
#include "model.h"
namespace XBuild {
bool ExpandGlob(const Project& project, const std::string& input, bool recursive,
                std::vector<fs::path>& files, std::string& error);
}
