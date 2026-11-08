#pragma once

#include <string>

namespace XER::ML::Utils {

class MlVersionManager {
 public:
	static std::string GetVersion();
	static std::string GetBuildTag();
	static std::string GetBackendSummary();
};

}  // namespace XER::ML::Utils

