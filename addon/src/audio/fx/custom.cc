// xer.audio.fx.custom — custom effect package runner binding.

#include "custom.h"

#include <string>
#include <vector>

#include "audio/effects_rack/custom_effect.h"
#include "audio/effects_rack/custom_effect_struct.h"

namespace xer::addon::audio::fx {

using XER::Audio::FX::CustomEffectAlgorithm;
using XER::Audio::FX::CustomEffectBackend;
using XER::Audio::FX::CustomEffectCurvePoint;
using XER::Audio::FX::CustomEffectNode;
using XER::Audio::FX::CustomEffectPackage;
using XER::Audio::FX::CustomEffectParameter;
using XER::Audio::FX::CustomEffectRenderConfig;
using XER::Audio::FX::CustomEffectReport;

namespace {

// --- JS <-> struct helpers ---------------------------------------------------

std::string PropStr(v8::Local<v8::Object> o, const char* key) {
    v8::Local<v8::Value> x;
    if (!Nan::Get(o, Nan::New(key).ToLocalChecked()).ToLocal(&x) ||
        !x->IsString()) {
        return {};
    }
    Nan::Utf8String s(x);
    return std::string(*s, static_cast<std::size_t>(s.length()));
}

double PropNum(v8::Local<v8::Object> o, const char* key, double fallback) {
    v8::Local<v8::Value> x;
    if (Nan::Get(o, Nan::New(key).ToLocalChecked()).ToLocal(&x) &&
        x->IsNumber()) {
        return x.As<v8::Number>()->Value();
    }
    return fallback;
}

bool PropBool(v8::Local<v8::Object> o, const char* key, bool fallback) {
    v8::Local<v8::Value> x;
    if (Nan::Get(o, Nan::New(key).ToLocalChecked()).ToLocal(&x)) {
        if (x->IsBoolean()) return x.As<v8::Boolean>()->Value();
        if (x->IsNumber()) return x.As<v8::Number>()->Value() != 0.0;
    }
    return fallback;
}

// backend/algorithm accept a string name (native parser) or an int ordinal.
CustomEffectBackend BackendFromJS(v8::Local<v8::Object> o) {
    v8::Local<v8::Value> x;
    if (!Nan::Get(o, Nan::New("backend").ToLocalChecked()).ToLocal(&x)) {
        return CustomEffectBackend::kBuiltinAlgorithm;
    }
    if (x->IsString()) {
        CustomEffectBackend b = CustomEffectBackend::kBuiltinAlgorithm;
        Nan::Utf8String s(x);
        if (XER::Audio::FX::ParseCustomEffectBackend(
                std::string(*s, static_cast<std::size_t>(s.length())),
                &b)) {
            return b;
        }
        return CustomEffectBackend::kBuiltinAlgorithm;
    }
    if (x->IsNumber()) {
        return static_cast<CustomEffectBackend>(
            static_cast<int>(x.As<v8::Number>()->Value()));
    }
    return CustomEffectBackend::kBuiltinAlgorithm;
}

CustomEffectAlgorithm AlgorithmFromJS(v8::Local<v8::Object> o) {
    v8::Local<v8::Value> x;
    if (!Nan::Get(o, Nan::New("algorithm").ToLocalChecked()).ToLocal(&x)) {
        return CustomEffectAlgorithm::kUnknown;
    }
    if (x->IsString()) {
        CustomEffectAlgorithm a = CustomEffectAlgorithm::kUnknown;
        Nan::Utf8String s(x);
        if (XER::Audio::FX::ParseCustomEffectAlgorithm(
                std::string(*s, static_cast<std::size_t>(s.length())), &a)) {
            return a;
        }
        return CustomEffectAlgorithm::kUnknown;
    }
    if (x->IsNumber()) {
        return static_cast<CustomEffectAlgorithm>(
            static_cast<int>(x.As<v8::Number>()->Value()));
    }
    return CustomEffectAlgorithm::kUnknown;
}

CustomEffectNode NodeFromJS(v8::Local<v8::Value> v) {
    CustomEffectNode n;
    if (v.IsEmpty() || !v->IsObject()) return n;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    n.label = PropStr(o, "label");
    n.backend = BackendFromJS(o);
    n.algorithm = AlgorithmFromJS(o);
    n.stage_index = static_cast<uint32_t>(PropNum(o, "stageIndex", 0));
    n.plugin_reference = PropStr(o, "pluginReference");
    n.enabled = PropBool(o, "enabled", true);

    v8::Local<v8::Value> x;
    if (Nan::Get(o, Nan::New("parameters").ToLocalChecked()).ToLocal(&x) &&
        x->IsArray()) {
        auto arr = x.As<v8::Array>();
        for (uint32_t i = 0; i < arr->Length(); ++i) {
            v8::Local<v8::Value> e;
            if (!arr->Get(Nan::GetCurrentContext(), i).ToLocal(&e) ||
                !e->IsObject()) {
                continue;
            }
            v8::Local<v8::Object> po = e.As<v8::Object>();
            CustomEffectParameter p;
            p.name = PropStr(po, "name");
            p.value = static_cast<float>(PropNum(po, "value", 0.0));
            n.parameters.push_back(std::move(p));
        }
    }
    if (Nan::Get(o, Nan::New("mixCurve").ToLocalChecked()).ToLocal(&x) &&
        x->IsArray()) {
        auto arr = x.As<v8::Array>();
        for (uint32_t i = 0; i < arr->Length(); ++i) {
            v8::Local<v8::Value> e;
            if (!arr->Get(Nan::GetCurrentContext(), i).ToLocal(&e) ||
                !e->IsObject()) {
                continue;
            }
            v8::Local<v8::Object> co = e.As<v8::Object>();
            CustomEffectCurvePoint p;
            p.frame_index = static_cast<size_t>(PropNum(co, "frameIndex", 0));
            p.value = static_cast<float>(PropNum(co, "value", 0.0));
            n.mix_curve.push_back(std::move(p));
        }
    }
    return n;
}

CustomEffectPackage PackageFromJS(v8::Local<v8::Value> v) {
    CustomEffectPackage pkg;
    if (v.IsEmpty() || !v->IsObject()) return pkg;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    pkg.label = PropStr(o, "label");

    v8::Local<v8::Value> rc;
    if (Nan::Get(o, Nan::New("renderConfig").ToLocalChecked()).ToLocal(&rc) &&
        rc->IsObject()) {
        v8::Local<v8::Object> rco = rc.As<v8::Object>();
        pkg.render_config.block_size = static_cast<size_t>(
            PropNum(rco, "blockSize", 1024));
        pkg.render_config.worker_count = static_cast<uint32_t>(
            PropNum(rco, "workerCount", 0));
        pkg.render_config.enable_multicore_render =
            PropBool(rco, "enableMulticoreRender", true);
    }

    v8::Local<v8::Value> x;
    if (Nan::Get(o, Nan::New("nodes").ToLocalChecked()).ToLocal(&x) &&
        x->IsArray()) {
        auto arr = x.As<v8::Array>();
        for (uint32_t i = 0; i < arr->Length(); ++i) {
            v8::Local<v8::Value> e;
            if (arr->Get(Nan::GetCurrentContext(), i).ToLocal(&e)) {
                pkg.nodes.push_back(NodeFromJS(e));
            }
        }
    }
    return pkg;
}

v8::Local<v8::Object> ReportToJS(const CustomEffectReport& r) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("label").ToLocalChecked(),
             Nan::New(r.label).ToLocalChecked());
    Nan::Set(o, Nan::New("nodeCount").ToLocalChecked(),
             Nan::New(static_cast<double>(r.node_count)));
    Nan::Set(o, Nan::New("stageCount").ToLocalChecked(),
             Nan::New(static_cast<double>(r.stage_count)));
    Nan::Set(o, Nan::New("channelCount").ToLocalChecked(),
             Nan::New(static_cast<double>(r.channel_count)));
    Nan::Set(o, Nan::New("workerCountUsed").ToLocalChecked(),
             Nan::New(static_cast<double>(r.worker_count_used)));
    Nan::Set(o, Nan::New("peak").ToLocalChecked(), Nan::New(r.peak));
    Nan::Set(o, Nan::New("rms").ToLocalChecked(), Nan::New(r.rms));
    Nan::Set(o, Nan::New("nodeReports").ToLocalChecked(),
             StrVecToJS(r.node_reports));
    Nan::Set(o, Nan::New("stageReports").ToLocalChecked(),
             StrVecToJS(r.stage_reports));
    return o;
}

v8::Local<v8::Value> RunResult(
    bool ok, const std::vector<float>& output,
    const CustomEffectReport& report, const std::string& error) {
    if (!ok) {
        Nan::ThrowError(("custom effect failed: " + error).c_str());
        return Nan::Undefined();
    }
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("output").ToLocalChecked(), F32ToJS(output));
    Nan::Set(o, Nan::New("report").ToLocalChecked(), ReportToJS(report));
    return o;
}

} // namespace

// --- functions ----------------------------------------------------------------

NAN_METHOD(List) {
    info.GetReturnValue().Set(
        StrVecToJS(XER::Audio::FX::ListAvailableCustomEffects()));
}

NAN_METHOD(RenderExample) {
    const float rate = static_cast<float>(NumArg(info, 0, 44100.0));
    std::vector<float> in;
    if (info.Length() < 2 || !F32FromJS(info[1], &in)) {
        Nan::ThrowError("custom.RenderExample expects (sampleRate, samples)");
        return;
    }
    std::vector<float> out;
    CustomEffectReport report;
    std::string error;
    const bool ok = XER::Audio::FX::RenderCustomEffectExample(
        rate, in, &out, &report, &error);
    info.GetReturnValue().Set(RunResult(ok, out, report, error));
}

NAN_METHOD(Run) {
    if (info.Length() < 3) {
        Nan::ThrowError("custom.Run expects (package, sampleRate, samples)");
        return;
    }
    const CustomEffectPackage pkg = PackageFromJS(info[0]);
    const float rate = static_cast<float>(NumArg(info, 1, 44100.0));
    std::vector<float> in;
    if (!F32FromJS(info[2], &in)) {
        Nan::ThrowError("custom.Run expects Float32Array samples");
        return;
    }
    std::vector<float> out;
    CustomEffectReport report;
    std::string error;
    const bool ok = XER::Audio::FX::RunCustomEffectPackage(
        pkg, rate, in, &out, &report, &error);
    info.GetReturnValue().Set(RunResult(ok, out, report, error));
}

NAN_METHOD(RunInterleaved) {
    if (info.Length() < 4) {
        Nan::ThrowError(
            "custom.RunInterleaved expects (package, sampleRate, samples,"
            " channels)");
        return;
    }
    const CustomEffectPackage pkg = PackageFromJS(info[0]);
    const float rate = static_cast<float>(NumArg(info, 1, 44100.0));
    std::vector<float> in;
    if (!F32FromJS(info[2], &in)) {
        Nan::ThrowError("custom.RunInterleaved expects Float32Array samples");
        return;
    }
    const int channels = IntArg(info, 3, 2);
    std::vector<float> out;
    CustomEffectReport report;
    std::string error;
    const bool ok = XER::Audio::FX::RunCustomEffectPackageInterleaved(
        pkg, rate, in, channels, &out, &report, &error);
    info.GetReturnValue().Set(RunResult(ok, out, report, error));
}


void InitFxCustom(v8::Local<v8::Object> ns) {
    Nan::Set(ns, Nan::New("List").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(List))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("RenderExample").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(RenderExample))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("Run").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(Run))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("RunInterleaved").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(RunInterleaved))
                 .ToLocalChecked());
}

} // namespace xer::addon::audio::fx
