#include "ml_logger.h"

namespace XER::ML::Utils {

MlLogger& MlLogger::GetInstance() {
	static MlLogger instance;
	return instance;
}

void MlLogger::SetMinLevel(MlLogLevel level) {
	std::scoped_lock lock(mutex_);
	min_level_ = level;
}

MlLogLevel MlLogger::GetMinLevel() const {
	std::scoped_lock lock(mutex_);
	return min_level_;
}

void MlLogger::Log(MlLogLevel level, std::string_view message) {
	std::scoped_lock lock(mutex_);
	if (static_cast<int>(level) < static_cast<int>(min_level_)) {
		return;
	}
	entries_.push_back(MlLogEntry{level, std::string(message)});
}

void MlLogger::Debug(std::string_view message) {
	Log(MlLogLevel::Debug, message);
}

void MlLogger::Info(std::string_view message) {
	Log(MlLogLevel::Info, message);
}

void MlLogger::Warning(std::string_view message) {
	Log(MlLogLevel::Warning, message);
}

void MlLogger::Error(std::string_view message) {
	Log(MlLogLevel::Error, message);
}

std::vector<MlLogEntry> MlLogger::GetEntries() const {
	std::scoped_lock lock(mutex_);
	return entries_;
}

void MlLogger::Clear() {
	std::scoped_lock lock(mutex_);
	entries_.clear();
}

}  // namespace XER::ML::Utils

