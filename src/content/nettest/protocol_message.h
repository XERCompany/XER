#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Content::NetTest {

// Line-based protocol frame:
//   NAME[.z]|field=value,field2="quoted, value"|<base64 payload>
//
// - NAME: [A-Z][A-Z0-9_]{0,31}; suffix ".z" marks a zlib-compressed payload
//   (4-byte LE uncompressed size precedes the zlib stream).
// - Fields are comma-separated name=value pairs; values may be
//   double-quoted to embed ',' '|' '=' characters (\" and \\ escapes).
// - The third section is standard base64 of the raw payload bytes.
//   An empty payload yields an empty third section.

enum class FieldType {
	kString,  // arbitrary text
	kInt,     // decimal integer
	kHex,     // 0x-prefixed or bare hex
	kBase64,  // base64 blob stored as text
};

struct MessageField {
	std::string name;
	std::string value;  // canonical text form
	FieldType type = FieldType::kString;
};

struct ProtocolMessage {
	std::string name;                // e.g. "AUTH", "PING", "DATA.z"
	bool compressed = false;         // payload zlib-compressed
	std::vector<MessageField> fields;
	std::vector<std::uint8_t> payload;

	void SetField(const std::string& field_name, const std::string& value,
		      FieldType type = FieldType::kString);
	const std::string* GetField(const std::string& field_name) const;
	bool RemoveField(const std::string& field_name);

	std::string Serialize() const;  // produces one line, no trailing '\n'
	// Parses one line; returns false + error string on malformed input.
	bool Parse(const std::string& line, std::string* error_out = nullptr);

	static bool IsValidName(const std::string& name);
};

// Stateless helpers shared by codec and client.
class CodecUtil {
public:
	static std::string Base64Encode(const std::uint8_t* data, std::size_t size);
	static bool Base64Decode(const std::string& text,
				 std::vector<std::uint8_t>* out);
	static bool ZlibCompress(const std::uint8_t* data, std::size_t size,
				 std::vector<std::uint8_t>* out);
	static bool ZlibDecompress(const std::uint8_t* data, std::size_t size,
				  std::vector<std::uint8_t>* out);
};

}  // namespace XER::Content::NetTest
