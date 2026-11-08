#include "mb_logger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace XER::ModelsBuilder::Utility {

namespace {

const char* ToString(ModelBuilderLogLevel level) {
	switch (level) {
		case ModelBuilderLogLevel::Debug:
			return "DEBUG";
		case ModelBuilderLogLevel::Info:
			return "INFO";
		case ModelBuilderLogLevel::Warning:
			return "WARN";
		case ModelBuilderLogLevel::Error:
			return "ERROR";
	}

	return "UNKNOWN";
}

std::string FormatLogMessage(const ModelBuilderLogEntry& entry) {
	const std::time_t raw_time = std::chrono::system_clock::to_time_t(entry.timestamp);
	std::tm time_info{};
#if defined(_WIN32)
	localtime_s(&time_info, &raw_time);
#else
	localtime_r(&raw_time, &time_info);
#endif

	std::ostringstream stream;
	stream << '[' << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S") << "] "
		   << ToString(entry.level) << " " << entry.message;
	return stream.str();
}

} // namespace

ModelBuilderLogger::ModelBuilderLogger()
	: minimum_level_(ModelBuilderLogLevel::Info), console_output_enabled_(true) {
}

ModelBuilderLogger& ModelBuilderLogger::GetInstance() {
	static ModelBuilderLogger instance;
	return instance;
}

void ModelBuilderLogger::SetMinimumLevel(ModelBuilderLogLevel level) {
	std::scoped_lock lock(mutex_);
	minimum_level_ = level;
}

ModelBuilderLogLevel ModelBuilderLogger::GetMinimumLevel() const {
	std::scoped_lock lock(mutex_);
	return minimum_level_;
}

void ModelBuilderLogger::EnableConsoleOutput(bool enabled) {
	std::scoped_lock lock(mutex_);
	console_output_enabled_ = enabled;
}

bool ModelBuilderLogger::IsConsoleOutputEnabled() const {
	std::scoped_lock lock(mutex_);
	return console_output_enabled_;
}

void ModelBuilderLogger::Log(ModelBuilderLogLevel level, std::string_view message) {
	std::scoped_lock lock(mutex_);
	if (static_cast<int>(level) < static_cast<int>(minimum_level_)) {
		return;
	}

	ModelBuilderLogEntry entry{level, std::string(message), std::chrono::system_clock::now()};
	entries_.push_back(entry);
	if (console_output_enabled_) {
		std::clog << FormatLogMessage(entry) << std::endl;
	}
}

void ModelBuilderLogger::Debug(std::string_view message) {
	Log(ModelBuilderLogLevel::Debug, message);
}

void ModelBuilderLogger::Info(std::string_view message) {
	Log(ModelBuilderLogLevel::Info, message);
}

void ModelBuilderLogger::Warning(std::string_view message) {
	Log(ModelBuilderLogLevel::Warning, message);
}

void ModelBuilderLogger::Error(std::string_view message) {
	Log(ModelBuilderLogLevel::Error, message);
}

std::vector<ModelBuilderLogEntry> ModelBuilderLogger::GetEntries() const {
	std::scoped_lock lock(mutex_);
	return entries_;
}

void ModelBuilderLogger::Clear() {
	std::scoped_lock lock(mutex_);
	entries_.clear();
}

} // namespace XER::ModelsBuilder::Utility
