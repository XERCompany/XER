#pragma once

#include <string>
#include <unordered_map>

#include "err_diagnostic_data.h"

namespace XER::ErrorHandler {

/// @brief Actions the runtime may take when handling an error
enum class RecoveryAction {
  Ignore,   ///< Swallow and continue
  Retry,    ///< Attempt the operation again
  Abort,    ///< Terminate the current operation cleanly
  Restart,  ///< Restart the affected subsystem
};

/// @brief Determines recovery action from a DiagnosticData record
///
/// Global policy applies first; per-component overrides take precedence.
class RecoveryPolicy {
 public:
  static RecoveryPolicy& Instance();

  /// Evaluate the appropriate action for a diagnostic
  RecoveryAction Evaluate(const DiagnosticData& data) const;

  /// Set global fallback action
  void SetGlobalPolicy(RecoveryAction action);

  /// Set per-component override
  void SetComponentPolicy(std::string_view component, RecoveryAction action);

  void Reset();

 private:
  RecoveryPolicy() = default;

  RecoveryAction global_{RecoveryAction::Abort};
  std::unordered_map<std::string, RecoveryAction> component_overrides_;
};

}  // namespace XER::ErrorHandler
