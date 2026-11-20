#pragma once

#include "config.h"
#include <string>
#include <unordered_map>
#include <vector>

class Provider {
public:
	void set(std::string key, std::string value);

	const std::string* get(const std::string& key) const;

	std::vector<std::string> keys() const;

private:
	std::unordered_map<std::string, std::string> values_;
};
