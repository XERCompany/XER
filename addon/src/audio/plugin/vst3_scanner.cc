// xer.audio.plugin.Vst3Scanner — VST3 module scanner binding.

#include "vst3_scanner.h"

#include <string>
#include <vector>

#include "audio/plugin_wrappers/vst3_plugin_scanner.h"

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::Vst3PluginInfo;

namespace {

v8::Local<v8::Value> PluginInfoToJS(const Vst3PluginInfo& p) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("path").ToLocalChecked(),
             Nan::New(p.path).ToLocalChecked());
    Nan::Set(o, Nan::New("modulePath").ToLocalChecked(),
             Nan::New(p.module_path).ToLocalChecked());
    Nan::Set(o, Nan::New("name").ToLocalChecked(),
             Nan::New(p.name).ToLocalChecked());
    Nan::Set(o, Nan::New("vendor").ToLocalChecked(),
             Nan::New(p.vendor).ToLocalChecked());
    Nan::Set(o, Nan::New("category").ToLocalChecked(),
             Nan::New(p.category).ToLocalChecked());
    Nan::Set(o, Nan::New("version").ToLocalChecked(),
             Nan::New(p.version).ToLocalChecked());
    return o;
}

NAN_METHOD(Scan) {
    // Scan([root, ...]) or Scan() -> default roots.
    std::vector<std::string> roots;
    if (info.Length() > 0 && info[0]->IsArray()) {
        roots = StrArray(info[0]);
    } else if (info.Length() > 0 && info[0]->IsString()) {
        Nan::Utf8String s(info[0]);
        roots.emplace_back(*s, static_cast<std::size_t>(s.length()));
    }
    if (roots.empty()) roots = XER::Audio::Plugin::DefaultVst3SearchRoots();
    std::string error;
    auto plugins = XER::Audio::Plugin::ScanVst3Plugins(roots, &error);
    auto arr = Nan::New<v8::Array>(plugins.size());
    for (size_t i = 0; i < plugins.size(); ++i) {
        Nan::Set(arr, static_cast<uint32_t>(i), PluginInfoToJS(plugins[i]));
    }
    info.GetReturnValue().Set(arr);
}

NAN_METHOD(DefaultRoots) {
    info.GetReturnValue().Set(
        StrVecToJS(XER::Audio::Plugin::DefaultVst3SearchRoots()));
}

} // namespace

void InitPluginVst3Scanner(v8::Local<v8::Object> ns) {
    Nan::Set(ns, Nan::New("ScanVst3Plugins").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(Scan))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("DefaultVst3SearchRoots").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(DefaultRoots))
                 .ToLocalChecked());
}

} // namespace xer::addon::audio::plugin
