#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace XER::ModelsBuilder::Reader::Utils {

enum class ReaderLogLevel {
	kDebug = 0,
	kInfo = 1,
	kWarning = 2,
	kError = 3,
};

struct ReaderLogEntry {
	ReaderLogLevel level;
	std::string message;
};

class ReaderLogger {
 public:
	static ReaderLogger& GetInstance();

	void SetMinLevel(ReaderLogLevel level);
	void Log(ReaderLogLevel level, std::string_view message);
	void Debug(std::string_view message);
	void Info(std::string_view message);
	void Warning(std::string_view message);
	void Error(std::string_view message);

	std::vector<ReaderLogEntry> GetEntries() const;
	void Clear();

 private:
	ReaderLogger() = default;

	mutable std::mutex mutex_;
	ReaderLogLevel min_level_ = ReaderLogLevel::kInfo;
	std::vector<ReaderLogEntry> entries_;
};

}  // namespace XER::ModelsBuilder::Reader::Utils

