#pragma once

#include <string>

namespace XER::engine::bridge::cudnn {

bool IsAvailable();
std::string Summary();

}  // namespace XER::engine::bridge::cudnn