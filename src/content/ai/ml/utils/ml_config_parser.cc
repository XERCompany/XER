#include "ml_config_parser.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace XER::ML::Utils {

bool MlConfigParser::ParseText(const std::string& text) {
	config_.clear();

	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		const std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == '#') {
			continue;
		}

		const size_t eq = trimmed.find('=');
		if (eq == std::string::npos) {
			continue;
		}

		std::string key = Trim(trimmed.substr(0, eq));
		std::string value = Trim(trimmed.substr(eq + 1));
		if (!key.empty()) {
			config_[key] = value;
		}
	}

	return !config_.empty();
}

std::optional<std::string> MlConfigParser::GetString(const std::string& key) const {
	const auto it = config_.find(key);
	if (it == config_.end()) {
		return std::nullopt;
	}
	return it->second;
}

std::optional<int> MlConfigParser::GetInt(const std::string& key) const {
	const auto value = GetString(key);
	if (!value.has_value()) {
		return std::nullopt;
	}
	try {
		return std::stoi(*value);
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<float> MlConfigParser::GetFloat(const std::string& key) const {
	const auto value = GetString(key);
	if (!value.has_value()) {
		return std::nullopt;
	}
	try {
		return std::stof(*value);
	} catch (...) {
		return std::nullopt;
	}
}

std::optional<bool> MlConfigParser::GetBool(const std::string& key) const {
	const auto value = GetString(key);
	if (!value.has_value()) {
		return std::nullopt;
	}

	std::string lowered = *value;
	std::transform(lowered.begin(), lowered.end(), lowered.begin(),
								 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
		return true;
	}
	if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
		return false;
	}
	return std::nullopt;
}

std::string MlConfigParser::Trim(std::string value) {
	auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
	value.erase(value.begin(), std::find_if(value.begin(), value.end(),
																					[&](unsigned char ch) { return !is_space(ch); }));
	value.erase(std::find_if(value.rbegin(), value.rend(),
													 [&](unsigned char ch) { return !is_space(ch); }).base(),
							value.end());
	return value;
}

}  // namespace XER::ML::Utils

