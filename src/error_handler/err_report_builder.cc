#include "error_handler/err_report_builder.h"

#include <string>
#include <vector>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>

#include "cache/cache_utils.h"
#include "helper/string_utils.h"

namespace XER::ErrorHandler {

std::string BuildDiagnosticReport(const DiagnosticData& data) {
	std::vector<std::string> lines = {
		absl::StrCat("level=", data.level),
		absl::StrCat("component=", data.component),
		absl::StrCat("timestamp_ticks=", data.timestamp_ticks),
		absl::StrCat("message=", data.message),
		data.script_path.empty() ? std::string() : absl::StrCat("script_path=", data.script_path),
		data.dump_dir.empty() ? std::string() : absl::StrCat("dump_dir=", data.dump_dir),
		data.crash_dump_path.empty() ? std::string() : absl::StrCat("crash_dump_path=", data.crash_dump_path)
	};
	const auto filtered = lines
		| ranges::views::filter([](const std::string& line) {
			return !Helper::String::IsBlank(line);
		})
		| ranges::to<std::vector<std::string>>();
	return absl::StrCat(absl::StrJoin(filtered, "\n"), "\n");
}

std::string BuildDiagnosticCacheKey(const DiagnosticData& data) {
	return absl::StrCat(data.component, "|", data.script_path, "|", data.timestamp_ticks);
}

std::string BuildDiagnosticFileName(const DiagnosticData& data) {
	return absl::StrCat(
		data.timestamp_ticks,
		"_",
		XER::Cache::SanitizeFileName(data.component),
		".report.txt");
}

}  // namespace XER::ErrorHandler
