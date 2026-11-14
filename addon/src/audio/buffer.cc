// xer.audio.Buffer — decoded audio buffer binding.

#include "buffer.h"

#include <cstring>
#include <string>
#include <utility>

#include "helper/path_utils.h"

namespace xer::addon::audio {

namespace {

namespace Path = XER::Helper::Path;
using XER::Audio::Core::AudioSourceBuffer;
using XER::Audio::Core::AudioSourceLoader;
using XER::Audio::Core::AudioSourceLoadOptions;

std::shared_ptr<AudioSourceBuffer> MakeBuffer() {
    return std::make_shared<AudioSourceBuffer>();
}

// Heap-allocated on purpose: a v8::Global destroyed during/after isolate
// teardown calls DisposeGlobal on a dead isolate -> segfault at exit.
// Leaked for the process lifetime (same trade-off as WidgetBaseTemplateStore).
v8::Global<v8::FunctionTemplate>& BufferTemplateStore() {
    static auto* tpl = new v8::Global<v8::FunctionTemplate>();
    return *tpl;
}

v8::Global<v8::Function>& BufferCtorStore() {
    static auto* fn = new v8::Global<v8::Function>();
    return *fn;
}

v8::Local<v8::FunctionTemplate> BufferTemplate() {
    return v8::Local<v8::FunctionTemplate>::New(
        v8::Isolate::GetCurrent(), BufferTemplateStore());
}

v8::Local<v8::Function> BufferCtor() {
    return v8::Local<v8::Function>::New(
        v8::Isolate::GetCurrent(), BufferCtorStore());
}

BufferWrap* Unwrap(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    v8::Local<v8::Object> obj = v.As<v8::Object>();
    if (obj->InternalFieldCount() < 1) return nullptr;
    // Reject foreign ObjectWraps (e.g. a Player passed to Player.Load):
    // internal field 0 is an ObjectWrap*, but only Buffer instances carry
    // the Buffer constructor.
    v8::Local<v8::Context> ctx = Nan::GetCurrentContext();
    v8::Local<v8::Value> ctor;
    if (!Nan::Get(obj, Nan::New("constructor").ToLocalChecked())
             .ToLocal(&ctor) ||
        ctor.IsEmpty() || !ctor->StrictEquals(BufferCtor())) {
        return nullptr;
    }
    return Nan::ObjectWrap::Unwrap<BufferWrap>(obj);
}

} // namespace

BufferWrap::BufferWrap(std::shared_ptr<AudioSourceBuffer> buffer)
    : buffer_(std::move(buffer)) {}

BufferWrap::~BufferWrap() = default;

BufferWrap* UnwrapBuffer(v8::Local<v8::Value> v) { return Unwrap(v); }

v8::Local<v8::Object> BufferWrap::ToJS(
    std::shared_ptr<AudioSourceBuffer> buf) {
    v8::Local<v8::Context> ctx = Nan::GetCurrentContext();
    v8::Local<v8::Object> obj = BufferTemplate()
                                   ->InstanceTemplate()
                                   ->NewInstance(ctx)
                                   .ToLocalChecked();
    auto* wrap = new BufferWrap(std::move(buf));
    wrap->Wrap(obj);
    return obj;
}

void BufferWrap::Adopt(
    const Nan::FunctionCallbackInfo<v8::Value>& info,
    std::shared_ptr<AudioSourceBuffer> buf) {
    auto* wrap = new BufferWrap(std::move(buf));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

// --- ctor: empty buffer ------------------------------------------------------

void BufferWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Adopt(info, MakeBuffer());
}

// --- Buffer.FromFile(path[, {sampleRate, channels}]) -------------------------
// Static factory on the constructor — returns a NEW Buffer instance.

void BufferWrap::FromFile(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 1 || !info[0]->IsString()) {
        Nan::ThrowError("Buffer.FromFile(path[, {sampleRate, channels}])");
        return;
    }
    Nan::Utf8String path(info[0]);
    const std::string raw =
        std::string(*path, static_cast<std::size_t>(path.length()));
    AudioSourceLoadOptions opts;
    opts.input_path =
        Path::FromUtf8(Path::NormalizeUtf8Path(raw));
    opts.target_sample_rate = 44100;
    opts.resample_quality =
        XER::Audio::Core::ResampleQuality::HIGH;
    if (info.Length() > 1 && info[1]->IsObject()) {
        v8::Local<v8::Object> o = info[1].As<v8::Object>();
        v8::Local<v8::Value> sr;
        if (Nan::Get(o, Nan::New("sampleRate").ToLocalChecked())
                .ToLocal(&sr) &&
            sr->IsNumber()) {
            opts.target_sample_rate =
                static_cast<int>(sr.As<v8::Number>()->Value());
        }
        v8::Local<v8::Value> ch;
        if (Nan::Get(o, Nan::New("channels").ToLocalChecked())
                .ToLocal(&ch) &&
            ch->IsNumber()) {
            opts.target_channels =
                static_cast<int>(ch.As<v8::Number>()->Value());
        }
    }
    auto buf = MakeBuffer();
    std::string err;
    if (!AudioSourceLoader::Load(opts, buf.get(), &err)) {
        Nan::ThrowError(Nan::New(err.empty() ? "decode failed" : err)
                            .ToLocalChecked());
        return;
    }
    info.GetReturnValue().Set(ToJS(std::move(buf)));
}

// --- Buffer.FromPCM({data, sampleRate, channels}) ----------------------------
// data: Buffer/TypedArray of float32 (interleaved).

void BufferWrap::FromPCM(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 1 || !info[0]->IsObject()) {
        Nan::ThrowError("Buffer.FromPCM({data, sampleRate, channels})");
        return;
    }
    v8::Local<v8::Object> o = info[0].As<v8::Object>();
    auto buf = MakeBuffer();
    buf->sample_rate = 44100;
    buf->channels = 2;
    v8::Local<v8::Value> v;
    if (Nan::Get(o, Nan::New("sampleRate").ToLocalChecked())
            .ToLocal(&v) &&
        v->IsNumber()) {
        buf->sample_rate = static_cast<int>(v.As<v8::Number>()->Value());
    }
    if (Nan::Get(o, Nan::New("channels").ToLocalChecked())
            .ToLocal(&v) &&
        v->IsNumber()) {
        buf->channels = static_cast<int>(v.As<v8::Number>()->Value());
    }

    if (Nan::Get(o, Nan::New("data").ToLocalChecked())
            .ToLocal(&v) &&
        v->IsArrayBufferView()) {
        v8::Local<v8::ArrayBufferView> view = v.As<v8::ArrayBufferView>();
        const size_t bytes = view->ByteLength();
        buf->samples.resize(bytes / sizeof(float));
        char* dst = reinterpret_cast<char*>(buf->samples.data());
        const char* src =
            static_cast<const char*>(
                view->Buffer()->GetBackingStore()->Data()) +
            static_cast<size_t>(view->ByteOffset());
        std::memcpy(dst, src, bytes);
        buf->frame_count = buf->samples.size() /
                           static_cast<size_t>(std::max(1, buf->channels));
    }
    buf->original_sample_rate = buf->sample_rate;
    buf->original_channels = buf->channels;
    buf->original_frame_count = buf->frame_count;
    buf->source_format = "pcm";
    info.GetReturnValue().Set(ToJS(std::move(buf)));
}

// --- getters -----------------------------------------------------------------

void BufferWrap::GetSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() ? w->Get()->sample_rate : 0));
}

void BufferWrap::GetChannels(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() ? w->Get()->channels : 0));
}

void BufferWrap::GetFrames(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(
        w && w->Get() ? w->Get()->frame_count : 0)));
}

void BufferWrap::GetDuration(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    double dur = 0.0;
    if (w && w->Get() && w->Get()->sample_rate > 0) {
        dur = static_cast<double>(w->Get()->frame_count) /
              w->Get()->sample_rate;
    }
    info.GetReturnValue().Set(Nan::New(dur));
}

void BufferWrap::GetFormat(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    info.GetReturnValue().Set(
        w && w->Get()
            ? Nan::New(w->Get()->source_format).ToLocalChecked()
            : Nan::New("").ToLocalChecked());
}

void BufferWrap::GetCodec(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    info.GetReturnValue().Set(
        w && w->Get()
            ? Nan::New(w->Get()->codec_name).ToLocalChecked()
            : Nan::New("").ToLocalChecked());
}

void BufferWrap::GetBackend(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuffer(info.This());
    info.GetReturnValue().Set(
        w && w->Get()
            ? Nan::New(w->Get()->decode_backend).ToLocalChecked()
            : Nan::New("").ToLocalChecked());
}

// --- registration ------------------------------------------------------------

void InitAudioBuffer(v8::Local<v8::Object> audio_ns) {
    v8::Local<v8::FunctionTemplate> tpl =
        Nan::New<v8::FunctionTemplate>(BufferWrap::New);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New("Buffer").ToLocalChecked());

    Nan::SetMethod(tpl->PrototypeTemplate(), "GetSampleRate",
                   BufferWrap::GetSampleRate);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetChannels",
                   BufferWrap::GetChannels);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetFrames",
                   BufferWrap::GetFrames);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetDuration",
                   BufferWrap::GetDuration);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetFormat",
                   BufferWrap::GetFormat);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetCodec",
                   BufferWrap::GetCodec);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetBackend",
                   BufferWrap::GetBackend);

    v8::Local<v8::Function> ctor = Nan::GetFunction(tpl).ToLocalChecked();
    BufferTemplateStore().Reset(v8::Isolate::GetCurrent(), tpl);
    BufferCtorStore().Reset(v8::Isolate::GetCurrent(), ctor);

    // Static factories on the constructor:
    //   xer.audio.Buffer.FromFile(path[, opts])
    //   xer.audio.Buffer.FromPCM({data, sampleRate, channels})
    Nan::Set(ctor, Nan::New("FromFile").ToLocalChecked(),
             Nan::GetFunction(
                 Nan::New<v8::FunctionTemplate>(BufferWrap::FromFile))
                 .ToLocalChecked());
    Nan::Set(ctor, Nan::New("FromPCM").ToLocalChecked(),
             Nan::GetFunction(
                 Nan::New<v8::FunctionTemplate>(BufferWrap::FromPCM))
                 .ToLocalChecked());

    Nan::Set(audio_ns, Nan::New("Buffer").ToLocalChecked(), ctor);
}

} // namespace xer::addon::audio
