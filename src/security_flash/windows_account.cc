#include "security_flash/windows_account.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

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
