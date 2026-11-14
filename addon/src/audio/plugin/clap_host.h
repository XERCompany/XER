#pragma once

// xer.audio.plugin.ClapHost — wraps XER::Audio::Plugin::ClapHostInterface.

#include <memory>

#include "audio/plugin_wrappers/clap_host_interface.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class ClapHostWrap : public Nan::ObjectWrap {
public:
    explicit ClapHostWrap();
    ~ClapHostWrap() override;

    XER::Audio::Plugin::ClapHostInterface& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void LoadPlugin(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameters(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::ClapHostInterface a_;
};

ClapHostWrap* UnwrapClapHost(v8::Local<v8::Value> v);
void InitPluginClapHost(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
