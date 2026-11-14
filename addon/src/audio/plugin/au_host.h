#pragma once

// xer.audio.plugin.AuHost — wraps XER::Audio::Plugin::AuHostInterface.

#include <memory>

#include "audio/plugin_wrappers/au_host_interface.h"

#include "audio/audio_util.h"
#include "plugin_param.h"

namespace xer::addon::audio::plugin {

class AuHostWrap : public Nan::ObjectWrap {
public:
    explicit AuHostWrap();
    ~AuHostWrap() override;

    XER::Audio::Plugin::AuHostInterface& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void LoadComponent(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetParameters(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBackendMode(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Plugin::AuHostInterface a_;
};

AuHostWrap* UnwrapAuHost(v8::Local<v8::Value> v);
void InitPluginAuHost(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::plugin
