#pragma once

#include <string>
#include <string_view>

namespace XER::ErrorHandler {

struct DiagnosticData {
	std::string level;
	std::string component;
	std::string message;
	std::string timestamp_ticks;
	std::string script_path;
	std::string dump_dir;
	std::string crash_dump_path;
};

DiagnosticData MakeDiagnosticData(std::string_view level,
				 std::string_view component,
				 std::string_view message);

}  // namespace XER::ErrorHandler
