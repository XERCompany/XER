#include "security_flash/windows_account.h"

#include <algorithm>

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winreg.h>

#include <lmaccess.h>
#include <lmerr.h>
#include <lmapibuf.h>

#endif

namespace XER::SecurityFlash::Detail {
namespace {

std::string NotSupported(const char* what) {
	return absl::StrCat(what, " is not supported on this platform");
}

#ifdef _WIN32

constexpr DWORD kPasswordPolicyFlags = UF_DONT_EXPIRE_PASSWD | UF_PASSWD_CANT_CHANGE;

std::string NetApiErrorMessage(NET_API_STATUS status, const char* context) {
	switch (status) {
	case NO_ERROR:
		return std::string();
	case ERROR_ACCESS_DENIED:
		return absl::StrCat(context,
				    " failed: access denied (run elevated as administrator)");
	case NERR_UserNotFound:
		return absl::StrCat(context, " failed: user not found");
	case NERR_UserExists:
		return absl::StrCat(context, " failed: user already exists");
	case NERR_BadUsername:
		return absl::StrCat(context, " failed: invalid username");
	default:
		return absl::StrFormat("%s failed (netapi error %lu)", context,
				       static_cast<unsigned long>(status));
	}
}

void SetError(std::string* error_out, const std::string& message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

// NetUserAdd level 1 honors most usri1_flags, but UF_DONT_EXPIRE_PASSWD /
// UF_PASSWD_CANT_CHANGE are not guaranteed on every Windows build. Read the
// account back through level 1008 and repair the flag bits when needed.
// Best effort: failures are ignored, the account itself is already created.
void EnforcePasswordFlagsBestEffort(const std::wstring& username_w,
				    DWORD desired_flags) {
	LPBYTE info_buf = nullptr;
	if (NetUserGetInfo(nullptr, username_w.c_str(), 1008, &info_buf) != NERR_Success
	    || info_buf == nullptr) {
		return;
	}

	auto* info = reinterpret_cast<USER_INFO_1008*>(info_buf);
	const DWORD current_flags = info->usri1008_flags;
	if (((current_flags ^ desired_flags) & kPasswordPolicyFlags) == 0) {
		NetApiBufferFree(info_buf);
		return;
	}

	info->usri1008_flags =
		(current_flags & ~kPasswordPolicyFlags) | (desired_flags & kPasswordPolicyFlags);
	NetUserSetInfo(nullptr, username_w.c_str(), 1008, info_buf, nullptr);
	NetApiBufferFree(info_buf);
}

// Sets the display name through a level-4 read/modify/write round trip so
// sibling fields are preserved. Best effort, cosmetic only.
void SetDisplayNameBestEffort(const std::wstring& username_w,
			      const std::wstring& display_w) {
	LPBYTE info_buf = nullptr;
	if (NetUserGetInfo(nullptr, username_w.c_str(), 4, &info_buf) != NERR_Success
	    || info_buf == nullptr) {
		return;
	}

	auto* info = reinterpret_cast<USER_INFO_4*>(info_buf);
	info->usri4_full_name = const_cast<LPWSTR>(display_w.c_str());
	NetUserSetInfo(nullptr, username_w.c_str(), 4, info_buf, nullptr);
	NetApiBufferFree(info_buf);
}

// The Windows sign-in screen lists local accounts that are members of the
// built-in Users group and not hidden via Winlogon\SpecialAccounts\UserList.
// NetUserAdd adds the account to no group, so join Users explicitly (by SID,
// the group name is localized) and drop any hide entry. Best effort.
void EnsureVisibleOnSignInScreenBestEffort(const std::wstring& username_w) {
	BYTE sid_buf[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof(sid_buf);
	if (CreateWellKnownSid(WinBuiltinUsersSid, nullptr, sid_buf, &sid_size)) {
		wchar_t group[256];
		DWORD group_len = 256;
		wchar_t domain[256];
		DWORD domain_len = 256;
		SID_NAME_USE use = SidTypeUnknown;
		if (LookupAccountSidW(nullptr, sid_buf, group, &group_len, domain,
				      &domain_len, &use)) {
			LOCALGROUP_MEMBERS_INFO_3 member = {};
			member.lgrmi3_domainandname = const_cast<LPWSTR>(username_w.c_str());
			// ERROR_MEMBER_IN_ALIAS (already a member) is fine.
			NetLocalGroupAddMembers(nullptr, group, 3,
						reinterpret_cast<LPBYTE>(&member), 1);
		}
	}
	RegDeleteKeyValueW(HKEY_LOCAL_MACHINE,
			   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon"
			   L"\\SpecialAccounts\\UserList",
			   username_w.c_str());
}

std::string WideToUtf8ForDetails(const wchar_t* text) {
	if (text == nullptr || *text == L'\0') {
		return {};
	}
	const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
					  nullptr, nullptr);
	if (n <= 1) {
		return {};
	}
	std::string out(static_cast<std::size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
	return out;
}

#endif  // _WIN32

}  // namespace

bool CreateLocalUser(const std::string& username,
		     const std::string& password,
		     const std::string& display_name,
		     const std::string& comment,
		     bool password_never_expires,
		     bool password_cannot_change,
		     std::string* error_out) {
#ifdef _WIN32
	const std::wstring name_w = Utf8ToWideLocal(username);
	const std::wstring password_w = Utf8ToWideLocal(password);
	if (name_w.empty() || password_w.empty()) {
		SetError(error_out, "username/password could not be converted to UTF-16");
		return false;
	}

	const std::wstring comment_w = Utf8ToWideLocal(comment);

	USER_INFO_1 info = {};
	info.usri1_name = const_cast<LPWSTR>(name_w.c_str());
	info.usri1_password = const_cast<LPWSTR>(password_w.c_str());
	info.usri1_password_age = 0;
	info.usri1_priv = USER_PRIV_USER;
	info.usri1_home_dir = nullptr;
	info.usri1_comment = comment_w.empty() ? nullptr : const_cast<LPWSTR>(comment_w.c_str());
	info.usri1_flags = UF_NORMAL_ACCOUNT;
	if (password_never_expires) {
		info.usri1_flags |= UF_DONT_EXPIRE_PASSWD;
	}
	if (password_cannot_change) {
		info.usri1_flags |= UF_PASSWD_CANT_CHANGE;
	}
	info.usri1_script_path = nullptr;

	DWORD parm_err = 0;
	const NET_API_STATUS status =
		NetUserAdd(nullptr, 1, reinterpret_cast<LPBYTE>(&info), &parm_err);
	if (status != NERR_Success) {
		std::string message = NetApiErrorMessage(status, "NetUserAdd");
		if (parm_err != 0) {
			message += absl::StrFormat(" (parameter error %lu)",
						   static_cast<unsigned long>(parm_err));
		}
		SetError(error_out, message);
		return false;
	}

	EnforcePasswordFlagsBestEffort(name_w, info.usri1_flags);

	const std::wstring display_w = Utf8ToWideLocal(display_name);
	if (!display_w.empty()) {
		SetDisplayNameBestEffort(name_w, display_w);
	}
	EnsureVisibleOnSignInScreenBestEffort(name_w);

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("Windows account creation"));
	return false;
#endif
}

bool DeleteLocalUser(const std::string& username, std::string* error_out) {
#ifdef _WIN32
	const std::wstring name_w = Utf8ToWideLocal(username);
	if (name_w.empty()) {
		SetError(error_out, "username could not be converted to UTF-16");
		return false;
	}

	const NET_API_STATUS status = NetUserDel(nullptr, name_w.c_str());
	if (status != NERR_Success) {
		SetError(error_out, NetApiErrorMessage(status, "NetUserDel"));
		return false;
	}

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("Windows account deletion"));
	return false;
#endif
}

bool ChangeLocalUserPassword(const std::string& username,
			     const std::string& new_password,
			     std::string* error_out) {
#ifdef _WIN32
	if (username.empty() || new_password.empty()) {
		SetError(error_out, "username and new_password are required");
		return false;
	}
	const std::wstring name_w = Utf8ToWideLocal(username);
	const std::wstring pass_w = Utf8ToWideLocal(new_password);
	if (name_w.empty() || pass_w.empty()) {
		SetError(error_out, "credentials could not be converted to UTF-16");
		return false;
	}

	USER_INFO_1003 info = {};
	info.usri1003_password = const_cast<LPWSTR>(pass_w.c_str());
	const NET_API_STATUS status = NetUserSetInfo(
	    nullptr, name_w.c_str(), 1003,
	    reinterpret_cast<LPBYTE>(&info), nullptr);
	if (status != NERR_Success) {
		SetError(error_out, NetApiErrorMessage(status, "NetUserSetInfo"));
		return false;
	}
	// Repairs accounts created before sign-in visibility was handled.
	EnsureVisibleOnSignInScreenBestEffort(name_w);
	if (error_out != nullptr) error_out->clear();
	return true;
#else
	SetError(error_out, NotSupported("Windows password change"));
	return false;
#endif
}

bool EnumerateLocalUsers(std::vector<std::string>* names_out,
			 std::string* error_out) {
#ifdef _WIN32
	if (names_out == nullptr) {
		SetError(error_out, "names_out must not be null");
		return false;
	}
	names_out->clear();

	constexpr DWORD kFilter = FILTER_NORMAL_ACCOUNT;
	constexpr DWORD kLevel = 0;  // names only
	DWORD resume = 0;
	// NetUserEnum requires a valid totalentries pointer — passing nullptr
	// crashes inside SAMCLI.DLL (access violation, not a clean error).
	DWORD total_entries = 0;
	for (;;) {
		LPBYTE buf = nullptr;
		DWORD entries_read = 0;
		const NET_API_STATUS status = NetUserEnum(
		    nullptr, kLevel, kFilter, &buf, MAX_PREFERRED_LENGTH,
		    &entries_read, &total_entries, &resume);
		if (status == ERROR_MORE_DATA || status == NERR_Success) {
			if (buf != nullptr) {
				auto* users = reinterpret_cast<USER_INFO_0*>(buf);
				for (DWORD i = 0; i < entries_read; ++i) {
					if (users[i].usri0_name != nullptr) {
						// USER_INFO_0 names are UTF-16 on
						// Windows; convert through WideCharTo
						// MultiByte via the existing helper in
						// reverse — kept simple: assume
						// ASCII-compatible and convert.
						const wchar_t* w = users[i].usri0_name;
						const int need = WideCharToMultiByte(
						    CP_UTF8, 0, w, -1, nullptr, 0,
						    nullptr, nullptr);
						if (need > 1) {
							std::string s(
							    static_cast<std::size_t>(need - 1),
							    '\0');
							WideCharToMultiByte(
							    CP_UTF8, 0, w, -1, s.data(),
							    need, nullptr, nullptr);
							names_out->push_back(std::move(s));
						}
					}
				}
				NetApiBufferFree(buf);
			}
			if (status == NERR_Success) break;
			continue;  // ERROR_MORE_DATA: next page
		}
		if (buf != nullptr) NetApiBufferFree(buf);
		SetError(error_out, NetApiErrorMessage(status, "NetUserEnum"));
		return false;
	}
	std::sort(names_out->begin(), names_out->end());
	if (error_out != nullptr) error_out->clear();
	return true;
#else
	SetError(error_out, NotSupported("Windows account enumeration"));
	return false;
#endif
}

bool VerifyLocalUserPassword(const std::string& username,
			     const std::string& password,
			     std::string* error_out) {
#ifdef _WIN32
	if (username.empty()) {
		SetError(error_out, "username is required");
		return false;
	}
	const std::wstring user_w = Utf8ToWideLocal(username);
	const std::wstring pass_w = Utf8ToWideLocal(password);
	if (user_w.empty() || pass_w.empty()) {
		SetError(error_out, "credentials could not be converted to UTF-16");
		return false;
	}

	HANDLE token = nullptr;
	if (!LogonUserW(user_w.c_str(), nullptr, pass_w.c_str(),
	                 LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT,
	                 &token)) {
		const DWORD err = GetLastError();
		if (error_out) {
			*error_out = err == ERROR_LOGON_FAILURE ||
			                     err == ERROR_ACCOUNT_RESTRICTION ||
			                     err == ERROR_ACCOUNT_DISABLED ||
			                     err == ERROR_PASSWORD_EXPIRED
			                 ? "password verification failed"
			                 : "LogonUser failed (error " +
			                       std::to_string(err) + ")";
		}
		return false;
	}
	CloseHandle(token);
	if (error_out != nullptr) error_out->clear();
	return true;
#else
	SetError(error_out, NotSupported("Windows password verification"));
	return false;
#endif
}

bool LocalUserExists(const std::string& username,
		     bool* exists_out,
		     std::string* error_out) {
#ifdef _WIN32
	if (exists_out == nullptr) {
		SetError(error_out, "exists_out must not be null");
		return false;
	}

	const std::wstring name_w = Utf8ToWideLocal(username);
	if (name_w.empty()) {
		SetError(error_out, "username could not be converted to UTF-16");
		return false;
	}

	LPBYTE info_buf = nullptr;
	const NET_API_STATUS status =
		NetUserGetInfo(nullptr, name_w.c_str(), 0, &info_buf);
	if (info_buf != nullptr) {
		NetApiBufferFree(info_buf);
	}

	if (status == NERR_Success) {
		*exists_out = true;
	} else if (status == NERR_UserNotFound) {
		*exists_out = false;
	} else {
		SetError(error_out, NetApiErrorMessage(status, "NetUserGetInfo"));
		return false;
	}

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("Windows account lookup"));
	return false;
#endif
}

bool GetLocalUserDetails(const std::string& username,
			 std::string* full_name_out,
			 std::string* comment_out,
			 bool* disabled_out,
			 std::string* error_out) {
#ifdef _WIN32
	const std::wstring name_w = Utf8ToWideLocal(username);
	if (name_w.empty()) {
		SetError(error_out, "username could not be converted to UTF-16");
		return false;
	}
	LPBYTE info_buf = nullptr;
	const NET_API_STATUS status =
		NetUserGetInfo(nullptr, name_w.c_str(), 2, &info_buf);
	if (status != NERR_Success || info_buf == nullptr) {
		if (info_buf != nullptr) {
			NetApiBufferFree(info_buf);
		}
		SetError(error_out, NetApiErrorMessage(status, "NetUserGetInfo"));
		return false;
	}
	const auto* info = reinterpret_cast<const USER_INFO_2*>(info_buf);
	if (full_name_out != nullptr) {
		*full_name_out = WideToUtf8ForDetails(info->usri2_full_name);
	}
	if (comment_out != nullptr) {
		*comment_out = WideToUtf8ForDetails(info->usri2_comment);
	}
	if (disabled_out != nullptr) {
		*disabled_out = (info->usri2_flags & UF_ACCOUNTDISABLE) != 0;
	}
	NetApiBufferFree(info_buf);
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("Windows account query"));
	return false;
#endif
}

bool UpdateLocalUserDetails(const std::string& username,
			    const std::string& full_name,
			    const std::string& comment,
			    bool disabled,
			    std::string* error_out) {
#ifdef _WIN32
	const std::wstring name_w = Utf8ToWideLocal(username);
	if (name_w.empty()) {
		SetError(error_out, "username could not be converted to UTF-16");
		return false;
	}
	const std::wstring full_w = Utf8ToWideLocal(full_name);
	const std::wstring comment_w = Utf8ToWideLocal(comment);

	USER_INFO_1011 full_info = {};
	full_info.usri1011_full_name = const_cast<LPWSTR>(full_w.c_str());
	NET_API_STATUS status = NetUserSetInfo(
	    nullptr, name_w.c_str(), 1011, reinterpret_cast<LPBYTE>(&full_info),
	    nullptr);
	if (status != NERR_Success) {
		SetError(error_out, NetApiErrorMessage(status, "NetUserSetInfo(full name)"));
		return false;
	}

	USER_INFO_1007 comment_info = {};
	comment_info.usri1007_comment = const_cast<LPWSTR>(comment_w.c_str());
	status = NetUserSetInfo(nullptr, name_w.c_str(), 1007,
				reinterpret_cast<LPBYTE>(&comment_info), nullptr);
	if (status != NERR_Success) {
		SetError(error_out, NetApiErrorMessage(status, "NetUserSetInfo(description)"));
		return false;
	}

	LPBYTE info_buf = nullptr;
	status = NetUserGetInfo(nullptr, name_w.c_str(), 1, &info_buf);
	if (status != NERR_Success || info_buf == nullptr) {
		if (info_buf != nullptr) {
			NetApiBufferFree(info_buf);
		}
		SetError(error_out, NetApiErrorMessage(status, "NetUserGetInfo"));
		return false;
	}
	const DWORD current_flags =
		reinterpret_cast<const USER_INFO_1*>(info_buf)->usri1_flags;
	NetApiBufferFree(info_buf);
	const DWORD desired_flags = disabled ? (current_flags | UF_ACCOUNTDISABLE)
					     : (current_flags & ~UF_ACCOUNTDISABLE);
	if (desired_flags != current_flags) {
		USER_INFO_1008 flags_info = {};
		flags_info.usri1008_flags = desired_flags;
		status = NetUserSetInfo(nullptr, name_w.c_str(), 1008,
					reinterpret_cast<LPBYTE>(&flags_info), nullptr);
		if (status != NERR_Success) {
			SetError(error_out, NetApiErrorMessage(status, "NetUserSetInfo(flags)"));
			return false;
		}
	}
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("Windows account update"));
	return false;
#endif
}

bool RenameLocalUser(const std::string& username,
		     const std::string& new_username,
		     std::string* error_out) {
#ifdef _WIN32
	const std::wstring name_w = Utf8ToWideLocal(username);
	const std::wstring new_w = Utf8ToWideLocal(new_username);
	if (name_w.empty() || new_w.empty()) {
		SetError(error_out, "username could not be converted to UTF-16");
		return false;
	}
	USER_INFO_0 info = {};
	info.usri0_name = const_cast<LPWSTR>(new_w.c_str());
	const NET_API_STATUS status = NetUserSetInfo(
	    nullptr, name_w.c_str(), 0, reinterpret_cast<LPBYTE>(&info), nullptr);
	if (status != NERR_Success) {
		SetError(error_out, NetApiErrorMessage(status, "NetUserSetInfo(rename)"));
		return false;
	}
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("Windows account rename"));
	return false;
#endif
}

bool ProcessIsElevated() {
#ifdef _WIN32
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}

	TOKEN_ELEVATION elevation = {};
	DWORD returned = 0;
	const BOOL ok = GetTokenInformation(
		token, TokenElevation, &elevation, sizeof(elevation), &returned);
	CloseHandle(token);
	return ok == TRUE && elevation.TokenIsElevated != 0;
#else
	return false;
#endif
}

std::wstring Utf8ToWideLocal(std::string_view utf8) {
#ifdef _WIN32
	if (utf8.empty()) {
		return std::wstring();
	}

	const int needed = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
		static_cast<int>(utf8.size()), nullptr, 0);
	if (needed <= 0) {
		return std::wstring();
	}

	std::wstring out(static_cast<std::size_t>(needed), L'\0');
	const int written = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
		static_cast<int>(utf8.size()), out.data(), needed);
	if (written <= 0) {
		return std::wstring();
	}
	return out;
#else
	return std::wstring();
#endif
}

}  // namespace XER::SecurityFlash::Detail
