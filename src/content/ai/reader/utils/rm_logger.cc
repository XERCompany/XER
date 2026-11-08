#include "rm_logger.h"

namespace XER::ModelsBuilder::Reader::Utils {

ReaderLogger& ReaderLogger::GetInstance() {
	static ReaderLogger instance;
	return instance;
}

void ReaderLogger::SetMinLevel(ReaderLogLevel level) {
	std::scoped_lock lock(mutex_);
	min_level_ = level;
}

void ReaderLogger::Log(ReaderLogLevel level, std::string_view message) {
	std::scoped_lock lock(mutex_);
	if (static_cast<int>(level) < static_cast<int>(min_level_)) {
		return;
	}

	entries_.push_back(ReaderLogEntry{level, std::string(message)});
}

void ReaderLogger::Debug(std::string_view message) {
	Log(ReaderLogLevel::kDebug, message);
}

void ReaderLogger::Info(std::string_view message) {
	Log(ReaderLogLevel::kInfo, message);
}

void ReaderLogger::Warning(std::string_view message) {
	Log(ReaderLogLevel::kWarning, message);
}

void ReaderLogger::Error(std::string_view message) {
	Log(ReaderLogLevel::kError, message);
}

std::vector<ReaderLogEntry> ReaderLogger::GetEntries() const {
	std::scoped_lock lock(mutex_);
	return entries_;
}

void ReaderLogger::Clear() {
	std::scoped_lock lock(mutex_);
	entries_.clear();
}

}  // namespace XER::ModelsBuilder::Reader::Utils

