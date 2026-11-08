#include "preprocess_base.h"

#include <string_view>

#include "flux/core/logger.h"

namespace XER::ML::Preprocessing {

void PreprocessBase::LogInfo(absl::string_view msg) const {
	if (logger_) {
		logger_->Info("ML.Preprocessing", std::string_view(msg.data(), msg.size()));
	}
}

void PreprocessBase::LogError(absl::string_view msg) const {
	if (logger_) {
		logger_->Error("ML.Preprocessing", std::string_view(msg.data(), msg.size()));
	}
}

}  // namespace XER::ML::Preprocessing
