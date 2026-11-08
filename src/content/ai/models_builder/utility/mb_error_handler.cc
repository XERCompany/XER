#include "mb_error_handler.h"

namespace XER::ModelsBuilder::Utility {

ModelBuilderErrorHandler& ModelBuilderErrorHandler::GetInstance() {
	static ModelBuilderErrorHandler instance;
	return instance;
}

void ModelBuilderErrorHandler::ReportError(ModelBuilderErrorCode code,
										   const std::string& message,
										   const std::string& source) {
	PushIssue(code, ModelBuilderIssueSeverity::Error, message, source);
}

void ModelBuilderErrorHandler::ReportWarning(ModelBuilderErrorCode code,
											 const std::string& message,
											 const std::string& source) {
	PushIssue(code, ModelBuilderIssueSeverity::Warning, message, source);
}

std::optional<ModelBuilderIssue> ModelBuilderErrorHandler::GetLastIssue() const {
	std::scoped_lock lock(mutex_);
	if (issues_.empty()) {
		return std::nullopt;
	}
	return issues_.back();
}

std::vector<ModelBuilderIssue> ModelBuilderErrorHandler::GetIssues() const {
	std::scoped_lock lock(mutex_);
	return issues_;
}

bool ModelBuilderErrorHandler::HasErrors() const {
	std::scoped_lock lock(mutex_);
	for (const ModelBuilderIssue& issue : issues_) {
		if (issue.severity == ModelBuilderIssueSeverity::Error) {
			return true;
		}
	}
	return false;
}

void ModelBuilderErrorHandler::Clear() {
	std::scoped_lock lock(mutex_);
	issues_.clear();
}

void ModelBuilderErrorHandler::PushIssue(ModelBuilderErrorCode code,
										 ModelBuilderIssueSeverity severity,
										 const std::string& message,
										 const std::string& source) {
	std::scoped_lock lock(mutex_);
	issues_.push_back(ModelBuilderIssue{code, severity, message, source, std::chrono::system_clock::now()});
}

} // namespace XER::ModelsBuilder::Utility
