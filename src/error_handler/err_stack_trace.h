#pragma once

#include <string>

namespace XER::ErrorHandler {

/// @brief Captures and demangles a C++ stack trace at the call site
///
/// Uses POSIX backtrace()/backtrace_symbols() when available.
class StackTrace {
 public:
  /// Capture current call stack
  /// @param skip_frames  Number of top frames to omit (default: 1 = omit self)
  /// @param max_frames   Maximum frames to capture
  /// @return Human-readable multi-line stack string
  static std::string Capture(int skip_frames = 1, int max_frames = 64);

  /// C++ symbol demangling via abi::__cxa_demangle
  static std::string Demangle(const char* mangled);

  /// Compact one-line summary (first N frames)
  static std::string Summary(int frames = 5);
};

}  // namespace XER::ErrorHandler
