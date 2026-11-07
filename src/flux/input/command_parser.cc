#include "flux/input/command_parser.h"
#include "helper/string_utils.h"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace XER::flux::input {
namespace {

bool StartsWith(std::string_view text, std::string_view prefix) {
	return XER::Engine::Helper::String::StartsWith(text, prefix);
}

std::string TrimDashPrefix(std::string_view text) {
	std::size_t index = 0;
	while (index < text.size() && text[index] == '-') {
		++index;
	}
	return std::string(text.substr(index));
}

}  // namespace

bool ParsedCommand::HasOption(std::string_view key) const {
	return options.find(std::string(key)) != options.end();
}

std::string ParsedCommand::OptionValue(std::string_view key) const {
	const auto iterator = options.find(std::string(key));
	return iterator == options.end() ? std::string() : iterator->second;
}

std::vector<std::string> TokenizeCommandLine(std::string_view input) {
	std::vector<std::string> tokens;
	std::string current;
	char quote = '\0';
	bool escape = false;

	for (char ch : input) {
		if (escape) {
			current.push_back(ch);
			escape = false;
			continue;
		}
		if (ch == '\\') {
			escape = true;
			continue;
		}
		if (quote != '\0') {
			if (ch == quote) {
				quote = '\0';
			} else {
				current.push_back(ch);
			}
			continue;
		}
		if (ch == '\'' || ch == '"') {
			quote = ch;
			continue;
		}
		if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
			if (!current.empty()) {
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}
		current.push_back(ch);
	}

	if (escape) {
		current.push_back('\\');
	}
	if (!current.empty()) {
		tokens.push_back(current);
	}
	return tokens;
}

ParsedCommand ParseCommandLine(std::string_view input) {
	ParsedCommand parsed;
	parsed.raw.assign(input.data(), input.size());
	const std::vector<std::string> tokens = TokenizeCommandLine(input);
	if (tokens.empty()) {
		return parsed;
	}

	parsed.name = tokens.front();
	parsed.valid = true;

	for (std::size_t index = 1; index < tokens.size(); ++index) {
		const std::string& token = tokens[index];
		if (StartsWith(token, "--") && token.size() > 2) {
			const std::size_t equal = token.find('=');
			if (equal != std::string::npos) {
				parsed.options[TrimDashPrefix(token.substr(0, equal))] = token.substr(equal + 1);
				continue;
			}
			const std::string key = TrimDashPrefix(token);
			if (index + 1 < tokens.size() && !StartsWith(tokens[index + 1], "-")) {
				parsed.options[key] = tokens[index + 1];
				++index;
			} else {
				parsed.options[key] = "true";
			}
			continue;
		}
		if (StartsWith(token, "-") && token.size() > 1) {
			for (std::size_t flag_index = 1; flag_index < token.size(); ++flag_index) {
				parsed.options[std::string(1, token[flag_index])] = "true";
			}
			continue;
		}
		parsed.args.push_back(token);
	}

	return parsed;
}

}  // namespace XER::flux::input
