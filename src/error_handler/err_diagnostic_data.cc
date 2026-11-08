#include "error_handler/err_diagnostic_data.h"

#include <chrono>

#include <absl/strings/string_view.h>
#include <range/v3/algorithm/any_of.hpp>

#include "helper/string.h"

namespace XER::ErrorHandler {

DiagnosticData MakeDiagnosticData(std::string_view level,
				 std::string_view component,
				 std::string_view message) {
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	const std::string ticks = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
	return DiagnosticData{
		Helper::String::CanonicalizeToken(absl::string_view(level.data(), level.size())),
		Helper::String::DefaultString(absl::string_view(component.data(), component.size()), "error_handler"),
		Helper::String::NormalizeUtf8(absl::string_view(message.data(), message.size())),
		ticks,
		std::string(),
		std::string(),
		std::string(),
	};
}

}  // namespace XER::ErrorHandler
