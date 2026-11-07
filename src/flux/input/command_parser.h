#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace XER::flux::input {

struct ParsedCommand {
	std::string raw;
	std::string name;
	std::vector<std::string> args;
	std::unordered_map<std::string, std::string> options;
	bool valid = false;

	bool HasOption(std::string_view key) const;
	std::string OptionValue(std::string_view key) const;
};

std::vector<std::string> TokenizeCommandLine(std::string_view input);
ParsedCommand ParseCommandLine(std::string_view input);

}  // namespace XER::flux::input
