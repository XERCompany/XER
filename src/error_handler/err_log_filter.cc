#include "err_log_filter.h"

namespace XER::ErrorHandler {

LogFilter& LogFilter::Instance() {
  static LogFilter inst;
  return inst;
}

void LogFilter::SetMinLevel(std::string_view level) {
  min_level_rank_ = LevelToRank(level);
}

void LogFilter::AllowComponent(std::string_view component) {
  allow_components_.emplace(component);
}

void LogFilter::DenyComponent(std::string_view component) {
  deny_components_.emplace(component);
}

bool LogFilter::ShouldPass(const DiagnosticData& data) const {
  if (LevelToRank(data.level) < min_level_rank_) return false;
  if (deny_components_.count(data.component)) return false;
  if (!allow_components_.empty() && !allow_components_.count(data.component))
    return false;
  return true;
}

void LogFilter::Reset() {
  min_level_rank_ = 0;
  allow_components_.clear();
  deny_components_.clear();
}

int LogFilter::LevelToRank(std::string_view level) {
  if (level == "debug") return 0;
  if (level == "info")  return 1;
  if (level == "warn")  return 2;
  if (level == "error") return 3;
  if (level == "fatal") return 4;
  return 0;
}

}  // namespace XER::ErrorHandler
