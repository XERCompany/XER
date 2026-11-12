#include "err_stack_trace.h"

#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#elif defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define XER_HAS_EXECINFO 1
#endif
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#define XER_HAS_CXXABI_DEMANGLE 1
#endif
#endif

namespace XER::ErrorHandler {

namespace {

#if defined(_WIN32)

// dbghelp is process-wide; initialize once, best-effort.
void EnsureSymbolsInitialized() {
	static const bool initialized = [] {
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
		return SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
	}();
	(void)initialized;
}

std::string FormatWindowsFrame(void* address, int index) {
	EnsureSymbolsInitialized();

	char buffer[sizeof(SYMBOL_INFO) + 256]{};
	auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = 255;

	std::ostringstream oss;
	oss << '#' << index << ' ' << address;

	DWORD64 displacement = 0;
	if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(address),
	                &displacement, symbol)) {
		oss << ' ' << symbol->Name << "+0x" << std::hex << displacement << std::dec;
	}

	IMAGEHLP_LINE64 line{};
	line.SizeOfStruct = sizeof(line);
	DWORD line_displacement = 0;
	if (SymGetLineFromAddr64(GetCurrentProcess(),
	                         reinterpret_cast<DWORD64>(address),
	                         &line_displacement, &line)) {
		oss << " (" << line.FileName << ':' << line.LineNumber << ')';
	}
	return oss.str();
}

#else

std::string FormatPosixFrame(const char* symbol, int index) {
	return "#" + std::to_string(index) + " " + StackTrace::Demangle(symbol);
}

#endif

}  // namespace

std::string StackTrace::Capture(int skip_frames, int max_frames) {
#if defined(_WIN32)
	std::vector<void*> frames(static_cast<size_t>(max_frames + 1));
	DWORD hash = 0;
	const USHORT count = CaptureStackBackTrace(
		static_cast<DWORD>(skip_frames + 1),
		static_cast<DWORD>(frames.size()), frames.data(), &hash);

	std::ostringstream oss;
	for (USHORT i = 0; i < count; ++i) {
		oss << FormatWindowsFrame(frames[i], i) << '\n';
	}
	if (count == 0) {
		oss << "(stack capture unavailable)\n";
	}
	return oss.str();
#elif defined(XER_HAS_EXECINFO)
	std::vector<void*> frames(static_cast<size_t>(max_frames));
	int count = ::backtrace(frames.data(), max_frames);

	char** syms = ::backtrace_symbols(frames.data(), count);
	if (!syms) return "(backtrace unavailable)";

	std::ostringstream oss;
	for (int i = skip_frames; i < count; ++i) {
		oss << FormatPosixFrame(syms[i], i - skip_frames) << '\n';
	}
	::free(syms);
	return oss.str();
#else
	return "(stack trace unsupported on this platform)";
#endif
}

std::string StackTrace::Demangle(const char* mangled) {
	if (!mangled) return {};
#if defined(XER_HAS_CXXABI_DEMANGLE)
	int status = 0;
	std::unique_ptr<char, void(*)(void*)> demangled{
		abi::__cxa_demangle(mangled, nullptr, nullptr, &status), ::free};
	return (status == 0 && demangled) ? demangled.get() : mangled;
#elif defined(_WIN32)
	char buffer[1024];
	if (UnDecorateSymbolName(mangled, buffer, sizeof(buffer),
	                       UNDNAME_COMPLETE) > 0) {
		return buffer;
	}
	return mangled;
#else
	return mangled;
#endif
}

std::string StackTrace::Summary(int frames) {
	return Capture(1, frames + 1);
}

}  // namespace XER::ErrorHandler
