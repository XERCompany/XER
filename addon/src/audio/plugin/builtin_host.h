#pragma once

// xer.audio.plugin.BuiltinHost — wraps XER::Audio::Plugin::BuiltinPluginHost.

#include <memory>

#include "audio/plugin_wrappers/plugin_builtin_host.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class BuiltinHostWrap : public Nan::ObjectWrap {
public:
    explicit BuiltinHostWrap();
    ~BuiltinHostWrap() override;

    XER::Audio::Plugin::BuiltinPluginHost& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Load(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameters(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::BuiltinPluginHost a_;
};

BuiltinHostWrap* UnwrapBuiltinHost(v8::Local<v8::Value> v);
void InitPluginBuiltinHost(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
