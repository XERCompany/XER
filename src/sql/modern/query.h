#pragma once

#include <string>
#include <vector>

#include "sql/content/value.h"

namespace XER::Engine::SQL::Modern {

struct Query {
	std::string text;
	std::vector<XER::Content::Value> parameters;
};

}  // namespace XER::Engine::SQL::Modern