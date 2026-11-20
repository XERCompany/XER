#pragma once

#include "config.h"
#include <string>

class Feature {
public:
	explicit Feature(std::string name);

	const std::string& name() const;

private:
	std::string name_;
};
