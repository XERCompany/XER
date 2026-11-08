#include "security_flash/removable_media.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <algorithm>
#include <cctype>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#endif

namespace XER::SecurityFlash::Detail {
namespace {

std::string NotSupported(const char* what) {
	return absl::StrCat(what, " is not supported on this platform");
}

void SetError(std::string* error_out, const std::string& message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

#ifdef _WIN32

std::wstring Utf8ToWide(std::string_view utf8) {
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
}

std::string WideToUtf8(const wchar_t* wide) {
	if (wide == nullptr || wide[0] == L'\0') {
		return std::string();
	}
	const int needed = WideCharToMultiByte(
		CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 0) {
		return std::string();
	}
	std::string out(static_cast<std::size_t>(needed), '\0');
	WideCharToMultiByte(
		CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
	if (!out.empty() && out.back() == '\0') {
		out.pop_back();
	}
	return out;
}

// Queries serial/label/space for an already-normalized root "X:\\".
void FillDriveInfo(const std::wstring& root_w, char letter, DriveInfo* info) {
	info->letter = letter;
	info->root_path = absl::StrCat(std::string(1, letter), ":\\");

	wchar_t label[256] = {};
	DWORD serial = 0;
	if (GetVolumeInformationW(root_w.c_str(), label, 256, &serial,
				  nullptr, nullptr, nullptr, 0)) {
		info->volume_serial = serial;
		info->volume_label = WideToUtf8(label);
	}

	ULARGE_INTEGER free_available = {};
	ULARGE_INTEGER total = {};
	ULARGE_INTEGER total_free = {};
	if (GetDiskFreeSpaceExW(root_w.c_str(), &free_available, &total, &total_free)) {
		info->total_bytes = total.QuadPart;
		info->free_bytes = free_available.QuadPart;
		info->ready = true;
	}
}

#endif  // _WIN32

}  // namespace

bool NormalizeDriveRoot(std::string_view input, std::string* root_out) {
	if (root_out == nullptr) {
		return false;
	}

	// Skip leading whitespace/quotes; locate the drive letter.
	std::size_t pos = 0;
	while (pos < input.size()
	       && (input[pos] == ' ' || input[pos] == '"' || input[pos] == '\'')) {
		++pos;
	}
	if (pos >= input.size() || !std::isalpha(static_cast<unsigned char>(input[pos]))) {
		return false;
	}

	// The letter must be followed by ':' or '\\' or end of string.
	if (pos + 1 < input.size()
	    && input[pos + 1] != ':' && input[pos + 1] != '\\') {
		return false;
	}

	const char letter = static_cast<char>(
		std::toupper(static_cast<unsigned char>(input[pos])));
	*root_out = absl::StrCat(std::string(1, letter), ":\\");
	return true;
}

bool QueryDriveInfo(const std::string& root,
		    DriveInfo* info_out,
		    std::string* error_out) {
#ifdef _WIN32
	if (info_out == nullptr) {
		SetError(error_out, "info_out must not be null");
		return false;
	}
	if (root.size() < 3 || root[1] != ':' || root[2] != '\\') {
		SetError(error_out, absl::StrCat("invalid drive root: '", root, "'"));
		return false;
	}

	const std::wstring root_w = Utf8ToWide(root);
	if (root_w.empty()) {
		SetError(error_out, "drive root could not be converted to UTF-16");
		return false;
	}

	const UINT type = GetDriveTypeW(root_w.c_str());
	DriveInfo info;
	info.removable = (type == DRIVE_REMOVABLE);
	FillDriveInfo(root_w, root[0], &info);
	info.ready = info.ready && type != DRIVE_NO_ROOT_DIR;

	*info_out = std::move(info);
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("drive query"));
	return false;
#endif
}

std::vector<DriveInfo> EnumerateLocalDrives() {
	std::vector<DriveInfo> drives;
#ifdef _WIN32
	const DWORD mask = GetLogicalDrives();
	for (int i = 0; i < 26; ++i) {
		if ((mask & (1u << i)) == 0) {
			continue;
		}

		wchar_t root_w[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
		const UINT type = GetDriveTypeW(root_w);
		if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_CDROM
		    || type == DRIVE_REMOTE) {
			continue;
		}

		DriveInfo info;
		info.removable = (type == DRIVE_REMOVABLE);
		FillDriveInfo(root_w, static_cast<char>('A' + i), &info);
		info.ready = info.ready && type != DRIVE_NO_ROOT_DIR;
		drives.push_back(std::move(info));
	}
#endif
	return drives;
}

bool VolumeSerialForPath(const std::string& path,
			 std::uint32_t* serial_out,
			 std::string* error_out) {
#ifdef _WIN32
	if (serial_out == nullptr) {
		SetError(error_out, "serial_out must not be null");
		return false;
	}

	const std::wstring path_w = Utf8ToWide(path);
	if (path_w.empty()) {
		SetError(error_out, "path could not be converted to UTF-16");
		return false;
	}

	// Resolve any path on the volume to its root ("E:\\").
	wchar_t volume_root[MAX_PATH] = {};
	if (!GetVolumePathNameW(path_w.c_str(), volume_root, MAX_PATH)) {
		SetError(error_out, absl::StrFormat(
			"cannot resolve volume for path '%s' (error %lu)",
			path, static_cast<unsigned long>(GetLastError())));
		return false;
	}

	DWORD serial = 0;
	if (!GetVolumeInformationW(volume_root, nullptr, 0, &serial,
				   nullptr, nullptr, nullptr, 0)) {
		SetError(error_out, absl::StrFormat(
			"cannot read volume serial for '%s' (error %lu)",
			WideToUtf8(volume_root), static_cast<unsigned long>(GetLastError())));
		return false;
	}

	*serial_out = serial;
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
#else
	SetError(error_out, NotSupported("volume serial query"));
	return false;
#endif
}

}  // namespace XER::SecurityFlash::Detail
