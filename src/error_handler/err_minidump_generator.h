#pragma once

#include <string>
#include <string_view>

#include <absl/strings/string_view.h>

namespace XER::ErrorHandler {

// ErrMinidumpGenerator produces a structured plain-text process snapshot on
// demand (i.e. from ordinary code, not from a signal handler).
// The dump captures: metadata, /proc memory stats, and a native stack trace.
class ErrMinidumpGenerator {
public:
	ErrMinidumpGenerator() = default;
	~ErrMinidumpGenerator() = default;

	// Optional context that is embedded in the dump header.
	void SetComponent(absl::string_view component);
	void SetScriptPath(absl::string_view script_path);
	void SetMessage(absl::string_view message);

	// Generate a dump and write it to |path|.
	// Returns true on success.
	bool Generate(absl::string_view path) const;

private:
	std::string component_;
	std::string script_path_;
	std::string message_;
};

}  // namespace XER::ErrorHandler
