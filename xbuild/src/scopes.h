#pragma once
#include "environment.h"
#include <json/json.h>
#include <string>
#include <vector>
namespace XBuild {
bool EvaluateScopes(const Json::Value& scopes, Environment& environment,
                    std::vector<std::string>& messages, std::string& error);
}
