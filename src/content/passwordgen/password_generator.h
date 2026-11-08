#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace XER::Content::PasswordGen {

// Cryptographically secure password generator (CNG on Windows,
// std::random_device elsewhere). Supports character-class policies,
// ambiguity exclusion and guaranteed class coverage.

struct Policy {
	std::size_t length = 20;

	bool use_lower = true;
	bool use_upper = true;
	bool use_digits = true;
	bool use_symbols = true;

	// Exclude look-alike characters: l 1 I O 0 (and | ` ' when symbols).
	bool exclude_ambiguous = true;

	// Guarantee at least one character per enabled class.
	bool guaranteed_classes = true;

	// Overrides the default symbol set when non-empty.
	std::string custom_symbols;
};

struct Result {
	bool ok = false;
	std::string password;
	std::string error;
};

class PasswordGenerator {
public:
	// Generates a password under `policy`. Never throws.
	static Result Generate(const Policy& policy);

	// Convenience: default policy, `length` characters.
	static Result Generate(std::size_t length = 20);

	// Named presets for UI pickers: "pin" is not offered (too weak);
	// presets are (name, policy) pairs.
	struct Preset {
		std::string name;
		Policy policy;
	};
	static const std::vector<Preset>& Presets();

	// Heuristic strength check: length + class diversity.
	static bool IsStrongEnough(std::string_view password,
				   std::size_t min_length = 12);
};

}  // namespace XER::Content::PasswordGen
