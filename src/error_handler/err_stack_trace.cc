#include "err_stack_trace.h"

#include <cxxabi.h>
#include <execinfo.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

namespace XER::ErrorHandler {

std::string StackTrace::Capture(int skip_frames, int max_frames) {
  std::vector<void*> frames(static_cast<size_t>(max_frames));
  int count = ::backtrace(frames.data(), max_frames);

  char** syms = ::backtrace_symbols(frames.data(), count);
  if (!syms) return "(backtrace unavailable)";

  std::ostringstream oss;
  for (int i = skip_frames; i < count; ++i) {
    oss << '#' << (i - skip_frames) << ' ' << Demangle(syms[i]) << '\n';
  }
  ::free(syms);
  return oss.str();
}

std::string StackTrace::Demangle(const char* mangled) {
  if (!mangled) return {};
  int status = 0;
  std::unique_ptr<char, void(*)(void*)> demangled{
      abi::__cxa_demangle(mangled, nullptr, nullptr, &status), ::free};
  return (status == 0 && demangled) ? demangled.get() : mangled;
}

std::string StackTrace::Summary(int frames) {
  return Capture(1, frames + 1);
}

}  // namespace XER::ErrorHandler
