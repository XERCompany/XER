#include "helper/buffer.h"

#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <algorithm>
#include <iterator>

#include <range/v3/algorithm/copy.hpp>
#include <range/v3/algorithm/equal.hpp>
#include <range/v3/algorithm/lexicographical_compare.hpp>
#include <range/v3/algorithm/search.hpp>
 
#include <unicode/normalizer2.h>
#include <unicode/unistr.h> 

namespace Engine::Helper {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

int HexNibble(char ch) {
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	}
	if (ch >= 'a' && ch <= 'f') {
		return 10 + (ch - 'a');
	}
	return -1;
}

std::string StripAsciiWhitespace(std::string_view text) {
	std::string compact;
	compact.reserve(text.size());
	for (char ch : text) {
		if (!absl::ascii_isspace(static_cast<unsigned char>(ch))) {
			compact.push_back(ch);
		}
	}
	return compact;
}

}  // namespace

Buffer::Bytes Buffer::FromString(std::string_view text, bool normalize_utf8) {
	Bytes out;
	AppendString(&out, text, normalize_utf8);
	return out;
}

std::string Buffer::ToString(ConstSpan bytes, bool normalize_utf8) {
	std::string out(bytes.begin(), bytes.end());
	return normalize_utf8 ? NormalizeUtf8(out) : out;
}

std::string Buffer::NormalizeUtf8(std::string_view text) {
	std::string normalized(text); 
	UErrorCode status = U_ZERO_ERROR;
	const icu::Normalizer2* normalizer = icu::Normalizer2::getNFCInstance(status);
	if (U_FAILURE(status) || normalizer == nullptr) {
		return normalized;
	}
	icu::UnicodeString source = icu::UnicodeString::fromUTF8(text);
	icu::UnicodeString output;
	status = U_ZERO_ERROR;
	normalizer->normalize(source, output, status);
	if (U_FAILURE(status)) {
		return normalized;
	}
	normalized.clear();
	output.toUTF8String(normalized); 
	return normalized;
}

void Buffer::AppendString(Bytes* target, std::string_view text, bool normalize_utf8) {
	if (target == nullptr || text.empty()) {
		return;
	}
	const std::string source = normalize_utf8 ? NormalizeUtf8(text) : std::string(text);
	target->reserve(target->size() + source.size());
	AppendBytes(target, ConstSpan(reinterpret_cast<const Byte*>(source.data()), source.size()));
}

void Buffer::AppendBytes(Bytes* target, ConstSpan bytes) {
	if (target == nullptr || bytes.empty()) {
		return;
	}
	target->reserve(target->size() + bytes.size());
	ranges::copy(bytes, std::back_inserter(*target));
}

std::string Buffer::HexEncode(ConstSpan bytes) {
	std::string hex;
	hex.reserve(bytes.size() * 2u);
	for (Byte value : bytes) {
		hex.push_back(kHexDigits[(value >> 4u) & 0x0Fu]);
		hex.push_back(kHexDigits[value & 0x0Fu]);
	}
	return hex;
}

bool Buffer::HexDecode(std::string_view hex, Bytes* bytes_out, std::string* error_out) {
	if (bytes_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "hex decode target is null";
		}
		return false;
	}
	std::string compact = StripAsciiWhitespace(hex);
	absl::AsciiStrToLower(&compact);
	if ((compact.size() % 2u) != 0u) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("hex input must contain an even number of digits: ", compact.size());
		}
		return false;
	}

	Bytes out;
	out.reserve(compact.size() / 2u);
	for (size_t index = 0; index < compact.size(); index += 2u) {
		const int high = HexNibble(compact[index]);
		const int low = HexNibble(compact[index + 1u]);
		if (high < 0 || low < 0) {
			if (error_out != nullptr) {
				*error_out = absl::StrCat("invalid hex digit at index ", index);
			}
			return false;
		}
		out.push_back(static_cast<Byte>((high << 4u) | low));
	}

	*bytes_out = std::move(out);
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

Buffer::SizeType Buffer::Copy(ConstSpan source,
				      MutableSpan destination,
				      SizeType source_offset,
				      SizeType destination_offset,
				      SizeType max_length) {
	if (source_offset >= source.size() || destination_offset >= destination.size()) {
		return 0;
	}
	ConstSpan source_tail = source.subspan(source_offset);
	MutableSpan destination_tail = destination.subspan(destination_offset);
	const SizeType count = std::min({max_length, source_tail.size(), destination_tail.size()});
	if (count == 0) {
		return 0;
	}
	ranges::copy(source_tail.first(count), destination_tail.begin());
	return count;
}

Buffer::Bytes Buffer::Concat(const std::vector<ConstSpan>& segments) {
	SizeType total_size = 0;
	for (ConstSpan segment : segments) {
		total_size += segment.size();
	}
	Bytes out;
	out.reserve(total_size);
	for (ConstSpan segment : segments) {
		AppendBytes(&out, segment);
	}
	return out;
}

int Buffer::Compare(ConstSpan lhs, ConstSpan rhs) {
	if (ranges::equal(lhs, rhs)) {
		return 0;
	}
	return ranges::lexicographical_compare(lhs, rhs) ? -1 : 1;
}

bool Buffer::Equals(ConstSpan lhs, ConstSpan rhs) {
	return ranges::equal(lhs, rhs);
}

std::ptrdiff_t Buffer::IndexOf(ConstSpan haystack, ConstSpan needle, SizeType offset) {
	if (offset > haystack.size()) {
		return -1;
	}
	if (needle.empty()) {
		return static_cast<std::ptrdiff_t>(offset);
	}
	ConstSpan window = haystack.subspan(offset);
	const auto match = ranges::search(window, needle);
	if (match.begin() == window.end()) {
		return -1;
	}
	return static_cast<std::ptrdiff_t>(offset) + (match.begin() - window.begin());
}

std::ptrdiff_t Buffer::LastIndexOf(ConstSpan haystack, ConstSpan needle, SizeType offset) {
	const SizeType clamped_offset = std::min(offset, haystack.size());
	if (needle.empty()) {
		return static_cast<std::ptrdiff_t>(clamped_offset);
	}
	if (needle.size() > haystack.size()) {
		return -1;
	}
	SizeType start = std::min(clamped_offset, haystack.size() - needle.size());
	for (;;) {
		if (ranges::equal(haystack.subspan(start, needle.size()), needle)) {
			return static_cast<std::ptrdiff_t>(start);
		}
		if (start == 0) {
			break;
		}
		--start;
	}
	return -1;
}

Buffer::SizeType Buffer::Fill(MutableSpan destination,
				       ConstSpan pattern,
				       SizeType offset,
				       SizeType end) {
	if (pattern.empty() || offset >= destination.size()) {
		return 0;
	}
	const SizeType clamped_end = std::min(end, destination.size());
	if (offset >= clamped_end) {
		return 0;
	}
	SizeType written = 0;
	for (SizeType index = offset; index < clamped_end; ++index) {
		destination[index] = pattern[written % pattern.size()];
		++written;
	}
	return written;
}

Buffer::Bytes Buffer::Slice(ConstSpan source, SizeType start, SizeType end) {
	const SizeType clamped_start = std::min(start, source.size());
	const SizeType clamped_end = std::min(end, source.size());
	if (clamped_start >= clamped_end) {
		return Bytes();
	}
	return Bytes(source.begin() + static_cast<std::ptrdiff_t>(clamped_start),
		     source.begin() + static_cast<std::ptrdiff_t>(clamped_end));
}

namespace {

bool SwapChunks(Buffer::MutableSpan bytes, std::size_t width, std::string* error_out) {
	if ((bytes.size() % width) != 0u) {
		if (error_out != nullptr) {
			*error_out = absl::StrFormat("buffer size %d must be a multiple of %d",
				static_cast<int>(bytes.size()),
				static_cast<int>(width));
		}
		return false;
	}
	for (std::size_t chunk = 0; chunk < bytes.size(); chunk += width) {
		for (std::size_t index = 0; index < width / 2u; ++index) {
			std::swap(bytes[chunk + index], bytes[chunk + width - 1u - index]);
		}
	}
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

}  // namespace

bool Buffer::Swap16(MutableSpan bytes, std::string* error_out) {
	return SwapChunks(bytes, 2u, error_out);
}

bool Buffer::Swap32(MutableSpan bytes, std::string* error_out) {
	return SwapChunks(bytes, 4u, error_out);
}

bool Buffer::Swap64(MutableSpan bytes, std::string* error_out) {
	return SwapChunks(bytes, 8u, error_out);
}

}  // namespace Engine::Helper
