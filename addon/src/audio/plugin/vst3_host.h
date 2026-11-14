#pragma once

// xer.audio.plugin.Vst3Host — wraps XER::Audio::Plugin::Vst3HostInterface.

#include <memory>

#include "audio/plugin_wrappers/vst3_host_interface.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class Vst3HostWrap : public Nan::ObjectWrap {
public:
    explicit Vst3HostWrap();
    ~Vst3HostWrap() override;

    XER::Audio::Plugin::Vst3HostInterface& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void LoadPlugin(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameters(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBackendMode(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::Vst3HostInterface a_;
};

Vst3HostWrap* UnwrapVst3Host(v8::Local<v8::Value> v);
void InitPluginVst3Host(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
