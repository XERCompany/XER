#pragma once

#include <exception>
#include <functional>
#include <string>
#include <string_view>

#include "err_diagnostic_data.h"

namespace XER::ErrorHandler {

/// @brief Wraps a callable in a try/catch and routes exceptions to the
///        ErrorHandler monitor — keeps JS/C++ boundary clean.
///
/// Usage:
/// ```cpp
/// auto result = ExceptionWrapper::TryCatch("MyModule", [&]() {
///     return heavy_operation();
/// });
/// ```
class ExceptionWrapper {
 public:
  /// Execute work(); report any exception via ReportException().
  /// @return true if work completed without exception
  static bool TryCatch(std::string_view component,
                        std::function<void()> work) noexcept;

  /// Execute work(); return default_value on exception.
  template <typename T>
  static T TryCatchReturn(std::string_view component,
                           std::function<T()> work,
                           T default_value = T{}) noexcept {
    try {
      return work();
    } catch (const std::exception& e) {
      ReportCaughtException(component, e.what());
    } catch (...) {
      ReportCaughtException(component, "unknown exception");
    }
    return default_value;
  }

 private:
  static void ReportCaughtException(std::string_view component,
                                     std::string_view what) noexcept;
};

}  // namespace XER::ErrorHandler
