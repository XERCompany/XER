#pragma once

#include <string>
#include <string_view>
#include <vector>

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

// Resets the account password through NetUserSetInfo level 1003.
// Requires elevation for accounts other than the caller's own.
bool ChangeLocalUserPassword(const std::string& username,
			     const std::string& new_password,
			     std::string* error_out);

// Reads display name, description and the disabled flag (level 2).
bool GetLocalUserDetails(const std::string& username,
			 std::string* full_name_out,
			 std::string* comment_out,
			 bool* disabled_out,
			 std::string* error_out);

// Sets display name (level 1011), description (level 1007) and the
// UF_ACCOUNTDISABLE flag (level 1008). Requires elevation.
bool UpdateLocalUserDetails(const std::string& username,
			    const std::string& full_name,
			    const std::string& comment,
			    bool disabled,
			    std::string* error_out);

// Renames the account login (level 0). The profile folder keeps its name.
bool RenameLocalUser(const std::string& username,
		     const std::string& new_username,
		     std::string* error_out);

// Lists local account names (NetUserEnum level 0, normal accounts).
bool EnumerateLocalUsers(std::vector<std::string>* names_out,
			 std::string* error_out);

// Validates credentials with LogonUserW(LOGON32_LOGON_NETWORK) —
// succeeds only when username+password pair is correct. The returned
// token is closed immediately; no session is created.
bool VerifyLocalUserPassword(const std::string& username,
			     const std::string& password,
			     std::string* error_out);

// Sets *exists_out; returns false only on lookup errors.
bool LocalUserExists(const std::string& username,
		     bool* exists_out,
		     std::string* error_out);

// True when the current process token is elevated (UAC admin).
bool ProcessIsElevated();

// Converts UTF-8 to UTF-16 for Win32 APIs. Returns empty on failure.
std::wstring Utf8ToWideLocal(std::string_view utf8);

}  // namespace XER::SecurityFlash::Detail
