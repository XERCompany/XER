// xer.audio.core.SourceLoader — AudioSourceLoader binding.

#include "source_loader.h"

#include <string>
#include <vector>

#include "../buffer.h"

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioSourceLoadOptions;
using XER::Audio::Core::AudioSourceBuffer;

SourceLoaderWrap::SourceLoaderWrap() = default;
SourceLoaderWrap::~SourceLoaderWrap() = default;

SourceLoaderWrap* UnwrapSourceLoader(v8::Local<v8::Value> v) {
    return UnwrapT<SourceLoaderWrap>(v);
}

void SourceLoaderWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new SourceLoaderWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

namespace {

// Reads {path, targetSampleRate, targetChannels, resampleQuality,
//        strictMp3, rawSampleRate} from a JS object (or string path).
bool ReadLoadOptions(v8::Local<v8::Value> v, AudioSourceLoadOptions* opts,
                     std::string* path_out) {
    if (v.IsEmpty()) return false;
    if (v->IsString()) {
        Nan::Utf8String s(v);
        opts->input_path = std::string(*s, static_cast<std::size_t>(s.length()));
        *path_out = opts->input_path.string();
        return true;
    }
    if (!v->IsObject()) return false;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    auto prop = [&](const char* k, v8::Local<v8::Value>* dst) {
        return Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(dst);
    };
    v8::Local<v8::Value> x;
    if (prop("path", &x) && x->IsString()) {
        Nan::Utf8String s(x);
        opts->input_path =
            std::string(*s, static_cast<std::size_t>(s.length()));
    }
    if (prop("targetSampleRate", &x) && x->IsNumber())
        opts->target_sample_rate = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("targetChannels", &x) && x->IsNumber())
        opts->target_channels = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("rawSampleRate", &x) && x->IsNumber())
        opts->raw_sample_rate = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("resampleQuality", &x) && x->IsNumber())
        opts->resample_quality = static_cast<XER::Audio::Core::ResampleQuality>(
            static_cast<int>(x.As<v8::Number>()->Value()));
    if (prop("strictMp3", &x) && x->IsBoolean())
        opts->strict_mp3_input = x.As<v8::Boolean>()->Value();
    *path_out = opts->input_path.string();
    return !opts->input_path.empty();
}

} // namespace

void SourceLoaderWrap::Load(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSourceLoader(info.This());
    if (!w || info.Length() < 1) return;
    AudioSourceLoadOptions opts;
    std::string path;
    if (!ReadLoadOptions(info[0], &opts, &path)) {
        Nan::ThrowError("SourceLoader.Load expects path string or options");
        return;
    }
    auto buf = std::make_shared<AudioSourceBuffer>();
    std::string error;
    if (!XER::Audio::Core::AudioSourceLoader::Load(opts, buf.get(), &error)) {
        Nan::ThrowError(("SourceLoader.Load failed: " + error).c_str());
        return;
    }
    info.GetReturnValue().Set(
        xer::addon::audio::BufferWrap::ToJS(std::move(buf)));
}

void SourceLoaderWrap::DescribeOptions(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioSourceLoadOptions opts;
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("rawSampleRate").ToLocalChecked(),
             Nan::New(opts.raw_sample_rate));
    Nan::Set(o, Nan::New("targetSampleRate").ToLocalChecked(),
             Nan::New(opts.target_sample_rate));
    Nan::Set(o, Nan::New("targetChannels").ToLocalChecked(),
             Nan::New(opts.target_channels));
    Nan::Set(o, Nan::New("resampleQuality").ToLocalChecked(),
             Nan::New(static_cast<int>(opts.resample_quality)));
    Nan::Set(o, Nan::New("strictMp3").ToLocalChecked(),
             Nan::New(opts.strict_mp3_input));
    info.GetReturnValue().Set(o);
}


void InitCoreSourceLoader(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "SourceLoader", SourceLoaderWrap::New,
        {{"Load", SourceLoaderWrap::Load},
         {"DescribeOptions", SourceLoaderWrap::DescribeOptions}});
}

} // namespace xer::addon::audio::core
