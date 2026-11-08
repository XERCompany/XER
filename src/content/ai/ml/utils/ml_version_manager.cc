#include "ml_version_manager.h"

#include "../../models_builder/utility/ai_runtime_features.h"

#include <sstream>

namespace XER::ML::Utils {

std::string MlVersionManager::GetVersion() {
	return "0.2.0";
}

std::string MlVersionManager::GetBuildTag() {
	return "xer-ml-runtime";
}

std::string MlVersionManager::GetBackendSummary() {
	const auto libs = ::XER::ModelsBuilder::Utility::DetectExternalLibraries();
	std::ostringstream ss;
	ss << "cuda=" << (libs.has_cuda ? "on" : "off")
		 << ", cudnn=" << (libs.has_cudnn ? "on" : "off")
		 << ", cutlass=" << (libs.has_cutlass ? "on" : "off")
		 << ", eigen=" << (libs.has_eigen ? "on" : "off")
		 << ", opencv=" << (libs.has_opencv ? "on" : "off")
		 << ", xnnpack=" << (libs.has_xnnpack ? "on" : "off");
	return ss.str();
}

}  // namespace XER::ML::Utils

