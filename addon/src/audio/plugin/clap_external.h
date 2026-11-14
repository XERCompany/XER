#pragma once

// xer.audio.plugin.ClapExternal — wraps XER::Audio::Plugin::ClapExternalPluginHost.

#include <memory>

#include "audio/plugin_wrappers/clap_external_plugin_host.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class ClapExtWrap : public Nan::ObjectWrap {
public:
    explicit ClapExtWrap();
    ~ClapExtWrap() override;

    XER::Audio::Plugin::ClapExternalPluginHost& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Load(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameters(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLoadedIdentifier(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::ClapExternalPluginHost a_;
};

ClapExtWrap* UnwrapClapExt(v8::Local<v8::Value> v);
void InitPluginClapExternal(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
