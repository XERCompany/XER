#include "security_flash/security_flash.h"

#include "security_flash/removable_media.h"
#include "security_flash/token_store.h"
#include "security_flash/windows_account.h"

#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/strip.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace XER::SecurityFlash {
namespace {

constexpr std::size_t kMaxTokenFileBytes = 2u * 1024u * 1024u;
constexpr std::size_t kMinPasswordLength = 12;
constexpr std::size_t kMaxPasswordLength = 64;

bool IsBlankStr(std::string_view text) {
	return std::all_of(text.begin(), text.end(), [](char ch) {
		return absl::ascii_isspace(static_cast<unsigned char>(ch));
	});
}

std::string TrimStr(std::string_view text) {
	return std::string(absl::StripAsciiWhitespace(text));
}

void SetError(std::string* error_out, const std::string& message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

// Bare drive spec: "E", "E:", "E:\\" (optionally quoted / spaced).
bool IsBareDriveSpec(std::string_view input, char* letter_out) {
	std::string text = TrimStr(input);
	if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
		text = text.substr(1, text.size() - 2);
	}
	if (text.empty() || text.size() > 3) {
		return false;
	}
	if (!std::isalpha(static_cast<unsigned char>(text[0]))) {
		return false;
	}
	std::size_t pos = 1;
	if (pos < text.size() && text[pos] == ':') {
		++pos;
	}
	if (pos < text.size() && text[pos] == '\\') {
		++pos;
	}
	if (pos != text.size()) {
		return false;
	}
	if (letter_out != nullptr) {
		*letter_out = static_cast<char>(
			std::toupper(static_cast<unsigned char>(text[0])));
	}
	return true;
}

// Resolves user input to a directory that hosts the token file.
// Accepts a bare drive spec ("E", "E:", "E:\\") or an existing directory.
bool ResolveTokenDirectory(const std::string& input,
			   std::string* dir_out,
			   std::string* error_out) {
	char letter = 0;
	if (IsBareDriveSpec(input, &letter)) {
		const std::string root = absl::StrCat(std::string(1, letter), ":\\");
		std::error_code ec;
		if (!std::filesystem::is_directory(root, ec) || ec) {
			SetError(error_out, absl::StrCat(
				"drive '", root, "' is not ready or has no media"));
			return false;
		}
		*dir_out = root;
		return true;
	}

	const std::string trimmed = TrimStr(input);
	std::error_code ec;
	if (!trimmed.empty()
	    && std::filesystem::is_directory(trimmed, ec) && !ec) {
		*dir_out = trimmed;
		return true;
	}

	SetError(error_out, absl::StrCat(
		"'", input, "' is neither a ready drive nor an existing directory"));
	return false;
}

std::string TokenPathForDir(const std::string& dir) {
	return (std::filesystem::path(dir) / std::string(kTokenFileName)).string();
}

bool PathExists(const std::string& path) {
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}

// Internal token ops on an already-resolved directory.
bool TokenFileExistsInDir(const std::string& dir) {
	return PathExists(TokenPathForDir(dir));
}

bool WriteTokenToDir(const std::string& dir,
		     const TokenPayload& payload,
		     const std::string& passphrase,
		     std::string* error_out) {
	std::vector<std::uint8_t> file_bytes;
	if (!Detail::BuildTokenFile(payload, passphrase, &file_bytes, error_out)) {
		return false;
	}

	const std::string token_path = TokenPathForDir(dir);

#ifdef _WIN32
	// A file marked hidden+system (or read-only) refuses plain ofstream
	// writes — normalize attributes before rewriting an existing token.
	const std::wstring wide_path = Detail::Utf8ToWideLocal(token_path);
	if (!wide_path.empty() && PathExists(token_path)) {
		SetFileAttributesW(wide_path.c_str(), FILE_ATTRIBUTE_NORMAL);
	}
#endif

	{
		std::ofstream output(token_path, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) {
			SetError(error_out, absl::StrCat(
				"cannot open token file for writing: '", token_path, "'"));
			return false;
		}
		output.write(reinterpret_cast<const char*>(file_bytes.data()),
			     static_cast<std::streamsize>(file_bytes.size()));
		output.flush();
		if (!output.good()) {
			SetError(error_out, absl::StrCat(
				"failed writing token file: '", token_path, "'"));
			return false;
		}
	}

#ifdef _WIN32
	if (!wide_path.empty()) {
		SetFileAttributesW(wide_path.c_str(),
				   FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
	}
#endif

	OPENSSL_cleanse(file_bytes.data(), file_bytes.size());
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

bool ReadTokenFromDir(const std::string& dir,
		      const std::string& passphrase,
		      TokenPayload* payload_out,
		      std::string* error_out) {
	const std::string token_path = TokenPathForDir(dir);

	std::error_code ec;
	const auto file_size = std::filesystem::file_size(token_path, ec);
	if (ec || file_size == 0 || file_size > kMaxTokenFileBytes) {
		SetError(error_out, absl::StrCat(
			"token file is missing or has an invalid size: '", token_path, "'"));
		return false;
	}

	std::ifstream input(token_path, std::ios::binary);
	if (!input.is_open()) {
		SetError(error_out, absl::StrCat(
			"cannot open token file: '", token_path, "'"));
		return false;
	}
	std::vector<std::uint8_t> file_bytes(
		static_cast<std::size_t>(file_size));
	input.read(reinterpret_cast<char*>(file_bytes.data()),
		   static_cast<std::streamsize>(file_bytes.size()));
	if (!input.good()) {
		SetError(error_out, absl::StrCat(
			"failed reading token file: '", token_path, "'"));
		return false;
	}

	if (!Detail::ParseTokenFile(file_bytes, passphrase, payload_out, error_out)) {
		return false;
	}

	// Volume binding: the token only opens on the flash drive it was
	// written to. Unbound tokens (volume_serial == 0) skip the check.
	if (payload_out->volume_serial != 0) {
		std::uint32_t current_serial = 0;
		if (!Detail::VolumeSerialForPath(dir, &current_serial, error_out)) {
			return false;
		}
		if (current_serial != payload_out->volume_serial) {
			SetError(error_out,
				 "token is bound to a different volume — "
				 "insert the original flash drive");
			return false;
		}
	}

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

bool EraseTokenFromDir(const std::string& dir, std::string* error_out) {
	const std::string token_path = TokenPathForDir(dir);
	if (!PathExists(token_path)) {
		SetError(error_out, absl::StrCat("no token found at '", token_path, "'"));
		return false;
	}

	std::error_code ec;
	const auto file_size = std::filesystem::file_size(token_path, ec);
	const std::size_t wipe_size = ec ? 0 : static_cast<std::size_t>(file_size);

	// Overwrite once with random bytes before deleting — the payload is
	// encrypted, this just removes the artifact cleanly.
	if (wipe_size > 0) {
		std::vector<std::uint8_t> random_bytes(wipe_size);
		if (RAND_bytes(random_bytes.data(), static_cast<int>(wipe_size)) == 1) {
			std::ofstream output(token_path,
					     std::ios::binary | std::ios::trunc);
			if (output.is_open()) {
				output.write(
					reinterpret_cast<const char*>(random_bytes.data()),
					static_cast<std::streamsize>(random_bytes.size()));
				output.flush();
			}
			OPENSSL_cleanse(random_bytes.data(), random_bytes.size());
		}
	}

#ifdef _WIN32
	const std::wstring wide_path = Detail::Utf8ToWideLocal(token_path);
	if (!wide_path.empty()) {
		SetFileAttributesW(wide_path.c_str(), FILE_ATTRIBUTE_NORMAL);
	}
#endif

	if (!std::filesystem::remove(token_path, ec) || ec) {
		SetError(error_out, absl::StrCat(
			"cannot delete token file: '", token_path, "'"));
		return false;
	}

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

std::string UtcNowIso() {
	const auto now = std::chrono::system_clock::now();
	const std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm tm_buf = {};
#ifdef _WIN32
	gmtime_s(&tm_buf, &time);
#else
	gmtime_r(&time, &tm_buf);
#endif
	char buffer[32] = {};
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
	return buffer;
}

std::string HostName() {
#ifdef _WIN32
	char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
	DWORD size = sizeof(name);
	if (GetComputerNameA(name, &size) != 0) {
		return name;
	}
	return "unknown";
#else
	char name[256] = {};
	if (gethostname(name, sizeof(name) - 1) == 0) {
		return name;
	}
	return "unknown";
#endif
}

bool RandBelow(std::uint32_t bound, std::uint32_t* out) {
	if (bound == 0 || out == nullptr) {
		return false;
	}
	const std::uint32_t limit =
		0xFFFFFFFFu - (0xFFFFFFFFu % bound);
	std::uint32_t value = 0;
	while (true) {
		if (RAND_bytes(reinterpret_cast<std::uint8_t*>(&value),
			       sizeof(value)) != 1) {
			return false;
		}
		if (value < limit) {
			break;
		}
	}
	*out = value % bound;
	return true;
}

constexpr std::string_view kPasswordUpper = "ABCDEFGHJKLMNPQRSTUVWXYZ";
constexpr std::string_view kPasswordLower = "abcdefghijkmnpqrstuvwxyz";
constexpr std::string_view kPasswordDigits = "23456789";
constexpr std::string_view kPasswordSymbols = "!@#$%^&*()-_=+";
constexpr std::string_view kPasswordAll =
	"ABCDEFGHJKLMNPQRSTUVWXYZ"
	"abcdefghijkmnpqrstuvwxyz"
	"23456789"
	"!@#$%^&*()-_=+";
constexpr std::string_view kUsernameAlphabet =
	"abcdefghijkmnpqrstuvwxyz23456789";

char RandomCharFrom(std::string_view set, bool* ok) {
	std::uint32_t index = 0;
	if (!RandBelow(static_cast<std::uint32_t>(set.size()), &index)) {
		*ok = false;
		return '\0';
	}
	return set[static_cast<std::size_t>(index)];
}

// Candidate drives for provisioning / deprovision search.
std::vector<std::string> CandidateDriveDirs(const DeprovisionOptions& options,
					    bool removable_only,
					    std::string* error_out) {
	std::vector<std::string> dirs;
	if (!options.drive_letter.empty()) {
		std::string dir;
		if (ResolveTokenDirectory(options.drive_letter, &dir, error_out)) {
			dirs.push_back(std::move(dir));
		}
		return dirs;
	}

	for (const DriveInfo& drive : SecurityFlash::EnumerateDrives(removable_only)) {
		if (drive.ready) {
			dirs.push_back(drive.root_path);
		}
	}
	return dirs;
}

}  // namespace

std::vector<DriveInfo> SecurityFlash::EnumerateDrives(bool removable_only) {
	std::vector<DriveInfo> all = Detail::EnumerateLocalDrives();
	if (!removable_only) {
		return all;
	}
	all.erase(std::remove_if(all.begin(), all.end(),
				 [](const DriveInfo& d) { return !d.removable; }),
		  all.end());
	return all;
}

bool SecurityFlash::QueryDrive(const std::string& drive_or_root,
			       DriveInfo* info_out,
			       std::string* error_out) {
	char letter = 0;
	std::string root;
	if (!IsBareDriveSpec(drive_or_root, &letter)) {
		SetError(error_out, absl::StrCat(
			"expected a drive specification like 'E:' — got '",
			drive_or_root, "'"));
		return false;
	}
	root = absl::StrCat(std::string(1, letter), ":\\");
	return Detail::QueryDriveInfo(root, info_out, error_out);
}

ProvisionResult SecurityFlash::Provision(const ProvisionOptions& options) {
	ProvisionResult result;

	if (IsBlankStr(options.passphrase)) {
		result.error = "passphrase is required to encrypt the token";
		return result;
	}

	// Resolve the target drive before touching the account database.
	DriveInfo drive;
	if (!options.drive_letter.empty()) {
		if (!QueryDrive(options.drive_letter, &drive, &result.error)) {
			return result;
		}
		if (!drive.ready) {
			result.error = absl::StrCat(
				"drive '", options.drive_letter,
				"' has no media or is not ready");
			return result;
		}
		if (!drive.removable && !options.allow_non_removable_drive) {
			result.error = absl::StrCat(
				"drive '", options.drive_letter,
				"' is not removable — only flash drives are allowed "
				"(set allow_non_removable_drive to override)");
			return result;
		}
	} else {
		bool found = false;
		for (const DriveInfo& candidate : EnumerateDrives(true)) {
			if (candidate.ready) {
				drive = candidate;
				found = true;
				break;
			}
		}
		if (!found) {
			result.error = "no ready removable drive found";
			return result;
		}
	}
	result.drive_letter = absl::StrCat(std::string(1, drive.letter), ":");

	// Resolve credentials.
	std::string username = TrimStr(options.username);
	if (username.empty() && options.generate_username) {
		username = GenerateUsername(&result.error);
		if (username.empty()) {
			return result;
		}
	}
	if (username.empty()) {
		result.error = "username is required (or set generate_username)";
		return result;
	}
	if (!IsValidUsername(username)) {
		result.error = absl::StrCat("invalid Windows username: '", username, "'");
		return result;
	}

	std::string password = options.password;
	bool password_generated = false;
	if (password.empty()) {
		password = GeneratePassword(20, &result.error);
		if (password.empty()) {
			return result;
		}
		password_generated = true;
	}

	// Create the Windows account.
	bool user_created = false;
	if (options.create_windows_user) {
		if (!Detail::ProcessIsElevated()) {
			result.error =
				"creating a Windows user requires an elevated process";
			return result;
		}

		bool exists = false;
		if (!UserExists(username, &result.error)) {
			return result;
		}
		if (exists) {
			result.error = absl::StrCat(
				"Windows user '", username, "' already exists");
			return result;
		}

		if (!CreateWindowsUser(username, password,
				       options.display_name, options.comment,
				       options.password_never_expires,
				       options.user_cannot_change_password,
				       &result.error)) {
			return result;
		}
		user_created = true;
	}

	// Write the encrypted token to the flash drive.
	TokenPayload payload;
	payload.username = username;
	payload.password = password;
	payload.created_utc = UtcNowIso();
	payload.host_name = HostName();
	payload.volume_serial = options.bind_to_volume ? drive.volume_serial : 0;

	if (!WriteToken(drive.root_path, payload, options.passphrase, &result.error)) {
		// Roll back the account so a failed provision leaves no orphan user.
		if (user_created) {
			std::string rollback_error;
			DeleteWindowsUser(username, &rollback_error);
		}
		OPENSSL_cleanse(payload.password.data(), payload.password.size());
		OPENSSL_cleanse(password.data(), password.size());
		return result;
	}
	OPENSSL_cleanse(payload.password.data(), payload.password.size());

	result.success = true;
	result.username = username;
	result.password_was_generated = password_generated;
	// The generated password is returned once, before the local copy is
	// wiped — callers must SecureClear() result.password after use.
	result.password = password_generated ? password : std::string();
	result.token_path = TokenPathForDir(drive.root_path);
	OPENSSL_cleanse(password.data(), password.size());
	return result;
}

DeprovisionResult SecurityFlash::Deprovision(const DeprovisionOptions& options) {
	DeprovisionResult result;

	const std::vector<std::string> dirs =
		CandidateDriveDirs(options, true, &result.error);
	if (!result.error.empty() && dirs.empty()) {
		return result;
	}

	// Erase tokens wherever they exist.
	if (options.erase_token) {
		for (const std::string& dir : dirs) {
			if (!TokenFileExistsInDir(dir)) {
				continue;
			}
			std::string erase_error;
			if (EraseTokenFromDir(dir, &erase_error)) {
				result.token_erased = true;
			} else {
				result.error = erase_error;
				return result;
			}
		}
	}

	// Resolve the account name.
	std::string username = TrimStr(options.username);
	if (username.empty() && options.delete_windows_user) {
		if (IsBlankStr(options.passphrase)) {
			result.error =
				"username is required, or provide passphrase to "
				"recover it from the token";
			return result;
		}
		for (const std::string& dir : dirs) {
			if (!TokenFileExistsInDir(dir)) {
				continue;
			}
			TokenPayload payload;
			std::string read_error;
			if (ReadTokenFromDir(dir, options.passphrase, &payload,
					     &read_error)) {
				username = payload.username;
				OPENSSL_cleanse(payload.password.data(),
					       payload.password.size());
				break;
			}
		}
		if (username.empty()) {
			result.error =
				"could not recover username from any token";
			return result;
		}
	}
	result.username = username;

	// Delete the Windows account.
	if (options.delete_windows_user) {
		if (username.empty()) {
			result.error = "username is required to delete the account";
			return result;
		}

		bool exists = false;
		if (!UserExists(username, &result.error)) {
			return result;
		}
		if (!exists) {
			// Already gone — deprovision is idempotent.
		} else if (!DeleteWindowsUser(username, &result.error)) {
			return result;
		} else {
			result.user_deleted = true;
		}
	}

	if (!result.error.empty()) {
		return result;
	}
	result.success = true;
	return result;
}

bool SecurityFlash::WriteToken(const std::string& drive_root,
			       const TokenPayload& payload,
			       const std::string& passphrase,
			       std::string* error_out) {
	std::string dir;
	if (!ResolveTokenDirectory(drive_root, &dir, error_out)) {
		return false;
	}
	return WriteTokenToDir(dir, payload, passphrase, error_out);
}

bool SecurityFlash::ReadToken(const std::string& drive_root,
			      const std::string& passphrase,
			      TokenPayload* payload_out,
			      std::string* error_out) {
	if (payload_out == nullptr) {
		SetError(error_out, "payload_out must not be null");
		return false;
	}
	std::string dir;
	if (!ResolveTokenDirectory(drive_root, &dir, error_out)) {
		return false;
	}
	return ReadTokenFromDir(dir, passphrase, payload_out, error_out);
}

bool SecurityFlash::EraseToken(const std::string& drive_root,
			       std::string* error_out) {
	std::string dir;
	if (!ResolveTokenDirectory(drive_root, &dir, error_out)) {
		return false;
	}
	return EraseTokenFromDir(dir, error_out);
}

bool SecurityFlash::TokenExists(const std::string& drive_root) {
	std::string dir;
	if (!ResolveTokenDirectory(drive_root, &dir, nullptr)) {
		return false;
	}
	return TokenFileExistsInDir(dir);
}

bool SecurityFlash::IsValidUsername(std::string_view username) {
	if (username.empty() || username.size() > 20) {
		return false;
	}
	if (username.front() == '.' || username.back() == '.'
	    || username.front() == ' ' || username.back() == ' ') {
		return false;
	}
	return std::all_of(username.begin(), username.end(), [](char ch) {
		const unsigned char c = static_cast<unsigned char>(ch);
		return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z')
			|| ch == '.' || ch == '_' || ch == '-' || ch == '@';
	});
}

bool SecurityFlash::UserExists(const std::string& username,
			       std::string* error_out) {
	bool exists = false;
	if (!Detail::LocalUserExists(username, &exists, error_out)) {
		return false;
	}
	return exists;
}

bool SecurityFlash::CreateWindowsUser(const std::string& username,
				      const std::string& password,
				      const std::string& display_name,
				      const std::string& comment,
				      bool password_never_expires,
				      bool user_cannot_change_password,
				      std::string* error_out) {
	if (!IsValidUsername(username)) {
		SetError(error_out, absl::StrCat("invalid username: '", username, "'"));
		return false;
	}
	if (password.empty()) {
		SetError(error_out, "password must not be empty");
		return false;
	}
	return Detail::CreateLocalUser(username, password, display_name, comment,
				       password_never_expires,
				       user_cannot_change_password, error_out);
}

bool SecurityFlash::DeleteWindowsUser(const std::string& username,
				      std::string* error_out) {
	if (!IsValidUsername(username)) {
		SetError(error_out, absl::StrCat("invalid username: '", username, "'"));
		return false;
	}
	return Detail::DeleteLocalUser(username, error_out);
}

bool SecurityFlash::IsAdministrator() {
	return Detail::ProcessIsElevated();
}

std::string SecurityFlash::GeneratePassword(std::size_t length,
					    std::string* error_out) {
	if (length < kMinPasswordLength || length > kMaxPasswordLength) {
		SetError(error_out, absl::StrFormat(
			"password length must be between %zu and %zu",
			kMinPasswordLength, kMaxPasswordLength));
		return std::string();
	}

	bool ok = true;
	std::string password;
	password.reserve(length);

	// Guarantee one character from each class.
	for (std::string_view set :
	     {kPasswordUpper, kPasswordLower, kPasswordDigits, kPasswordSymbols}) {
		password.push_back(RandomCharFrom(set, &ok));
	}
	while (password.size() < length) {
		password.push_back(RandomCharFrom(kPasswordAll, &ok));
	}
	if (!ok) {
		SetError(error_out, "cryptographic RNG failed");
		OPENSSL_cleanse(password.data(), password.size());
		return std::string();
	}

	// Fisher-Yates shuffle so the guaranteed characters are not positional.
	for (std::size_t i = password.size() - 1; i > 0; --i) {
		std::uint32_t j = 0;
		if (!RandBelow(static_cast<std::uint32_t>(i + 1), &j)) {
			SetError(error_out, "cryptographic RNG failed");
			OPENSSL_cleanse(password.data(), password.size());
			return std::string();
		}
		std::swap(password[i], password[static_cast<std::size_t>(j)]);
	}

	if (error_out != nullptr) {
		error_out->clear();
	}
	return password;
}

std::string SecurityFlash::GenerateUsername(std::string* error_out) {
	constexpr std::size_t kSuffixLength = 10;
	std::string username = "xer_";
	username.reserve(username.size() + kSuffixLength);
	for (std::size_t i = 0; i < kSuffixLength; ++i) {
		bool ok = true;
		username.push_back(RandomCharFrom(kUsernameAlphabet, &ok));
		if (!ok) {
			SetError(error_out, "cryptographic RNG failed");
			return std::string();
		}
	}
	if (error_out != nullptr) {
		error_out->clear();
	}
	return username;
}

void SecurityFlash::SecureClear(std::string& value) {
	OPENSSL_cleanse(value.data(), value.size());
	value.clear();
}

}  // namespace XER::SecurityFlash
