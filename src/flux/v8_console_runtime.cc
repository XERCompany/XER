#include "flux/v8_console_runtime.h"

#include "helper/js_buffer.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "flux/terminal/terminal_output_renderer.h"

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/transform.hpp>

namespace flux::console {
namespace {

std::string Indent(int level) {
	return std::string(static_cast<std::size_t>(std::max(level, 0) * 2), ' ');
}

std::string NormalizeDisplayText(const std::string& text) {
	return Engine::Helper::JsBuffer::NormalizeUtf8(text);
}

std::string HexByte(std::uint8_t byte) {
	return absl::StrFormat("%02x", byte);
}

std::vector<std::uint16_t> ReadUtf16Units(v8::Isolate* isolate, v8::Local<v8::String> string_value) {
	const int length = string_value->Length();
	std::vector<std::uint16_t> units(static_cast<std::size_t>(length));
	string_value->Write(
		isolate,
		units.data(),
		0,
		length,
		static_cast<int>(v8::String::WriteOptions::NO_NULL_TERMINATION));
	return units;
}

bool HasBinaryUnits(const std::vector<std::uint16_t>& units) {
	for (std::uint16_t unit : units) {
		if (unit > 0xFFu) {
			continue;
		}
		const std::uint8_t byte = static_cast<std::uint8_t>(unit & 0xFFu);
		if (byte < 0x20u || byte == 0x7Fu) {
			return true;
		}
	}
	return false;
}

std::string FormatStringForConsole(v8::Isolate* isolate,
				   v8::Local<v8::String> string_value,
				   bool top_level,
				   const FormatOptions& options) {
	const std::vector<std::uint16_t> units = ReadUtf16Units(isolate, string_value);
	if (!HasBinaryUnits(units)) {
		const std::string plain = NormalizeDisplayText(Utf8(isolate, string_value));
		if (top_level && options.top_level_plain_strings) {
			return plain;
		}
		return absl::StrCat("\"", plain, "\"");
	}

	std::string out = top_level ? "b\"" : "\"b:";
	for (std::uint16_t unit : units) {
		if (unit <= 0xFFu) {
			const std::uint8_t byte = static_cast<std::uint8_t>(unit & 0xFFu);
			if (byte >= 0x20u && byte != 0x7Fu && byte != '\\' && byte != '"') {
				out.push_back(static_cast<char>(byte));
			} else {
				absl::StrAppend(&out, "\\x", HexByte(byte));
			}
			continue;
		}
		absl::StrAppendFormat(&out, "\\u%04x", unit);
	}
	out.push_back('"');
	return out;
}

bool ReadUint8ArrayBytes(v8::Local<v8::Uint8Array> array, std::vector<std::uint8_t>* bytes_out) {
	if (bytes_out == nullptr) {
		return false;
	}
	bytes_out->resize(array->ByteLength());
	if (bytes_out->empty()) {
		return true;
	}
	array->CopyContents(bytes_out->data(), bytes_out->size());
	return true;
}

bool ReadArrayBufferBytes(v8::Local<v8::ArrayBuffer> buffer, std::vector<std::uint8_t>* bytes_out) {
	if (bytes_out == nullptr) {
		return false;
	}
	const std::shared_ptr<v8::BackingStore> backing = buffer->GetBackingStore();
	if (!backing) {
		bytes_out->clear();
		return true;
	}
	const auto* begin = static_cast<const std::uint8_t*>(backing->Data());
	bytes_out->assign(begin, begin + backing->ByteLength());
	return true;
}

std::string ReadConstructorName(v8::Isolate* isolate,
				v8::Local<v8::Context> context,
				v8::Local<v8::Object> object) {
	v8::Local<v8::Value> constructor_value;
	v8::Local<v8::Value> name_value;
	if (!object->Get(context, v8::String::NewFromUtf8Literal(isolate, "constructor")).ToLocal(&constructor_value)
		|| !constructor_value->IsObject()
		|| !constructor_value.As<v8::Object>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "name")).ToLocal(&name_value)) {
		return std::string();
	}
	return Utf8(isolate, name_value);
}

std::string FormatBytePreview(const std::vector<std::uint8_t>& bytes,
				      std::size_t max_items,
				      bool hex) {
	const std::size_t limit = std::min(bytes.size(), max_items);
	const auto token_range = ranges::views::iota(std::size_t{0}, limit)
		| ranges::views::transform([&](std::size_t index) {
			return hex
				? HexByte(bytes[index])
				: absl::StrCat(static_cast<int>(bytes[index]));
		});
	const auto tokens = ranges::to<std::vector<std::string>>(token_range);

	std::string out;
	for (std::size_t index = 0; index < tokens.size(); ++index) {
		if (index > 0) {
			absl::StrAppend(&out, " ");
		}
		absl::StrAppend(&out, tokens[index]);
	}
	if (limit < bytes.size()) {
		absl::StrAppendFormat(&out, " ... %d more", static_cast<int>(bytes.size() - limit));
	}
	return out;
}

std::string FormatTypedArray(v8::Isolate* isolate,
				     v8::Local<v8::Context> context,
				     v8::Local<v8::Value> value,
				     const FormatOptions& options) {
	v8::Local<v8::Object> object = value.As<v8::Object>();
	const std::string ctor_name = ReadConstructorName(isolate, context, object);
	std::vector<std::uint8_t> bytes;
	if (!ReadUint8ArrayBytes(value.As<v8::Uint8Array>(), &bytes)) {
		return "[Uint8Array]";
	}
	if (ctor_name == "Buffer") {
		return absl::StrCat("<Buffer ", FormatBytePreview(bytes, options.max_collection_entries, true), ">");
	}
	return absl::StrCat(
		ctor_name.empty() ? "Uint8Array" : ctor_name,
		"(",
		bytes.size(),
		") [ ",
		FormatBytePreview(bytes, options.max_collection_entries, false),
		" ]");
}

std::string FormatArrayBuffer(v8::Local<v8::ArrayBuffer> buffer) {
	return absl::StrCat("ArrayBuffer(", buffer->ByteLength(), ")");
}

std::string FormatValueImpl(v8::Isolate* isolate,
			    v8::Local<v8::Context> context,
			    v8::Local<v8::Value> value,
			    int depth,
			    int indent,
			    bool top_level,
			    std::unordered_set<int>* seen_hashes,
			    const FormatOptions& options);

std::string FormatArray(v8::Isolate* isolate,
			v8::Local<v8::Context> context,
			v8::Local<v8::Array> array,
			int depth,
			int indent,
			std::unordered_set<int>* seen_hashes,
			const FormatOptions& options) {
	const std::uint32_t length = array->Length();
	if (length == 0) {
		return "[]";
	}
	std::string out = "[\n";
	const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(length, options.max_collection_entries));
	for (std::uint32_t index : ranges::views::iota(std::uint32_t{0}, limit)) {
		if (index > 0) {
			absl::StrAppend(&out, ",\n");
		}
		v8::Local<v8::Value> element;
		if (!array->Get(context, index).ToLocal(&element)) {
			absl::StrAppend(&out, Indent(indent + 1), "<error>");
			continue;
		}
		absl::StrAppend(
			&out,
			Indent(indent + 1),
			FormatValueImpl(isolate, context, element, depth - 1, indent + 1, false, seen_hashes, options));
	}
	if (limit < length) {
		absl::StrAppendFormat(&out, ",\n%s... %d more", Indent(indent + 1), length - limit);
	}
	absl::StrAppend(&out, "\n", Indent(indent), "]");
	return out;
}

std::string FormatObject(v8::Isolate* isolate,
			 v8::Local<v8::Context> context,
			 v8::Local<v8::Object> object,
			 int depth,
			 int indent,
			 std::unordered_set<int>* seen_hashes,
			 const FormatOptions& options) {
	v8::Local<v8::Array> keys;
	if (!object->GetOwnPropertyNames(context).ToLocal(&keys) || keys->Length() == 0) {
		return "{}";
	}
	std::string out = "{\n";
	const std::uint32_t length = keys->Length();
	const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(length, options.max_collection_entries));
	for (std::uint32_t index : ranges::views::iota(std::uint32_t{0}, limit)) {
		if (index > 0) {
			absl::StrAppend(&out, ",\n");
		}
		v8::Local<v8::Value> key;
		v8::Local<v8::Value> prop_value;
		if (!keys->Get(context, index).ToLocal(&key)) {
			absl::StrAppend(&out, Indent(indent + 1), "<key-error>: <error>");
			continue;
		}
		if (!object->Get(context, key).ToLocal(&prop_value)) {
			absl::StrAppend(&out, Indent(indent + 1), Utf8(isolate, key), ": <error>");
			continue;
		}
		absl::StrAppend(
			&out,
			Indent(indent + 1),
			Utf8(isolate, key),
			": ",
			FormatValueImpl(isolate, context, prop_value, depth - 1, indent + 1, false, seen_hashes, options));
	}
	if (limit < length) {
		absl::StrAppendFormat(&out, ",\n%s... %d more", Indent(indent + 1), length - limit);
	}
	absl::StrAppend(&out, "\n", Indent(indent), "}");
	return out;
}

std::string FormatValueImpl(v8::Isolate* isolate,
			    v8::Local<v8::Context> context,
			    v8::Local<v8::Value> value,
			    int depth,
			    int indent,
			    bool top_level,
			    std::unordered_set<int>* seen_hashes,
			    const FormatOptions& options) {
	if (value->IsUndefined()) {
		return "undefined";
	}
	if (value->IsNull()) {
		return "null";
	}
	if (value->IsBoolean()) {
		return value->BooleanValue(isolate) ? "true" : "false";
	}
	if (value->IsNumber() || value->IsBigInt()) {
		return Utf8(isolate, value);
	}
	if (value->IsString()) {
		return FormatStringForConsole(isolate, value.As<v8::String>(), top_level, options);
	}
	if (value->IsFunction()) {
		return Utf8(isolate, value);
	}
	if (value->IsUint8Array()) {
		return FormatTypedArray(isolate, context, value, options);
	}
	if (value->IsArrayBuffer()) {
		return FormatArrayBuffer(value.As<v8::ArrayBuffer>());
	}
	if (!value->IsObject()) {
		return Utf8(isolate, value);
	}
	if (depth <= 0) {
		return value->IsArray() ? "[Array]" : "[Object]";
	}

	v8::Local<v8::Object> object = value.As<v8::Object>();
	const int identity_hash = object->GetIdentityHash();
	if (identity_hash != 0 && seen_hashes != nullptr && seen_hashes->find(identity_hash) != seen_hashes->end()) {
		return "[Circular]";
	}
	if (identity_hash != 0 && seen_hashes != nullptr) {
		seen_hashes->insert(identity_hash);
	}

	const std::string out = value->IsArray()
		? FormatArray(isolate, context, value.As<v8::Array>(), depth, indent, seen_hashes, options)
		: FormatObject(isolate, context, object, depth, indent, seen_hashes, options);

	if (identity_hash != 0 && seen_hashes != nullptr) {
		seen_hashes->erase(identity_hash);
	}
	return out;
}

}  // namespace

std::string Utf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
	v8::String::Utf8Value utf8(isolate, value);
	if (*utf8 == nullptr) {
		return std::string();
	}
	return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
}

std::string JoinValues(v8::Isolate* isolate,
			       v8::Local<v8::Context> context,
			       const std::vector<v8::Local<v8::Value>>& values,
			       const FormatOptions& options) {
	std::unordered_set<int> seen_hashes;
	const auto piece_range = values
		| ranges::views::transform([&](v8::Local<v8::Value> value) {
			return FormatValueImpl(isolate, context, value, options.max_depth, 0, true, &seen_hashes, options);
		});
	const auto pieces = ranges::to<std::vector<std::string>>(piece_range);

	std::string text;
	for (std::size_t index = 0; index < pieces.size(); ++index) {
		if (index > 0) {
			absl::StrAppend(&text, " ");
		}
		absl::StrAppend(&text, pieces[index]);
	}
	return text;
}

std::string JoinArguments(v8::Isolate* isolate,
			  const v8::FunctionCallbackInfo<v8::Value>& args,
			  const FormatOptions& options) {
	std::vector<v8::Local<v8::Value>> values;
	values.reserve(static_cast<std::size_t>(args.Length()));
	for (int index = 0; index < args.Length(); ++index) {
		values.push_back(args[index]);
	}
	return JoinValues(isolate, isolate->GetCurrentContext(), values, options);
}

void WriteLine(Stream stream, std::string_view text) {
	flux::terminal::WriteLine(
		stream == Stream::kStdout ? flux::terminal::OutputStream::kStdout : flux::terminal::OutputStream::kStderr,
		text);
}

}  // namespace flux::console