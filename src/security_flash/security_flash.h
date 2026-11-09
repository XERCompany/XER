#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace XER::SecurityFlash {

// File name of the credential token written to the root of the flash drive.
// The file is marked hidden + system after writing.
inline constexpr std::string_view kTokenFileName = "xer_flash.tok";

// SecurityFlashHello flash-only key: the token passphrase sealed with
// DPAPI (machine scope), so the drive signs in without a PIN on this PC.
inline constexpr std::string_view kLoginKeyFileName = "xer_flash.key";

// Information about a mounted drive.
struct DriveInfo {
	char letter = 0;                // 'E'
	std::string root_path;          // "E:\\"
	std::string volume_label;
	std::uint32_t volume_serial = 0;
	std::uint64_t total_bytes = 0;
	std::uint64_t free_bytes = 0;
	bool removable = false;         // DRIVE_REMOVABLE (USB flash etc.)
	bool ready = false;             // media present and space queryable
};

// Editable properties of a local Windows account.
struct WindowsUserDetails {
	std::string full_name;          // display name on the sign-in screen
	std::string comment;            // description
	bool disabled = false;          // UF_ACCOUNTDISABLE
};

// Decrypted contents of a token. The password field is sensitive —
// callers must wipe it (SecureClear) as soon as it is no longer needed.
struct TokenPayload {
	std::string username;
	std::string password;
	std::string created_utc;        // ISO-8601 UTC, e.g. "2026-11-08T12:34:56Z"
	std::string host_name;          // host that provisioned the token
	std::uint32_t volume_serial = 0;  // non-zero binds token to that volume
};

struct ProvisionOptions {
	std::string username;           // required unless generate_username
	std::string password;           // empty -> generated (returned in result)
	std::string display_name;
	std::string comment;
	std::string passphrase;         // required — encrypts the token on the drive
	std::string drive_letter;       // "E" / "E:" / "E:\\" — empty -> first ready removable drive
	bool generate_username = false;
	bool password_never_expires = true;
	bool user_cannot_change_password = true;
	bool bind_to_volume = true;     // token only readable from the same flash drive
	bool create_windows_user = true;  // false -> write token only, no account
	bool allow_non_removable_drive = false;  // USB HDDs report as fixed
};

struct ProvisionResult {
	bool success = false;
	bool password_was_generated = false;
	std::string username;
	std::string password;           // sensitive — do not log
	std::string drive_letter;
	std::string token_path;
	std::string error;
};

struct DeprovisionOptions {
	std::string username;           // empty -> recovered from token (needs passphrase)
	std::string drive_letter;       // empty -> search all removable drives
	std::string passphrase;         // required only when username must come from token
	bool delete_windows_user = true;
	bool erase_token = true;
};

struct DeprovisionResult {
	bool success = false;
	bool user_deleted = false;
	bool token_erased = false;
	std::string username;
	std::string error;
};

// XER::SecurityFlash — USB-bound Windows credential protection.
//
// Provision() creates a local Windows user account (requires elevation)
// and writes the login credentials as an AES-256-GCM encrypted token
// onto a removable flash drive. The plaintext password never touches
// the disk outside the encrypted container and lives in the Windows
// SAM database only as a hash.
//
// Windows account operations are Windows-only; on other platforms they
// report "not supported". Token encrypt/decrypt is portable.
class SecurityFlash {
public:
	// Drive enumeration
	static std::vector<DriveInfo> EnumerateDrives(bool removable_only = true);
	static bool QueryDrive(const std::string& drive_or_root,
			       DriveInfo* info_out,
			       std::string* error_out = nullptr);

	// Provisioning: Windows user + encrypted token on flash media
	static ProvisionResult Provision(const ProvisionOptions& options);

	// Token container operations. drive_root accepts "E", "E:", "E:\\"
	// or any directory on the target volume.
	static bool WriteToken(const std::string& drive_root,
			       const TokenPayload& payload,
			       const std::string& passphrase,
			       std::string* error_out = nullptr);
	static bool ReadToken(const std::string& drive_root,
			      const std::string& passphrase,
			      TokenPayload* payload_out,
			      std::string* error_out = nullptr);
	static bool EraseToken(const std::string& drive_root,
			       std::string* error_out = nullptr);
	static bool TokenExists(const std::string& drive_root);

	// Windows local account management
	static bool IsValidUsername(std::string_view username);
	static bool UserExists(const std::string& username,
				std::string* error_out = nullptr);
	static bool CreateWindowsUser(const std::string& username,
				      const std::string& password,
				      const std::string& display_name,
				      const std::string& comment,
				      bool password_never_expires,
				      bool user_cannot_change_password,
				      std::string* error_out = nullptr);
	static bool DeleteWindowsUser(const std::string& username,
				     std::string* error_out = nullptr);

	// Resets a local account password (NetUserSetInfo level 1003).
	// Requires elevation unless changing the caller's own password.
	static bool ChangeWindowsPassword(const std::string& username,
					  const std::string& new_password,
					  std::string* error_out = nullptr);

	// Account editing (NetUserGetInfo / NetUserSetInfo). Update and
	// rename require elevation.
	static bool GetWindowsUserDetails(const std::string& username,
					  WindowsUserDetails* details_out,
					  std::string* error_out = nullptr);
	static bool UpdateWindowsUserDetails(const std::string& username,
					     const WindowsUserDetails& details,
					     std::string* error_out = nullptr);
	static bool RenameWindowsUser(const std::string& username,
				      const std::string& new_username,
				      std::string* error_out = nullptr);

	// Lists local account names (NetUserEnum, normal accounts).
	static bool EnumerateLocalUsers(std::vector<std::string>* names_out,
					std::string* error_out = nullptr);

	// Validates a username+password pair via LogonUserW — true only
	// when the credentials are correct. No session is created.
	static bool VerifyWindowsPassword(const std::string& username,
					  const std::string& password,
					  std::string* error_out = nullptr);

	static bool IsAdministrator();

	// SecurityFlashHello login mode per drive. enable=true verifies the
	// passphrase and writes the sealed key (flash-only); enable=false
	// removes it (flash + PIN).
	static bool SetFlashOnlyLogin(const std::string& drive_root,
				      const std::string& passphrase,
				      bool enable,
				      std::string* error_out = nullptr);
	static bool IsFlashOnlyLogin(const std::string& drive_root);

	// Decrypts the token for sign-in: uses the sealed key when present,
	// otherwise `pin` as the passphrase.
	static bool ReadLoginToken(const std::string& drive_root,
				   const std::string& pin,
				   TokenPayload* payload_out,
				   std::string* error_out = nullptr);

	// Full cycle: erase token + delete account
	static DeprovisionResult Deprovision(const DeprovisionOptions& options);

	// Utilities
	static std::string GeneratePassword(std::size_t length = 20,
					    std::string* error_out = nullptr);
	static std::string GenerateUsername(std::string* error_out = nullptr);
	static void SecureClear(std::string& value);
};

}  // namespace XER::SecurityFlash
