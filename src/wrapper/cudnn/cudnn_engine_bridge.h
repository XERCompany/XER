#pragma once

#include <string>

namespace XER::Bridge::CuDNN {

bool IsAvailable();
std::string Summary();

}  // namespace XER::Bridge::CuDNN