#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace XER::ML::Utils {

enum class MlLogLevel : unsigned char {
	Debug = 0,
	Info = 1,
	Warning = 2,
	Error = 3,
};

struct MlLogEntry {
	MlLogLevel level;
	std::string message;
};

class MlLogger {
 public:
	static MlLogger& GetInstance();

	void SetMinLevel(MlLogLevel level);
	MlLogLevel GetMinLevel() const;

	void Log(MlLogLevel level, std::string_view message);
	void Debug(std::string_view message);
	void Info(std::string_view message);
	void Warning(std::string_view message);
	void Error(std::string_view message);

	std::vector<MlLogEntry> GetEntries() const;
	void Clear();

 private:
	MlLogger() = default;

	mutable std::mutex mutex_;
	MlLogLevel min_level_ = MlLogLevel::Info;
	std::vector<MlLogEntry> entries_;
};

}  // namespace XER::ML::Utils

