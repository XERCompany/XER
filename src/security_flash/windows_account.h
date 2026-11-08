#pragma once

#include <string>
#include <string_view>

namespace XER::SecurityFlash::Detail {

// Local Windows account management via NetUser* APIs (netapi32).
// All functions return false and fill error_out on failure.
// On non-Windows platforms every function reports "not supported".

bool CreateLocalUser(const std::string& username,
		     const std::string& password,
		     const std::string& display_name,
		     const std::string& comment,
		     bool password_never_expires,
		     bool password_cannot_change,
		     std::string* error_out);

bool DeleteLocalUser(const std::string& username, std::string* error_out);

// Sets *exists_out; returns false only on lookup errors.
bool LocalUserExists(const std::string& username,
		     bool* exists_out,
		     std::string* error_out);

// True when the current process token is elevated (UAC admin).
bool ProcessIsElevated();

// Converts UTF-8 to UTF-16 for Win32 APIs. Returns empty on failure.
std::wstring Utf8ToWideLocal(std::string_view utf8);

}  // namespace XER::SecurityFlash::Detail
