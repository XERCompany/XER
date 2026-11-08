#include "err_recovery_policy.h"

namespace XER::ErrorHandler {

RecoveryPolicy& RecoveryPolicy::Instance() {
  static RecoveryPolicy inst;
  return inst;
}

RecoveryAction RecoveryPolicy::Evaluate(const DiagnosticData& data) const {
  auto it = component_overrides_.find(data.component);
  if (it != component_overrides_.end()) return it->second;

  // fatal level always aborts regardless of global policy
  if (data.level == "fatal") return RecoveryAction::Abort;

  return global_;
}

void RecoveryPolicy::SetGlobalPolicy(RecoveryAction action) {
  global_ = action;
}

void RecoveryPolicy::SetComponentPolicy(std::string_view component,
                                         RecoveryAction action) {
  component_overrides_[std::string(component)] = action;
}

void RecoveryPolicy::Reset() {
  global_ = RecoveryAction::Abort;
  component_overrides_.clear();
}

}  // namespace XER::ErrorHandler
