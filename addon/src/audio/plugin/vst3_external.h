#pragma once

// xer.audio.plugin.Vst3External — wraps XER::Audio::Plugin::Vst3ExternalHost.

#include <memory>

#include "audio/plugin_wrappers/vst3_external_host.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class Vst3ExtWrap : public Nan::ObjectWrap {
public:
    explicit Vst3ExtWrap();
    ~Vst3ExtWrap() override;

    XER::Audio::Plugin::Vst3ExternalHost& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Load(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Unload(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsLoaded(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameters(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPluginName(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::Vst3ExternalHost a_;
};

Vst3ExtWrap* UnwrapVst3Ext(v8::Local<v8::Value> v);
void InitPluginVst3External(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
