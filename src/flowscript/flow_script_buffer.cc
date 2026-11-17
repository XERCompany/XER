#include "flow_script_buffer.h"

#include "flow_script_console.h"
#include "helper/class_builder.h"
#include "helper/js_buffer.h"

#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace XER {
namespace flow_script_detail {
namespace {

using Byte = Engine::Helper::JsBuffer::Byte;
using Bytes = Engine::Helper::JsBuffer::Bytes;
using ConstSpan = Engine::Helper::JsBuffer::ConstSpan;
using MutableSpan = Engine::Helper::JsBuffer::MutableSpan;
using SizeType = Engine::Helper::JsBuffer::SizeType;

struct BufferByteView {
	std::shared_ptr<v8::BackingStore> backing;
	Byte* data = nullptr;
	SizeType size = 0;
};

v8::Local<v8::String> BufferSymbol(v8::Isolate* isolate, const char* name) {
	return Engine::Helper::ToV8Str(isolate, name);
}

v8::Local<v8::Private> BufferBrandKey(v8::Isolate* isolate) {
	return v8::Private::ForApi(isolate, BufferSymbol(isolate, "__flow_script_buffer_brand__"));
}

SizeType ClampIndex(int64_t value, SizeType length) {
	if (value < 0) {
		const int64_t adjusted = static_cast<int64_t>(length) + value;
		return adjusted <= 0 ? 0u : static_cast<SizeType>(adjusted);
	}
	return static_cast<SizeType>(std::min<int64_t>(value, static_cast<int64_t>(length)));
}

SizeType ClampOffsetForLastIndex(int64_t value, SizeType length) {
	if (value == std::numeric_limits<int64_t>::max()) {
		return length;
	}
	return ClampIndex(value, length);
}

int64_t IntegerValue(v8::Local<v8::Context> context,
			     v8::Local<v8::Value> value,
			     int64_t fallback) {
	if (value.IsEmpty() || value->IsUndefined() || value->IsNull()) {
		return fallback;
	}
	return value->IntegerValue(context).FromMaybe(fallback);
}

v8::Local<v8::Value> ArgOr(v8::Isolate* isolate,
			   const v8::FunctionCallbackInfo<v8::Value>& args,
			   int index,
			   v8::Local<v8::Value> fallback) {
	return args.Length() > index ? v8::Local<v8::Value>(args[index]) : fallback;
}

bool NormalizeEncoding(std::string* encoding) {
	if (encoding == nullptr) {
		return false;
	}
	if (encoding->empty()) {
		*encoding = "utf8";
		return true;
	}
	absl::AsciiStrToLower(encoding);
	if (*encoding == "utf-8") {
		*encoding = "utf8";
	}
	if (*encoding == "binary") {
		*encoding = "latin1";
	}
	return *encoding == "utf8" || *encoding == "hex" || *encoding == "ascii" || *encoding == "latin1";
}

bool ReadEncodingArg(v8::Isolate* isolate,
			 v8::Local<v8::Value> value,
			 std::string* encoding_out,
			 const char* error_prefix) {
	if (encoding_out == nullptr) {
		Engine::Helper::ThrowError(isolate, "encoding output target is null");
		return false;
	}
	if (value.IsEmpty() || value->IsUndefined() || value->IsNull()) {
		*encoding_out = "utf8";
		return true;
	}
	if (!value->IsString()) {
		Engine::Helper::ThrowTypeError(isolate, error_prefix);
		return false;
	}
	*encoding_out = Engine::Helper::FromV8Str(isolate, value);
	if (!NormalizeEncoding(encoding_out)) {
		Engine::Helper::ThrowError(isolate, absl::StrCat("unsupported buffer encoding: ", *encoding_out));
		return false;
	}
	return true;
}

v8::Local<v8::String> ToV8ExactString(v8::Isolate* isolate, const std::string& text) {
	return v8::String::NewFromUtf8(
		isolate,
		text.data(),
		v8::NewStringType::kNormal,
		static_cast<int>(text.size())).ToLocalChecked();
}

bool BytesFromString(v8::Isolate* isolate,
			     const std::string& text,
			     const std::string& encoding,
			     Bytes* bytes_out) {
	if (bytes_out == nullptr) {
		Engine::Helper::ThrowError(isolate, "byte output target is null");
		return false;
	}
	if (encoding == "utf8") {
		*bytes_out = Engine::Helper::JsBuffer::FromString(text);
		return true;
	}
	if (encoding == "hex") {
		std::string error;
		if (!Engine::Helper::JsBuffer::HexDecode(text, bytes_out, &error)) {
			Engine::Helper::ThrowError(isolate, error);
			return false;
		}
		return true;
	}
	*bytes_out = Bytes(text.begin(), text.end());
	return true;
}

std::string StringFromBytes(v8::Isolate* isolate,
			    ConstSpan bytes,
			    const std::string& encoding,
			    bool* ok_out) {
	if (ok_out != nullptr) {
		*ok_out = true;
	}
	if (encoding == "hex") {
		return Engine::Helper::JsBuffer::HexEncode(bytes);
	}
	if (encoding == "utf8") {
		return Engine::Helper::JsBuffer::ToString(bytes);
	}
	return std::string(bytes.begin(), bytes.end());
}

bool GetUint8ArrayByteView(v8::Local<v8::Uint8Array> view, BufferByteView* out) {
	if (out == nullptr) {
		return false;
	}
	out->size = view->ByteLength();
	v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
	out->backing = buffer->GetBackingStore();
	if (!out->backing) {
		out->data = nullptr;
		return out->size == 0;
	}
	auto* begin = static_cast<Byte*>(out->backing->Data());
	out->data = begin == nullptr ? nullptr : begin + view->ByteOffset();
	return true;
}

ConstSpan AsConstSpan(const BufferByteView& view) {
	return view.size == 0 ? ConstSpan() : ConstSpan(view.data, view.size);
}

MutableSpan AsMutableSpan(BufferByteView* view) {
	return view == nullptr || view->size == 0 ? MutableSpan() : MutableSpan(view->data, view->size);
}

bool ResolveBufferPrototype(v8::Local<v8::Context> context,
			    v8::Local<v8::Value> constructor_hint,
			    v8::Local<v8::Object>* prototype_out) {
	v8::Isolate* isolate = context->GetIsolate();
	v8::Local<v8::Value> constructor_value = constructor_hint;
	if (constructor_value.IsEmpty() || !constructor_value->IsFunction()) {
		if (!context->Global()->Get(context, BufferSymbol(isolate, "Buffer")).ToLocal(&constructor_value)
			|| !constructor_value->IsFunction()) {
			return false;
		}
	}
	v8::Local<v8::Value> prototype_value;
	if (!constructor_value.As<v8::Function>()
			 ->Get(context, BufferSymbol(isolate, "prototype"))
			 .ToLocal(&prototype_value)
		|| !prototype_value->IsObject()) {
		return false;
	}
	*prototype_out = prototype_value.As<v8::Object>();
	return true;
}

bool BrandBufferObject(v8::Local<v8::Context> context,
			       v8::Local<v8::Uint8Array> view,
			       v8::Local<v8::Value> constructor_hint) {
	v8::Isolate* isolate = context->GetIsolate();
	v8::Local<v8::Object> object = view.As<v8::Object>();
	v8::Local<v8::Object> prototype;
	if (!ResolveBufferPrototype(context, constructor_hint, &prototype)) {
		return false;
	}
	return object->SetPrivate(context, BufferBrandKey(isolate), v8::True(isolate)).FromMaybe(false)
		&& object->SetPrototype(context, prototype).FromMaybe(false);
}

bool IsBufferInstance(v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
	if (!value->IsUint8Array()) {
		return false;
	}
	v8::Local<v8::Value> brand;
	return value.As<v8::Object>()->GetPrivate(context, BufferBrandKey(context->GetIsolate())).ToLocal(&brand)
		&& brand->IsTrue();
}

bool GetBufferThis(const v8::FunctionCallbackInfo<v8::Value>& args,
			   v8::Local<v8::Uint8Array>* view_out,
			   BufferByteView* byte_view_out) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	if (!IsBufferInstance(context, args.This())) {
		Engine::Helper::ThrowTypeError(args.GetIsolate(), "Buffer method receiver must be a Buffer");
		return false;
	}
	v8::Local<v8::Uint8Array> view = args.This().As<v8::Uint8Array>();
	if (view_out != nullptr) {
		*view_out = view;
	}
	if (byte_view_out != nullptr && !GetUint8ArrayByteView(view, byte_view_out)) {
		Engine::Helper::ThrowError(args.GetIsolate(), "failed to access Buffer backing store");
		return false;
	}
	return true;
}

bool ValueToBytes(v8::Local<v8::Context> context,
			  v8::Local<v8::Value> value,
			  const std::string& encoding,
			  Bytes* bytes_out,
			  bool reject_numbers) {
	v8::Isolate* isolate = context->GetIsolate();
	if (value->IsString()) {
		return BytesFromString(isolate, Engine::Helper::FromV8Str(isolate, value), encoding, bytes_out);
	}
	if (!reject_numbers && value->IsNumber()) {
		const int32_t byte_value = value->Int32Value(context).FromMaybe(0);
		if (byte_value < 0 || byte_value > 255) {
			Engine::Helper::ThrowRangeError(isolate, "numeric buffer value must be between 0 and 255");
			return false;
		}
		*bytes_out = Bytes{static_cast<Byte>(byte_value)};
		return true;
	}
	if (ValueToByteVector(context, value, bytes_out)) {
		return true;
	}
	Engine::Helper::ThrowTypeError(isolate, "expected Buffer, Uint8Array, ArrayBuffer, number[], or string");
	return false;
}

std::optional<SizeType> OptionalLengthArg(v8::Local<v8::Context> context, int index, const v8::FunctionCallbackInfo<v8::Value>& args) {
	if (args.Length() <= index || args[index]->IsUndefined() || args[index]->IsNull()) {
		return std::nullopt;
	}
	const int64_t value = IntegerValue(context, args[index], 0);
	if (value < 0) {
		return static_cast<SizeType>(0);
	}
	return static_cast<SizeType>(value);
}

v8::MaybeLocal<v8::Uint8Array> CreateBufferFromBytes(v8::Local<v8::Context> context,
					       const Bytes& bytes,
					       v8::Local<v8::Value> constructor_hint) {
	v8::Isolate* isolate = context->GetIsolate();
	v8::Local<v8::ArrayBuffer> array_buffer = v8::ArrayBuffer::New(isolate, bytes.size());
	if (!bytes.empty()) {
		auto backing = array_buffer->GetBackingStore();
		std::memcpy(backing->Data(), bytes.data(), bytes.size());
	}
	v8::Local<v8::Uint8Array> view = v8::Uint8Array::New(array_buffer, 0, bytes.size());
	if (!BrandBufferObject(context, view, constructor_hint)) {
		return v8::MaybeLocal<v8::Uint8Array>();
	}
	return view;
}

v8::MaybeLocal<v8::Uint8Array> CreateBufferView(v8::Local<v8::Context> context,
					 v8::Local<v8::ArrayBuffer> array_buffer,
					 SizeType byte_offset,
					 SizeType length,
					 v8::Local<v8::Value> constructor_hint) {
	v8::Local<v8::Uint8Array> view = v8::Uint8Array::New(array_buffer, byte_offset, length);
	if (!BrandBufferObject(context, view, constructor_hint)) {
		return v8::MaybeLocal<v8::Uint8Array>();
	}
	return view;
}

bool ParseFillPattern(v8::Local<v8::Context> context,
			      v8::Isolate* isolate,
			      v8::Local<v8::Value> value,
			      v8::Local<v8::Value> encoding_value,
			      Bytes* pattern_out) {
	if (pattern_out == nullptr) {
		Engine::Helper::ThrowError(isolate, "fill pattern output target is null");
		return false;
	}
	std::string encoding = "utf8";
	if (!encoding_value.IsEmpty() && !encoding_value->IsUndefined() && !encoding_value->IsNull()) {
		if (!ReadEncodingArg(isolate, encoding_value, &encoding, "fill encoding must be a string")) {
			return false;
		}
	}
	return ValueToBytes(context, value, encoding, pattern_out, false);
}

void BufferPrototypeToStringCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	std::string encoding = "utf8";
	if (args.Length() > 0 && !ReadEncodingArg(isolate, args[0], &encoding, "toString encoding must be a string")) {
		return;
	}
	const SizeType start = ClampIndex(IntegerValue(context, ArgOr(isolate, args, 1, v8::Undefined(isolate)), 0), view.size);
	const SizeType end = ClampIndex(
		IntegerValue(context,
			     ArgOr(isolate, args, 2, v8::Integer::New(isolate, static_cast<int>(view.size))),
			     static_cast<int64_t>(view.size)),
		view.size);
	const Bytes slice = Engine::Helper::JsBuffer::Slice(AsConstSpan(view), start, end);
	bool ok = false;
	const std::string text = StringFromBytes(isolate, ConstSpan(slice.data(), slice.size()), encoding, &ok);
	if (!ok) {
		return;
	}
	args.GetReturnValue().Set(ToV8ExactString(isolate, text));
}

void BufferPrototypeEqualsCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	Bytes other;
	if (args.Length() < 1 || !ValueToBytes(context, args[0], "utf8", &other, true)) {
		return;
	}
	args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), Engine::Helper::JsBuffer::Equals(AsConstSpan(view), ConstSpan(other.data(), other.size()))));
}

void BufferPrototypeCompareCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	Bytes other;
	if (args.Length() < 1 || !ValueToBytes(context, args[0], "utf8", &other, true)) {
		return;
	}
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), Engine::Helper::JsBuffer::Compare(AsConstSpan(view), ConstSpan(other.data(), other.size()))));
}

void BufferPrototypeIndexOfCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	std::string encoding = "utf8";
	if (args.Length() > 2 && !args[2]->IsUndefined() && !args[2]->IsNull() && !ReadEncodingArg(args.GetIsolate(), args[2], &encoding, "indexOf encoding must be a string")) {
		return;
	}
	Bytes needle;
	if (args.Length() < 1 || !ValueToBytes(context, args[0], encoding, &needle, false)) {
		return;
	}
	const SizeType offset = ClampIndex(IntegerValue(context, ArgOr(args.GetIsolate(), args, 1, v8::Integer::New(args.GetIsolate(), 0)), 0), view.size);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), static_cast<int32_t>(Engine::Helper::JsBuffer::IndexOf(AsConstSpan(view), ConstSpan(needle.data(), needle.size()), offset))));
}

void BufferPrototypeLastIndexOfCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	std::string encoding = "utf8";
	if (args.Length() > 2 && !args[2]->IsUndefined() && !args[2]->IsNull() && !ReadEncodingArg(args.GetIsolate(), args[2], &encoding, "lastIndexOf encoding must be a string")) {
		return;
	}
	Bytes needle;
	if (args.Length() < 1 || !ValueToBytes(context, args[0], encoding, &needle, false)) {
		return;
	}
	const int64_t raw_offset = args.Length() > 1 ? IntegerValue(context, args[1], view.size) : std::numeric_limits<int64_t>::max();
	const SizeType offset = ClampOffsetForLastIndex(raw_offset, view.size);
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), static_cast<int32_t>(Engine::Helper::JsBuffer::LastIndexOf(AsConstSpan(view), ConstSpan(needle.data(), needle.size()), offset))));
}

void BufferPrototypeFillCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	if (args.Length() < 1) {
		Engine::Helper::ThrowTypeError(isolate, "fill expects a value");
		return;
	}
	Bytes pattern;
	if (!ParseFillPattern(context, isolate, args[0], ArgOr(isolate, args, 3, v8::Undefined(isolate)), &pattern)) {
		return;
	}
	const SizeType start = ClampIndex(IntegerValue(context, ArgOr(isolate, args, 1, v8::Integer::New(isolate, 0)), 0), view.size);
	const SizeType end = ClampIndex(
		IntegerValue(context,
			     ArgOr(isolate, args, 2, v8::Integer::New(isolate, static_cast<int>(view.size))),
			     static_cast<int64_t>(view.size)),
		view.size);
	Engine::Helper::JsBuffer::Fill(AsMutableSpan(&view), ConstSpan(pattern.data(), pattern.size()), start, end);
	args.GetReturnValue().Set(args.This());
}

void BufferPrototypeSliceCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	v8::Local<v8::Uint8Array> view;
	BufferByteView byte_view;
	if (!GetBufferThis(args, &view, &byte_view)) {
		return;
	}
	const SizeType start = ClampIndex(IntegerValue(context, ArgOr(isolate, args, 0, v8::Integer::New(isolate, 0)), 0), byte_view.size);
	const SizeType end = ClampIndex(
		IntegerValue(context,
			     ArgOr(isolate, args, 1, v8::Integer::New(isolate, static_cast<int>(byte_view.size))),
			     byte_view.size),
		byte_view.size);
	const SizeType length = start < end ? end - start : 0u;
	v8::MaybeLocal<v8::Uint8Array> result = CreateBufferView(context, view->Buffer(), view->ByteOffset() + start, length, v8::Local<v8::Value>());
	v8::Local<v8::Uint8Array> local_result;
	if (!result.ToLocal(&local_result)) {
		Engine::Helper::ThrowError(isolate, "failed to create Buffer slice");
		return;
	}
	args.GetReturnValue().Set(local_result);
}

void BufferPrototypeCopyCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	BufferByteView source_view;
	if (!GetBufferThis(args, nullptr, &source_view)) {
		return;
	}
	if (args.Length() < 1 || !args[0]->IsUint8Array()) {
		Engine::Helper::ThrowTypeError(isolate, "copy expects a Buffer or Uint8Array target");
		return;
	}
	BufferByteView target_view;
	if (!GetUint8ArrayByteView(args[0].As<v8::Uint8Array>(), &target_view)) {
		Engine::Helper::ThrowError(isolate, "failed to access copy target backing store");
		return;
	}
	const SizeType target_start = ClampIndex(IntegerValue(context, ArgOr(isolate, args, 1, v8::Integer::New(isolate, 0)), 0), target_view.size);
	const SizeType source_start = ClampIndex(IntegerValue(context, ArgOr(isolate, args, 2, v8::Integer::New(isolate, 0)), 0), source_view.size);
	const SizeType source_end = ClampIndex(
		IntegerValue(context,
			     ArgOr(isolate, args, 3, v8::Integer::New(isolate, static_cast<int>(source_view.size))),
			     source_view.size),
		source_view.size);
	const SizeType source_length = source_start < source_end ? source_end - source_start : 0u;
	const SizeType copied = Engine::Helper::JsBuffer::Copy(
		AsConstSpan(source_view),
		AsMutableSpan(&target_view),
		source_start,
		target_start,
		source_length);
	args.GetReturnValue().Set(v8::Integer::New(isolate, static_cast<int32_t>(copied)));
}

void BufferPrototypeSwap16Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	std::string error;
	if (!Engine::Helper::JsBuffer::Swap16(AsMutableSpan(&view), &error)) {
		Engine::Helper::ThrowError(args.GetIsolate(), error);
		return;
	}
	args.GetReturnValue().Set(args.This());
}

void BufferPrototypeSwap32Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	std::string error;
	if (!Engine::Helper::JsBuffer::Swap32(AsMutableSpan(&view), &error)) {
		Engine::Helper::ThrowError(args.GetIsolate(), error);
		return;
	}
	args.GetReturnValue().Set(args.This());
}

void BufferPrototypeSwap64Callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	std::string error;
	if (!Engine::Helper::JsBuffer::Swap64(AsMutableSpan(&view), &error)) {
		Engine::Helper::ThrowError(args.GetIsolate(), error);
		return;
	}
	args.GetReturnValue().Set(args.This());
}

void BufferPrototypeToJsonCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	BufferByteView view;
	if (!GetBufferThis(args, nullptr, &view)) {
		return;
	}
	v8::Local<v8::Object> object = v8::Object::New(isolate);
	v8::Local<v8::Array> data = v8::Array::New(isolate, static_cast<int>(view.size));
	for (SizeType index = 0; index < view.size; ++index) {
		data->Set(context, static_cast<uint32_t>(index), v8::Integer::New(isolate, view.data[index])).FromMaybe(false);
	}
	object->Set(context, BufferSymbol(isolate, "type"), BufferSymbol(isolate, "Buffer")).FromMaybe(false);
	object->Set(context, BufferSymbol(isolate, "data"), data).FromMaybe(false);
	args.GetReturnValue().Set(object);
}

v8::Local<v8::Value> BufferFromValue(v8::Local<v8::Context> context,
				      const v8::FunctionCallbackInfo<v8::Value>& args,
				      v8::Local<v8::Value> constructor_hint) {
	v8::Isolate* isolate = context->GetIsolate();
	if (args.Length() < 1) {
		Engine::Helper::ThrowTypeError(isolate, "Buffer.from expects a source value");
		return v8::Undefined(isolate);
	}
	if (args[0]->IsArrayBuffer()) {
		v8::Local<v8::ArrayBuffer> array_buffer = args[0].As<v8::ArrayBuffer>();
		const SizeType max_length = array_buffer->ByteLength();
		const SizeType byte_offset = ClampIndex(IntegerValue(context, ArgOr(isolate, args, 1, v8::Integer::New(isolate, 0)), 0), max_length);
		const SizeType requested_length = args.Length() > 2 && args[2]->IsNumber()
			? static_cast<SizeType>(std::max<int64_t>(0, IntegerValue(context, args[2], max_length - byte_offset)))
			: (max_length - byte_offset);
		const SizeType view_length = std::min(requested_length, max_length - byte_offset);
		v8::Local<v8::Uint8Array> view;
		if (!CreateBufferView(context, array_buffer, byte_offset, view_length, constructor_hint).ToLocal(&view)) {
			Engine::Helper::ThrowError(isolate, "failed to create Buffer from ArrayBuffer");
			return v8::Undefined(isolate);
		}
		return view;
	}
	if (args[0]->IsNumber()) {
		Engine::Helper::ThrowTypeError(isolate, "Buffer.from does not accept a number; use Buffer.alloc");
		return v8::Undefined(isolate);
	}
	std::string encoding = "utf8";
	if (args[0]->IsString() && args.Length() > 1 && !ReadEncodingArg(isolate, args[1], &encoding, "Buffer.from encoding must be a string")) {
		return v8::Undefined(isolate);
	}
	Bytes bytes;
	if (!ValueToBytes(context, args[0], encoding, &bytes, true)) {
		return v8::Undefined(isolate);
	}
	v8::Local<v8::Uint8Array> view;
	if (!CreateBufferFromBytes(context, bytes, constructor_hint).ToLocal(&view)) {
		Engine::Helper::ThrowError(isolate, "failed to create Buffer instance");
		return v8::Undefined(isolate);
	}
	return view;
}

void BufferConstructorCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	if (args.Length() > 0 && args[0]->IsNumber()) {
		const int64_t size_value = IntegerValue(context, args[0], 0);
		if (size_value < 0) {
			Engine::Helper::ThrowRangeError(isolate, "Buffer size must be non-negative");
			return;
		}
		Bytes bytes(static_cast<SizeType>(size_value), 0u);
		if (args.Length() > 1 && !args[1]->IsUndefined() && !args[1]->IsNull()) {
			Bytes pattern;
			if (!ParseFillPattern(context, isolate, args[1], ArgOr(isolate, args, 2, v8::Undefined(isolate)), &pattern)) {
				return;
			}
			Engine::Helper::JsBuffer::Fill(MutableSpan(bytes.data(), bytes.size()), ConstSpan(pattern.data(), pattern.size()));
		}
		v8::Local<v8::Uint8Array> view;
		if (!CreateBufferFromBytes(context, bytes, args.IsConstructCall() ? args.NewTarget() : v8::Local<v8::Value>()).ToLocal(&view)) {
			Engine::Helper::ThrowError(isolate, "failed to create Buffer allocation");
			return;
		}
		args.GetReturnValue().Set(view);
		return;
	}
	args.GetReturnValue().Set(BufferFromValue(context, args, args.IsConstructCall() ? args.NewTarget() : v8::Local<v8::Value>()));
}

void BufferFromCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	args.GetReturnValue().Set(BufferFromValue(context, args, v8::Local<v8::Value>()));
}

void BufferAllocCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	BufferConstructorCallback(args);
}

void BufferAllocUnsafeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	BufferConstructorCallback(args);
}

void BufferConcatCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	if (args.Length() < 1 || !args[0]->IsArray()) {
		Engine::Helper::ThrowTypeError(isolate, "Buffer.concat expects an array of buffers");
		return;
	}
	v8::Local<v8::Array> list = args[0].As<v8::Array>();
	std::vector<Bytes> storage;
	storage.reserve(list->Length());
	std::vector<ConstSpan> spans;
	spans.reserve(list->Length());
	for (uint32_t index = 0; index < list->Length(); ++index) {
		v8::Local<v8::Value> item;
		if (!list->Get(context, index).ToLocal(&item)) {
			Engine::Helper::ThrowError(isolate, "failed to read Buffer.concat item");
			return;
		}
		storage.emplace_back();
		if (!ValueToBytes(context, item, "utf8", &storage.back(), true)) {
			return;
		}
		spans.push_back(ConstSpan(storage.back().data(), storage.back().size()));
	}
	Bytes bytes = Engine::Helper::JsBuffer::Concat(spans);
	if (args.Length() > 1 && args[1]->IsNumber()) {
		const SizeType total_length = static_cast<SizeType>(std::max<int64_t>(0, IntegerValue(context, args[1], bytes.size())));
		if (bytes.size() > total_length) {
			bytes.resize(total_length);
		} else if (bytes.size() < total_length) {
			bytes.resize(total_length, 0u);
		}
	}
	v8::Local<v8::Uint8Array> view;
	if (!CreateBufferFromBytes(context, bytes, v8::Local<v8::Value>()).ToLocal(&view)) {
		Engine::Helper::ThrowError(isolate, "failed to create Buffer.concat result");
		return;
	}
	args.GetReturnValue().Set(view);
}

void BufferCompareStaticCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	Bytes lhs;
	Bytes rhs;
	if (args.Length() < 2 || !ValueToBytes(context, args[0], "utf8", &lhs, true) || !ValueToBytes(context, args[1], "utf8", &rhs, true)) {
		return;
	}
	args.GetReturnValue().Set(v8::Integer::New(args.GetIsolate(), Engine::Helper::JsBuffer::Compare(ConstSpan(lhs.data(), lhs.size()), ConstSpan(rhs.data(), rhs.size()))));
}

void BufferByteLengthCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	if (args.Length() < 1) {
		Engine::Helper::ThrowTypeError(isolate, "Buffer.byteLength expects a value");
		return;
	}
	std::string encoding = "utf8";
	if (args[0]->IsString() && args.Length() > 1 && !ReadEncodingArg(isolate, args[1], &encoding, "Buffer.byteLength encoding must be a string")) {
		return;
	}
	Bytes bytes;
	if (!ValueToBytes(context, args[0], encoding, &bytes, true)) {
		return;
	}
	args.GetReturnValue().Set(v8::Integer::New(isolate, static_cast<int32_t>(bytes.size())));
}

void BufferIsBufferCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Local<v8::Context> context = args.GetIsolate()->GetCurrentContext();
	args.GetReturnValue().Set(v8::Boolean::New(args.GetIsolate(), args.Length() > 0 && IsBufferInstance(context, args[0])));
}

bool BindFunction(v8::Local<v8::Context> context,
			 v8::Local<v8::Object> object,
			 const char* name,
			 v8::FunctionCallback callback) {
	v8::Local<v8::Function> function;
	if (!v8::Function::New(context, callback).ToLocal(&function)) {
		return false;
	}
	return object->Set(context, BufferSymbol(context->GetIsolate(), name), function).FromMaybe(false);
}

}  // namespace

bool BindBuffer(v8::Isolate* isolate, v8::Local<v8::Context> context) {
	v8::Local<v8::Function> constructor;
	if (!v8::Function::New(context, &BufferConstructorCallback).ToLocal(&constructor)) {
		return false;
	}
	constructor->SetName(BufferSymbol(isolate, "Buffer"));

	v8::Local<v8::Object> prototype = v8::Object::New(isolate);
	v8::Local<v8::Value> uint8_array_ctor_value;
	v8::Local<v8::Value> uint8_array_proto_value;
	if (!context->Global()->Get(context, BufferSymbol(isolate, "Uint8Array")).ToLocal(&uint8_array_ctor_value)
		|| !uint8_array_ctor_value->IsFunction()
		|| !uint8_array_ctor_value.As<v8::Function>()->Get(context, BufferSymbol(isolate, "prototype")).ToLocal(&uint8_array_proto_value)
		|| !uint8_array_proto_value->IsObject()) {
		return false;
	}
	if (!prototype->SetPrototype(context, uint8_array_proto_value.As<v8::Object>()).FromMaybe(false)) {
		return false;
	}
	bool ok = true;
	ok = ok && prototype->Set(context, BufferSymbol(isolate, "constructor"), constructor).FromMaybe(false);
	ok = ok && BindFunction(context, prototype, "toString", &BufferPrototypeToStringCallback);
	ok = ok && BindFunction(context, prototype, "equals", &BufferPrototypeEqualsCallback);
	ok = ok && BindFunction(context, prototype, "compare", &BufferPrototypeCompareCallback);
	ok = ok && BindFunction(context, prototype, "indexOf", &BufferPrototypeIndexOfCallback);
	ok = ok && BindFunction(context, prototype, "lastIndexOf", &BufferPrototypeLastIndexOfCallback);
	ok = ok && BindFunction(context, prototype, "fill", &BufferPrototypeFillCallback);
	ok = ok && BindFunction(context, prototype, "slice", &BufferPrototypeSliceCallback);
	ok = ok && BindFunction(context, prototype, "copy", &BufferPrototypeCopyCallback);
	ok = ok && BindFunction(context, prototype, "swap16", &BufferPrototypeSwap16Callback);
	ok = ok && BindFunction(context, prototype, "swap32", &BufferPrototypeSwap32Callback);
	ok = ok && BindFunction(context, prototype, "swap64", &BufferPrototypeSwap64Callback);
	ok = ok && BindFunction(context, prototype, "toJSON", &BufferPrototypeToJsonCallback);
	ok = ok && constructor->Set(context, BufferSymbol(isolate, "prototype"), prototype).FromMaybe(false);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "from", &BufferFromCallback);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "alloc", &BufferAllocCallback);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "allocUnsafe", &BufferAllocUnsafeCallback);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "concat", &BufferConcatCallback);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "compare", &BufferCompareStaticCallback);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "byteLength", &BufferByteLengthCallback);
	ok = ok && BindFunction(context, constructor.As<v8::Object>(), "isBuffer", &BufferIsBufferCallback);
	ok = ok && context->Global()->Set(context, BufferSymbol(isolate, "Buffer"), constructor).FromMaybe(false);
	return ok;
}

}  // namespace flow_script_detail
}  // namespace XER