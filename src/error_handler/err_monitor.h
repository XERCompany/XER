#pragma once

#include <string>
#include <string_view>

#include "error_handler/err_diagnostic_data.h"

namespace XER::ErrorHandler {

struct MonitorConfiguration {
	bool verbose = false;
	bool timestamps = false;
	bool persist_reports = true;
	std::string log_level;
};

void InitializeMonitor(const MonitorConfiguration& config);
void ReportDiagnostic(const DiagnosticData& diagnostic);
void ReportStartupEvent(std::string_view component, std::string_view message);
void ReportStartupError(std::string_view component, std::string_view message);
void ReportException(std::string_view component, std::string_view message);
std::string LastReportPath();

}  // namespace XER::ErrorHandler
