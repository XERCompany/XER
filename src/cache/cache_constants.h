#pragma once

#include <absl/strings/string_view.h>

namespace XER::Engine::Cache::constants {

inline constexpr absl::string_view kFlowScriptScope = "flow_script";
inline constexpr absl::string_view kRuntimeLiveScope = "runtime_live";
inline constexpr absl::string_view kErrorReportScope = "error_report";

}  // namespace XER::Engine::Cache::constants
