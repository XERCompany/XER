#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace XER::ML::Utils {

class MlConfigParser {
 public:
	bool ParseText(const std::string& text);

	std::optional<std::string> GetString(const std::string& key) const;
	std::optional<int> GetInt(const std::string& key) const;
	std::optional<float> GetFloat(const std::string& key) const;
	std::optional<bool> GetBool(const std::string& key) const;

	const std::unordered_map<std::string, std::string>& Raw() const { return config_; }

 private:
	static std::string Trim(std::string value);

	std::unordered_map<std::string, std::string> config_;
};

}  // namespace XER::ML::Utils

