#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "err_diagnostic_data.h"

namespace XER::ErrorHandler {

/// @brief Controls which diagnostic messages are forwarded to monitors
///
/// Filters by severity level and optional component inclusion/exclusion lists.
class LogFilter {
 public:
  static LogFilter& Instance();

  /// Set minimum level to forward ("debug" < "info" < "warn" < "error" < "fatal")
  void SetMinLevel(std::string_view level);

  /// Add a component to the allow-list.  Empty allow-list = allow all.
  void AllowComponent(std::string_view component);

  /// Add a component to the deny-list (always suppressed, overrides allow).
  void DenyComponent(std::string_view component);

  /// @return true if the diagnostic should be forwarded
  bool ShouldPass(const DiagnosticData& data) const;

  void Reset();

 private:
  LogFilter() = default;

  int min_level_rank_{0};  ///< Numeric rank of min level
  std::unordered_set<std::string> allow_components_;
  std::unordered_set<std::string> deny_components_;

  static int LevelToRank(std::string_view level);
};

}  // namespace XER::ErrorHandler
