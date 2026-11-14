// xer.audio.codec.Id3 — MetadataId3 binding.

#include "id3.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::MetadataId3;
using XER::Audio::CodecIO::Id3Tag;

Id3Wrap::Id3Wrap() = default;
Id3Wrap::~Id3Wrap() = default;

Id3Wrap* UnwrapId3(v8::Local<v8::Value> v) {
    return UnwrapT<Id3Wrap>(v);
}

void Id3Wrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new Id3Wrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void Id3Wrap::Parse(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapId3(info.This());
    if (!w) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Id3.Parse expects Uint8Array");
        return;
    }
    Id3Tag tag;
    if (!w->Get().Parse(in.data(), in.size(), tag)) return;
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("title").ToLocalChecked(), Nan::New(tag.title).ToLocalChecked());
    Nan::Set(o, Nan::New("artist").ToLocalChecked(), Nan::New(tag.artist).ToLocalChecked());
    Nan::Set(o, Nan::New("album").ToLocalChecked(), Nan::New(tag.album).ToLocalChecked());
    info.GetReturnValue().Set(o);
}

void Id3Wrap::Build(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapId3(info.This());
    if (!w) return;
    if (info.Length() < 1 || !info[0]->IsObject()) return;
    v8::Local<v8::Object> o = info[0].As<v8::Object>();
    Id3Tag tag;
    auto gs = [&](const char* k, std::string* dst) {
        v8::Local<v8::Value> v;
        if (Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(&v) &&
            v->IsString()) {
            Nan::Utf8String s(v);
            *dst = std::string(*s, static_cast<size_t>(s.length()));
        }
    };
    gs("title", &tag.title); gs("artist", &tag.artist);
    gs("album", &tag.album);
    info.GetReturnValue().Set(U8ToJS(w->Get().Build(tag)));
}


void InitCodecId3(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Id3", Id3Wrap::New,
        {{"Parse", Id3Wrap::Parse},
         {"Build", Id3Wrap::Build}});
}

} // namespace xer::addon::audio::codec
