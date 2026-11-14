#pragma once

// xer.audio.plugin.ParamBridge — wraps XER::Audio::Plugin::PluginParameterBridge.

#include <memory>

#include "audio/plugin_wrappers/plugin_parameter_bridge.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class ParamBridgeWrap : public Nan::ObjectWrap {
public:
    explicit ParamBridgeWrap();
    ~ParamBridgeWrap() override;

    XER::Audio::Plugin::PluginParameterBridge& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void RegisterParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetValue(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetNormalizedValue(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetValue(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Snapshot(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::PluginParameterBridge a_;
};

ParamBridgeWrap* UnwrapParamBridge(v8::Local<v8::Value> v);
void InitPluginParamBridge(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
