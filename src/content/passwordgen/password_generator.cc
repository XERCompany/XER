#include "password_generator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <random>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace XER::Content::PasswordGen {
namespace {

constexpr std::string_view kLower = "abcdefghijklmnopqrstuvwxyz";
constexpr std::string_view kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::string_view kDigits = "0123456789";
constexpr std::string_view kSymbols = "!@#$%^&*()-_=+[]{};:,.<>?/~";
constexpr std::string_view kAmbiguous = "l1IO0|`'\"";

// Uniform random index in [0, n) via rejection sampling (no modulo bias).
bool RandIndex(std::size_t n, std::size_t* out) {
	if (n == 0) return false;
#ifdef _WIN32
	std::uint64_t r = 0;
	if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&r),
	                    sizeof(r), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
		return false;
	}
#else
	static thread_local std::mt19937_64 rng{std::random_device{}()};
	const std::uint64_t r = rng();
#endif
	const std::uint64_t limit =
	    ((std::numeric_limits<std::uint64_t>::max)() / n) * n;
	if (r >= limit) {
		// Extremely rare: retry once with a fresh draw.
		return RandIndex(n, out);
	}
	*out = static_cast<std::size_t>(r % n);
	return true;
}

char PickChar(std::string_view set, bool* ok) {
	std::size_t i = 0;
	if (!RandIndex(set.size(), &i)) {
		*ok = false;
		return '\0';
	}
	return set[i];
}

std::string BuildClassSet(const Policy& p, std::string_view base,
                          bool enabled) {
	if (!enabled) return {};
	std::string set(base);
	if (p.exclude_ambiguous) {
		set.erase(std::remove_if(set.begin(), set.end(),
		                         [](char c) {
			                         return kAmbiguous.find(c) !=
			                                std::string_view::npos;
		                         }),
		          set.end());
	}
	return set;
}

}  // namespace

Result PasswordGenerator::Generate(const Policy& policy) {
	Result res;
	if (policy.length < 4) {
		res.error = "password length must be >= 4";
		return res;
	}

	const std::string lower = BuildClassSet(policy, kLower, policy.use_lower);
	const std::string upper = BuildClassSet(policy, kUpper, policy.use_upper);
	const std::string digits =
	    BuildClassSet(policy, kDigits, policy.use_digits);
	std::string symbols;
	if (policy.use_symbols) {
		symbols = policy.custom_symbols.empty()
		              ? BuildClassSet(policy, kSymbols, true)
		              : policy.custom_symbols;
		if (policy.exclude_ambiguous) {
			symbols.erase(std::remove_if(
			    symbols.begin(), symbols.end(),
			    [](char c) {
				    return kAmbiguous.find(c) !=
				           std::string_view::npos;
			    }),
			    symbols.end());
		}
	}

	std::string all;
	all.reserve(lower.size() + upper.size() + digits.size() +
	            symbols.size());
	all += lower;
	all += upper;
	all += digits;
	all += symbols;
	if (all.empty()) {
		res.error = "policy enables no character classes";
		return res;
	}

	const std::array<std::string, 4> classes = {lower, upper, digits,
	                                            symbols};
	std::size_t enabled = 0;
	for (const auto& c : classes) enabled += !c.empty();
	if (policy.guaranteed_classes && policy.length < enabled) {
		res.error = "length smaller than enabled class count";
		return res;
	}

	bool ok = true;
	res.password.reserve(policy.length);

	// Guaranteed coverage: one char per enabled class first.
	if (policy.guaranteed_classes) {
		for (const auto& cls : classes) {
			if (cls.empty()) continue;
			res.password.push_back(PickChar(cls, &ok));
		}
	}
	while (res.password.size() < policy.length) {
		res.password.push_back(PickChar(all, &ok));
	}
	if (!ok) {
		res.password.clear();
		res.error = "cryptographic RNG failed";
		return res;
	}

	// Fisher-Yates shuffle so guaranteed chars are not positional.
	for (std::size_t i = res.password.size() - 1; i > 0; --i) {
		std::size_t j = 0;
		if (!RandIndex(i + 1, &j)) {
			res.password.clear();
			res.error = "cryptographic RNG failed";
			return res;
		}
		std::swap(res.password[i], res.password[static_cast<std::size_t>(j)]);
	}

	res.ok = true;
	return res;
}

Result PasswordGenerator::Generate(std::size_t length) {
	Policy p;
	p.length = length;
	return Generate(p);
}

const std::vector<PasswordGenerator::Preset>& PasswordGenerator::Presets() {
	static const std::vector<Preset> presets = [] {
		std::vector<Preset> v;
		Policy strong;
		strong.length = 20;
		v.push_back({"strong (20)", strong});

		Policy mem;
		mem.length = 12;
		mem.use_symbols = false;
		v.push_back({"memorable (12, no symbols)", mem});

		Policy pin_like;
		pin_like.length = 8;
		pin_like.use_upper = false;
		pin_like.use_symbols = false;
		v.push_back({"short digits+lower (8)", pin_like});
		return v;
	}();
	return presets;
}

bool PasswordGenerator::IsStrongEnough(std::string_view password,
				       std::size_t min_length) {
	if (password.size() < min_length) return false;
	bool has_lower = false, has_upper = false, has_digit = false,
	     has_other = false;
	for (const char c : password) {
		has_lower |= (c >= 'a' && c <= 'z');
		has_upper |= (c >= 'A' && c <= 'Z');
		has_digit |= (c >= '0' && c <= '9');
		has_other |= !(std::isalnum(static_cast<unsigned char>(c)));
	}
	const int classes =
	    static_cast<int>(has_lower) + static_cast<int>(has_upper) +
	    static_cast<int>(has_digit) + static_cast<int>(has_other);
	return classes >= 3;
}

}  // namespace XER::Content::PasswordGen
