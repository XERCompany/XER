#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Utility {

enum class ModelBuilderErrorCode : uint16_t {
	None = 0,
	InvalidArgument = 1,
	InvalidShape = 2,
	InitializationFailed = 3,
	CompilationFailed = 4,
	SerializationFailed = 5,
	IoFailure = 6,
	ModelHasNoLayers = 7
};

enum class ModelBuilderIssueSeverity : uint8_t {
	Warning = 0,
	Error = 1
};

struct ModelBuilderIssue {
	ModelBuilderErrorCode code;
	ModelBuilderIssueSeverity severity;
	std::string message;
	std::string source;
	std::chrono::system_clock::time_point timestamp;
};

class ModelBuilderErrorHandler {
public:
	static ModelBuilderErrorHandler& GetInstance();

	void ReportError(ModelBuilderErrorCode code, const std::string& message, const std::string& source);
	void ReportWarning(ModelBuilderErrorCode code, const std::string& message, const std::string& source);

	std::optional<ModelBuilderIssue> GetLastIssue() const;
	std::vector<ModelBuilderIssue> GetIssues() const;
	bool HasErrors() const;
	void Clear();

private:
	void PushIssue(ModelBuilderErrorCode code,
				   ModelBuilderIssueSeverity severity,
				   const std::string& message,
				   const std::string& source);

	mutable std::mutex mutex_;
	std::vector<ModelBuilderIssue> issues_;
};

} // namespace XER::ModelsBuilder::Utility
