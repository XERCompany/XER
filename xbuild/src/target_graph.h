#pragma once
#include <json/json.h>
#include <string>
#include <vector>
namespace XBuild {
bool OrderTargets(const Json::Value& targets, std::vector<std::size_t>& order, std::string& error);
}
