#pragma once

#include <string>
#include <vector>

#include "sql/content/value.h"

namespace Engine::SQL::Modern {

struct Query {
	std::string text;
	std::vector<Content::Value> parameters;
};

}  // namespace Engine::SQL::Modern