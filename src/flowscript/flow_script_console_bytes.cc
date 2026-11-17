#include "flow_script_console.h"

#include <cstring>

namespace XER {
namespace flow_script_detail {

bool ValueToByteVector(
    v8::Local<v8::Context> context,
    v8::Local<v8::Value> value,
    std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    if (value->IsUint8Array()) {
        v8::Local<v8::Uint8Array> arr = value.As<v8::Uint8Array>();
        const size_t len = arr->Length();
        out->resize(len);
        arr->CopyContents(out->data(), len);
        return true;
    }

    if (value->IsArrayBuffer()) {
        v8::Local<v8::ArrayBuffer> ab = value.As<v8::ArrayBuffer>();
        const std::shared_ptr<v8::BackingStore> backing = ab->GetBackingStore();
        if (!backing) {
            return false;
        }
        const auto* begin = static_cast<const std::uint8_t*>(backing->Data());
        out->assign(begin, begin + backing->ByteLength());
        return true;
    }

    if (value->IsArray()) {
        v8::Local<v8::Array> arr = value.As<v8::Array>();
        const uint32_t len = arr->Length();
        out->reserve(len);
        for (uint32_t i = 0; i < len; ++i) {
            v8::Local<v8::Value> item;
            if (!arr->Get(context, i).ToLocal(&item) || !item->IsNumber()) {
                return false;
            }
            const double num = item->NumberValue(context).FromMaybe(0.0);
            if (num < 0.0 || num > 255.0) {
                return false;
            }
            out->push_back(static_cast<std::uint8_t>(num));
        }
        return true;
    }

    return false;
}

v8::Local<v8::Uint8Array> ByteVectorToUint8Array(
    v8::Isolate* isolate,
    const std::vector<std::uint8_t>& bytes) {
    v8::EscapableHandleScope scope(isolate);
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, bytes.size());
    if (!bytes.empty()) {
        std::memcpy(buffer->Data(), bytes.data(), bytes.size());
    }
    v8::Local<v8::Uint8Array> out = v8::Uint8Array::New(buffer, 0, bytes.size());
    return scope.Escape(out);
}

}  // namespace flow_script_detail
}  // namespace XER
