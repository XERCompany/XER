#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace XER::Helper {

// Reads an environment variable into an owned string.
// Returns an empty string when the variable is unset or empty —
// callers that must distinguish "unset" from "set to empty" should
// treat both as absent (the common case for config overrides).
//
// Header-only so low-level modules (async_io, cache, flux) can use it
// without depending on XER_Helper, which itself links XER_AsyncIO.
// Implemented with the bounds-checked CRT on Windows (_dupenv_s) so
// call sites stay free of C4996 deprecation warnings.
inline std::string GetEnv(std::string_view name) {
    if (name.empty()) {
        return {};
    }
#ifdef _WIN32
    char* value = nullptr;
    std::size_t len = 0;
    const errno_t rc = _dupenv_s(&value, &len, std::string(name).c_str());
    if (rc != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(std::string(name).c_str());
    return value != nullptr ? std::string(value) : std::string();
#endif
}

}  // namespace XER::Helper
