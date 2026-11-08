#include "err_exception_wrapper.h"

#include "err_monitor.h"
#include "err_diagnostic_data.h"

namespace XER::ErrorHandler {

bool ExceptionWrapper::TryCatch(std::string_view component,
                                 std::function<void()> work) noexcept {
  try {
    work();
    return true;
  } catch (const std::exception& e) {
    ReportCaughtException(component, e.what());
  } catch (...) {
    ReportCaughtException(component, "unknown exception");
  }
  return false;
}

void ExceptionWrapper::ReportCaughtException(std::string_view component,
                                              std::string_view what) noexcept {
  try {
    ReportException(std::string(component), std::string(what));
  } catch (...) {}
}

}  // namespace XER::ErrorHandler
