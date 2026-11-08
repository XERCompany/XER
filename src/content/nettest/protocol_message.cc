#include "protocol_message.h"

#include <algorithm>
#include <cctype>

#include <re2/re2.h>
#include <zlib.h>

namespace XER::Content::NetTest {
namespace {

const re2::RE2 kNamePattern(R"(^[A-Z][A-Z0-9_]{0,31}(\.z)?$)");
const re2::RE2 kFieldNamePattern(R"(^[a-z][a-z0-9_]{0,31}$)");

std::string EscapeFieldValue(const std::string& v) {
	bool needs_quote = v.find_first_of(",|=\"\\") != std::string::npos;
	if (!needs_quote) return v;
	std::string out = "\"";
	for (char c : v) {
		if (c == '"' || c == '\\') out += '\\';
		out += c;
	}
	out += '"';
	return out;
}

bool UnescapeFieldValue(const std::string& raw, std::string* out) {
	out->clear();
	if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
		for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
			if (raw[i] == '\\' && i + 2 < raw.size()) {
				out->push_back(raw[i + 1]);
				++i;
			} else {
				out->push_back(raw[i]);
			}
		}
		return true;
	}
	*out = raw;
	return true;
}

}  // namespace

void ProtocolMessage::SetField(const std::string& field_name,
			       const std::string& value, FieldType type) {
	for (auto& f : fields) {
		if (f.name == field_name) {
			f.value = value;
			f.type = type;
			return;
		}
	}
	fields.push_back({field_name, value, type});
}

const std::string* ProtocolMessage::GetField(const std::string& field_name) const {
	for (const auto& f : fields) {
		if (f.name == field_name) return &f.value;
	}
	return nullptr;
}

bool ProtocolMessage::RemoveField(const std::string& field_name) {
	const auto it = std::remove_if(fields.begin(), fields.end(),
				       [&field_name](const MessageField& f) {
					       return f.name == field_name;
				       });
	if (it == fields.end()) return false;
	fields.erase(it, fields.end());
	return true;
}

std::string ProtocolMessage::Serialize() const {
	std::string out = name;
	if (compressed) out += ".z";

	// Always emit both separators: NAME[.z]|fields|payload
	out += '|';
	for (std::size_t i = 0; i < fields.size(); ++i) {
		if (i) out += ',';
		out += fields[i].name;
		out += '=';
		out += EscapeFieldValue(fields[i].value);
	}

	out += '|';
	if (!payload.empty()) {
		std::vector<std::uint8_t> wire = payload;
		if (compressed) {
			std::vector<std::uint8_t> packed;
			if (CodecUtil::ZlibCompress(payload.data(), payload.size(), &packed)) {
				wire = std::move(packed);
			} else {
				// Compression failed: emit uncompressed without the .z mark.
				out.resize(out.size() - 2);
			}
		}
		out += CodecUtil::Base64Encode(wire.data(), wire.size());
	}
	return out;
}

bool ProtocolMessage::Parse(const std::string& line,
			    std::string* error_out) {
	auto fail = [&](const std::string& msg) {
		if (error_out) *error_out = msg;
		return false;
	};

	// Split into 3 sections on '|'. Field values may contain '|' inside
	// quotes, but the base64 payload never does — so the payload starts
	// after the LAST '|' and fields live between the first and last.
	const std::size_t p1 = line.find('|');
	const std::size_t p2 = p1 == std::string::npos ? p1 : line.rfind('|');
	if (p1 == std::string::npos || p2 <= p1) {
		return fail("missing field/payload separators");
	}

	std::string head = line.substr(0, p1);
	const std::string fields_part = line.substr(p1 + 1, p2 - p1 - 1);
	const std::string payload_part = line.substr(p2 + 1);

	compressed = false;
	if (head.size() > 2 && head.compare(head.size() - 2, 2, ".z") == 0) {
		compressed = true;
		head.resize(head.size() - 2);
	}
	name = head;
	if (!IsValidName(name)) return fail("invalid message name: " + name);

	fields.clear();
	if (!fields_part.empty()) {
		// Split on ',' outside quotes; '\"' and '\\' escape inside quotes.
		std::size_t start = 0;
		bool in_quote = false;
		for (std::size_t i = 0; i <= fields_part.size(); ++i) {
			const bool at_end = i == fields_part.size();
			const char c = at_end ? '\0' : fields_part[i];
			if (!at_end) {
				if (c == '\\' && in_quote) { ++i; continue; }
				if (c == '"') in_quote = !in_quote;
			}
			if (at_end || (c == ',' && !in_quote)) {
				const std::string kv = fields_part.substr(start, i - start);
				start = i + 1;

				std::size_t eq = std::string::npos;
				bool q = false;
				for (std::size_t j = 0; j < kv.size(); ++j) {
					if (kv[j] == '\\' && q) { ++j; continue; }
					if (kv[j] == '"') q = !q;
					if (kv[j] == '=' && !q) { eq = j; break; }
				}
				if (eq == std::string::npos) {
					return fail("bad field (no '='): " + kv);
				}
				const std::string fname = kv.substr(0, eq);
				if (!re2::RE2::FullMatch(fname, kFieldNamePattern)) {
					return fail("invalid field name: " + fname);
				}
				MessageField f;
				f.name = fname;
				UnescapeFieldValue(kv.substr(eq + 1), &f.value);
				fields.push_back(std::move(f));
			}
		}
	}

	payload.clear();
	if (!payload_part.empty()) {
		std::vector<std::uint8_t> raw;
		if (!CodecUtil::Base64Decode(payload_part, &raw)) {
			return fail("invalid base64 payload");
		}
		if (compressed) {
			if (!CodecUtil::ZlibDecompress(raw.data(), raw.size(), &payload)) {
				return fail("zlib decompress failed");
			}
		} else {
			payload = std::move(raw);
		}
	}
	return true;
}

bool ProtocolMessage::IsValidName(const std::string& name) {
	return re2::RE2::FullMatch(name, kNamePattern);
}

// --- CodecUtil ---------------------------------------------------------

namespace {

constexpr char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string CodecUtil::Base64Encode(const std::uint8_t* data, std::size_t size) {
	std::string out;
	out.reserve(((size + 2) / 3) * 4);
	for (std::size_t i = 0; i < size; i += 3) {
		const std::uint32_t b0 = data[i];
		const std::uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
		const std::uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
		const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
		out += kB64[(triple >> 18) & 63];
		out += kB64[(triple >> 12) & 63];
		out += i + 1 < size ? kB64[(triple >> 6) & 63] : '=';
		out += i + 2 < size ? kB64[triple & 63] : '=';
	}
	return out;
}

bool CodecUtil::Base64Decode(const std::string& text,
			     std::vector<std::uint8_t>* out) {
	auto val = [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '+') return 62;
		if (c == '/') return 63;
		return -1;
	};
	out->clear();
	out->reserve((text.size() / 4) * 3);
	std::uint32_t acc = 0;
	int bits = 0;
	for (char c : text) {
		if (c == '=') break;
		const int v = val(c);
		if (v < 0) {
			if (std::isspace(static_cast<unsigned char>(c))) continue;
			return false;
		}
		acc = (acc << 6) | static_cast<std::uint32_t>(v);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out->push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
		}
	}
	return true;
}

bool CodecUtil::ZlibCompress(const std::uint8_t* data, std::size_t size,
			     std::vector<std::uint8_t>* out) {
	if (!data && size != 0) return false;
	uLongf bound = compressBound(static_cast<uLong>(size));
	std::vector<std::uint8_t> buf(bound + 4);
	// 4-byte LE uncompressed size prefix.
	const std::uint32_t sz = static_cast<std::uint32_t>(size);
	buf[0] = static_cast<std::uint8_t>(sz & 0xFF);
	buf[1] = static_cast<std::uint8_t>((sz >> 8) & 0xFF);
	buf[2] = static_cast<std::uint8_t>((sz >> 16) & 0xFF);
	buf[3] = static_cast<std::uint8_t>((sz >> 24) & 0xFF);
	uLongf dest_len = bound;
	if (compress2(buf.data() + 4, &dest_len, data,
		      static_cast<uLong>(size), Z_BEST_SPEED) != Z_OK) {
		return false;
	}
	buf.resize(4 + dest_len);
	*out = std::move(buf);
	return true;
}

bool CodecUtil::ZlibDecompress(const std::uint8_t* data, std::size_t size,
			       std::vector<std::uint8_t>* out) {
	if (size < 4) return false;
	const std::uint32_t expected =
	    static_cast<std::uint32_t>(data[0]) |
	    (static_cast<std::uint32_t>(data[1]) << 8) |
	    (static_cast<std::uint32_t>(data[2]) << 16) |
	    (static_cast<std::uint32_t>(data[3]) << 24);
	// Sanity cap: 256 MiB.
	if (expected > 256u * 1024u * 1024u) return false;
	std::vector<std::uint8_t> buf(expected);
	uLongf dest_len = expected;
	if (uncompress(buf.data(), &dest_len, data + 4,
		       static_cast<uLong>(size - 4)) != Z_OK ||
	    dest_len != expected) {
		return false;
	}
	buf.resize(dest_len);
	*out = std::move(buf);
	return true;
}

}  // namespace XER::Content::NetTest
