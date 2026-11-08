#pragma once

#include <string>

#include "error_handler/err_diagnostic_data.h"

namespace XER::ErrorHandler {

std::string BuildDiagnosticReport(const DiagnosticData& data);
std::string BuildDiagnosticCacheKey(const DiagnosticData& data);
std::string BuildDiagnosticFileName(const DiagnosticData& data);

}  // namespace XER::ErrorHandler
