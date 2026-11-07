#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace XER::Engine::Helper {

class Buffer {
public:
	using Byte = std::uint8_t;
	using Bytes = std::vector<Byte>;
	using MutableSpan = std::span<Byte>;
	using ConstSpan = std::span<const Byte>;
	using SizeType = Bytes::size_type;

	static Bytes FromString(std::string_view text, bool normalize_utf8 = false);
	static std::string ToString(ConstSpan bytes, bool normalize_utf8 = false);
	static std::string NormalizeUtf8(std::string_view text);

	static void AppendString(Bytes* target, std::string_view text, bool normalize_utf8 = false);
	static void AppendBytes(Bytes* target, ConstSpan bytes);

	static std::string HexEncode(ConstSpan bytes);
	static bool HexDecode(std::string_view hex, Bytes* bytes_out, std::string* error_out = nullptr);

	static SizeType Copy(ConstSpan source,
			     MutableSpan destination,
			     SizeType source_offset = 0,
			     SizeType destination_offset = 0,
			     SizeType max_length = std::numeric_limits<SizeType>::max());

	static Bytes Concat(const std::vector<ConstSpan>& segments);
	static int Compare(ConstSpan lhs, ConstSpan rhs);
	static bool Equals(ConstSpan lhs, ConstSpan rhs);
	static std::ptrdiff_t IndexOf(ConstSpan haystack, ConstSpan needle, SizeType offset = 0);
	static std::ptrdiff_t LastIndexOf(ConstSpan haystack,
				     ConstSpan needle,
				     SizeType offset = std::numeric_limits<SizeType>::max());
	static SizeType Fill(MutableSpan destination,
			     ConstSpan pattern,
			     SizeType offset = 0,
			     SizeType end = std::numeric_limits<SizeType>::max());
	static Bytes Slice(ConstSpan source,
			   SizeType start = 0,
			   SizeType end = std::numeric_limits<SizeType>::max());
	static bool Swap16(MutableSpan bytes, std::string* error_out = nullptr);
	static bool Swap32(MutableSpan bytes, std::string* error_out = nullptr);
	static bool Swap64(MutableSpan bytes, std::string* error_out = nullptr);
};

}  // namespace XER::Engine::Helper

using Buffer = XER::Engine::Helper::Buffer;
