// Smoke test for XER::SecurityFlash — exercises the portable token
// container (AES-256-GCM + PBKDF2), password/username generators and
// drive enumeration. Windows account creation is NOT tested here —
// it requires an elevated process and a real machine change.

#include "security_flash/security_flash.h"
#include "security_flash/removable_media.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* what,
	   const std::string& detail = std::string()) {
	if (!condition) {
		std::cerr << "FAIL: " << what;
		if (!detail.empty()) {
			std::cerr << " — " << detail;
		}
		std::cerr << '\n';
		++failures;
	}
}

std::filesystem::path MakeTempDir() {
	const auto base = std::filesystem::temp_directory_path()
		/ "xer_security_flash_smoke";
	std::filesystem::remove_all(base);
	std::filesystem::create_directories(base);
	return base;
}

bool ReadFileBytes(const std::filesystem::path& path,
		   std::vector<char>* bytes) {
	std::ifstream input(path, std::ios::binary);
	if (!input.is_open()) {
		return false;
	}
	bytes->assign(std::istreambuf_iterator<char>(input),
		      std::istreambuf_iterator<char>());
	return true;
}

}  // namespace

int main() {
	using namespace XER::SecurityFlash;

	const std::filesystem::path dir = MakeTempDir();
	const std::string dir_str = dir.string();
	std::string error;

	// --- Token roundtrip (unbound) ---
	TokenPayload payload;
	payload.username = "smoke_user";
	payload.password = "s3cret!P@ssw0rd";
	payload.created_utc = "2026-11-08T00:00:00Z";
	payload.host_name = "smoke-host";
	payload.volume_serial = 0;

	Check(SecurityFlash::WriteToken(dir_str, payload, "test-passphrase", &error), "WriteToken unbound", error);
	Check(SecurityFlash::TokenExists(dir_str), "TokenExists after write");

	TokenPayload read_back;
	Check(SecurityFlash::ReadToken(dir_str, "test-passphrase", &read_back, &error), "ReadToken unbound", error);
	Check(read_back.username == payload.username, "username roundtrip");
	Check(read_back.password == payload.password, "password roundtrip");
	Check(read_back.host_name == payload.host_name, "host roundtrip");
	Check(read_back.volume_serial == 0, "unbound serial");

	// The password must not appear in the raw file bytes.
	std::vector<char> raw;
	Check(ReadFileBytes(dir / kTokenFileName, &raw), "read raw token file");
	const std::string raw_text(raw.begin(), raw.end());
	Check(raw_text.find(payload.password) == std::string::npos,
	      "password not stored in plaintext");
	Check(raw_text.find("smoke_user") == std::string::npos,
	      "username not stored in plaintext");

	// Wrong passphrase must fail.
	TokenPayload wrong;
	Check(!SecurityFlash::ReadToken(dir_str, "wrong-passphrase", &wrong, &error),
	      "ReadToken rejects wrong passphrase");

	// Unicode password roundtrip.
	payload.password = "пароль🔒!";
	Check(SecurityFlash::WriteToken(dir_str, payload, "test-passphrase", &error), "WriteToken unicode", error);
	Check(SecurityFlash::ReadToken(dir_str, "test-passphrase", &read_back, &error)
	      && read_back.password == payload.password,
	      "unicode password roundtrip");

	// --- Volume binding ---
	std::uint32_t serial = 0;
	Check(Detail::VolumeSerialForPath(dir_str, &serial, &error),
	      "VolumeSerialForPath on temp dir");
	payload.volume_serial = serial;
	Check(SecurityFlash::WriteToken(dir_str, payload, "test-passphrase", &error), "WriteToken bound", error);
	Check(SecurityFlash::ReadToken(dir_str, "test-passphrase", &read_back, &error)
	      && read_back.volume_serial == serial,
	      "bound token reads on same volume");

	payload.volume_serial = serial + 1;  // bind to a different volume
	Check(SecurityFlash::WriteToken(dir_str, payload, "test-passphrase", &error), "WriteToken foreign binding", error);
	Check(!SecurityFlash::ReadToken(dir_str, "test-passphrase", &read_back, &error), "bound token rejects foreign volume", error);

	payload.volume_serial = 0;
	Check(SecurityFlash::WriteToken(dir_str, payload, "test-passphrase", &error), "WriteToken restore unbound", error);

	// --- Erase ---
	Check(SecurityFlash::EraseToken(dir_str, &error), "EraseToken");
	Check(!SecurityFlash::TokenExists(dir_str), "TokenExists after erase");
	Check(!SecurityFlash::EraseToken(dir_str, &error), "EraseToken strict on missing");

	// --- Generators ---
	std::string generated = SecurityFlash::GeneratePassword(20, &error);
	Check(generated.size() == 20, "generated password length");
	bool has_upper = false, has_lower = false, has_digit = false,
	     has_symbol = false;
	for (char ch : generated) {
		has_upper |= (ch >= 'A' && ch <= 'Z');
		has_lower |= (ch >= 'a' && ch <= 'z');
		has_digit |= (ch >= '0' && ch <= '9');
		has_symbol |= !(std::isalnum(static_cast<unsigned char>(ch)));
	}
	Check(has_upper && has_lower && has_digit && has_symbol,
	      "generated password has all classes");
	Check(SecurityFlash::GeneratePassword(5, &error).empty(),
	      "GeneratePassword rejects short length");

	// Two generated passwords must differ (RNG sanity).
	std::string another = SecurityFlash::GeneratePassword(20, &error);
	Check(generated != another, "generated passwords differ");
	SecurityFlash::SecureClear(generated);
	SecurityFlash::SecureClear(another);

	Check(SecurityFlash::IsValidUsername("user_1"), "valid username");
	Check(SecurityFlash::IsValidUsername("XER.Admin-01"), "valid username dotted");
	Check(!SecurityFlash::IsValidUsername(".bad"), "reject leading dot");
	Check(!SecurityFlash::IsValidUsername("bad."), "reject trailing dot");
	Check(!SecurityFlash::IsValidUsername("has space"), "reject space");
	Check(!SecurityFlash::IsValidUsername("way_too_long_username_xx"), "reject >20 chars");
	Check(!SecurityFlash::IsValidUsername(""), "reject empty");

	// --- Drive enumeration (informational, must not crash) ---
	const auto drives = SecurityFlash::EnumerateDrives(true);
	std::cout << "removable drives found: " << drives.size() << '\n';
	for (const DriveInfo& drive : drives) {
		std::cout << "  " << drive.root_path
			  << " label='" << drive.volume_label << "'"
			  << " serial=0x" << std::hex << drive.volume_serial
			  << std::dec << " ready=" << drive.ready << '\n';
	}

	std::filesystem::remove_all(dir);

	if (failures == 0) {
		std::cout << "security_flash smoke: OK\n";
		return 0;
	}
	std::cerr << "security_flash smoke: " << failures << " failure(s)\n";
	return 1;
}
