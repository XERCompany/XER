#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace XER::ModelsBuilder::Utility {

enum class ModelBuilderLogLevel : uint8_t {
	Debug = 0,
	Info = 1,
	Warning = 2,
	Error = 3
};

struct ModelBuilderLogEntry {
	ModelBuilderLogLevel level;
	std::string message;
	std::chrono::system_clock::time_point timestamp;
};

class ModelBuilderLogger {
public:
	static ModelBuilderLogger& GetInstance();

	void SetMinimumLevel(ModelBuilderLogLevel level);
	ModelBuilderLogLevel GetMinimumLevel() const;

	void EnableConsoleOutput(bool enabled);
	bool IsConsoleOutputEnabled() const;

	void Log(ModelBuilderLogLevel level, std::string_view message);
	void Debug(std::string_view message);
	void Info(std::string_view message);
	void Warning(std::string_view message);
	void Error(std::string_view message);

	std::vector<ModelBuilderLogEntry> GetEntries() const;
	void Clear();

private:
	ModelBuilderLogger();

	mutable std::mutex mutex_;
	std::vector<ModelBuilderLogEntry> entries_;
	ModelBuilderLogLevel minimum_level_;
	bool console_output_enabled_;
};

} // namespace XER::ModelsBuilder::Utility
