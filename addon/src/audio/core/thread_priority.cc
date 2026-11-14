// xer.audio.core.ThreadPriority — AudioThreadPriority binding.

#include "thread_priority.h"

#include "audio/audio_core/audio_thread_priority.h"

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioThreadPriority;
using XER::Audio::Core::ThreadPriority;

namespace {

ThreadPriority PriorityFromArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                               int index) {
    const int v = IntArg(info, index, 1);
    if (v < 0 || v > 3) return ThreadPriority::NORMAL;
    return static_cast<ThreadPriority>(v);
}

NAN_METHOD(SetCurrentThreadPriority) {
    info.GetReturnValue().Set(Nan::New(
        AudioThreadPriority::SetCurrentThreadPriority(
            PriorityFromArg(info, 0))));
}

NAN_METHOD(SetThreadAffinity) {
    info.GetReturnValue().Set(Nan::New(
        AudioThreadPriority::SetThreadAffinity(IntArg(info, 0, -1))));
}

NAN_METHOD(SetAffinity) {
    info.GetReturnValue().Set(Nan::New(
        AudioThreadPriority::SetAffinity(IntArg(info, 0, -1))));
}

NAN_METHOD(GetOptimalCore) {
    int core = -1;
    const bool ok = AudioThreadPriority::GetOptimalCore(core);
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("ok").ToLocalChecked(), Nan::New(ok));
    Nan::Set(o, Nan::New("coreId").ToLocalChecked(), Nan::New(core));
    info.GetReturnValue().Set(o);
}

NAN_METHOD(GetOptimalAudioCore) {
    info.GetReturnValue().Set(
        Nan::New(AudioThreadPriority::GetOptimalAudioCore()));
}

NAN_METHOD(GetPriorityName) {
    info.GetReturnValue().Set(
        Nan::New(AudioThreadPriority::GetPriorityName(
                     PriorityFromArg(info, 0)))
            .ToLocalChecked());
}

} // namespace

void InitCoreThreadPriority(v8::Local<v8::Object> ns) {
    Nan::Set(ns, Nan::New("SetCurrentThreadPriority").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 SetCurrentThreadPriority)).ToLocalChecked());
    Nan::Set(ns, Nan::New("SetThreadAffinity").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 SetThreadAffinity)).ToLocalChecked());
    Nan::Set(ns, Nan::New("SetAffinity").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(SetAffinity))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("GetOptimalCore").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(GetOptimalCore))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("GetOptimalAudioCore").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 GetOptimalAudioCore)).ToLocalChecked());
    Nan::Set(ns, Nan::New("GetPriorityName").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(GetPriorityName))
                 .ToLocalChecked());
}

} // namespace xer::addon::audio::core
