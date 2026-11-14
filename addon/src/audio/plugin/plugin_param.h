#pragma once

// PluginParameterInfo <-> JS conversion shared by the plugin_* bindings.

#include "audio/audio_util.h"
#include "audio/plugin_wrappers/plugin_parameter_bridge.h"

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::PluginParameterInfo;

inline v8::Local<v8::Value> ParamInfoToJS(const PluginParameterInfo& p) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("id").ToLocalChecked(),
             Nan::New(static_cast<double>(p.id)));
    Nan::Set(o, Nan::New("name").ToLocalChecked(),
             Nan::New(p.name).ToLocalChecked());
    Nan::Set(o, Nan::New("module").ToLocalChecked(),
             Nan::New(p.module).ToLocalChecked());
    Nan::Set(o, Nan::New("unit").ToLocalChecked(),
             Nan::New(p.unit).ToLocalChecked());
    Nan::Set(o, Nan::New("minValue").ToLocalChecked(), Nan::New(p.min_value));
    Nan::Set(o, Nan::New("maxValue").ToLocalChecked(), Nan::New(p.max_value));
    Nan::Set(o, Nan::New("defaultValue").ToLocalChecked(),
             Nan::New(p.default_value));
    Nan::Set(o, Nan::New("currentValue").ToLocalChecked(),
             Nan::New(p.current_value));
    Nan::Set(o, Nan::New("isStepped").ToLocalChecked(),
             Nan::New(p.is_stepped));
    Nan::Set(o, Nan::New("isAutomatable").ToLocalChecked(),
             Nan::New(p.is_automatable));
    return o;
}

inline v8::Local<v8::Value> ParamInfosToJS(
    const std::vector<PluginParameterInfo>& ps) {
    auto arr = Nan::New<v8::Array>(ps.size());
    for (size_t i = 0; i < ps.size(); ++i) {
        Nan::Set(arr, static_cast<uint32_t>(i), ParamInfoToJS(ps[i]));
    }
    return arr;
}

} // namespace xer::addon::audio::plugin
