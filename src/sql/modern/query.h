#pragma once

#include <string>
#include <vector>

#include "sql/content/value.h"

namespace XER::SQL::Modern {

struct Query {
	std::string text;
	std::vector<XER::SQL::Content::Value> parameters;
};

}  // namespace XER::SQL::Modern