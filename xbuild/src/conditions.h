#pragma once
#include "environment.h"
#include <string>
namespace XBuild {
bool EvaluateCondition(const std::string& expression, const Environment& environment,
                       bool& result, std::string& error);
}
