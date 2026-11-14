// xer.audio.core.CacheManager — AudioCacheManager binding (singleton).

#include "cache_manager.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "audio/audio_core/audio_cache_manager.h"

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioCacheManager;
using XER::Audio::Core::AudioFileMetadata;
using XER::Audio::Core::EffectPreset;
using XER::Audio::Core::AnalysisResultCache;

CacheManagerWrap::CacheManagerWrap() = default;
CacheManagerWrap::~CacheManagerWrap() = default;

void CacheManagerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new CacheManagerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

namespace {

AudioCacheManager& Cache() { return AudioCacheManager::Instance(); }

// --- JS <-> struct helpers ---------------------------------------------------

bool MetadataFromJS(v8::Local<v8::Value> v, AudioFileMetadata* m) {
    if (v.IsEmpty() || !v->IsObject()) return false;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    auto prop = [&](const char* k, v8::Local<v8::Value>* dst) {
        return Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(dst);
    };
    v8::Local<v8::Value> x;
    if (prop("sampleRate", &x) && x->IsNumber())
        m->sample_rate = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("channels", &x) && x->IsNumber())
        m->channels = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("bitDepth", &x) && x->IsNumber())
        m->bit_depth = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("durationSamples", &x) && x->IsNumber())
        m->duration_samples =
            static_cast<size_t>(x.As<v8::Number>()->Value());
    if (prop("durationSeconds", &x) && x->IsNumber())
        m->duration_seconds = x.As<v8::Number>()->Value();
    if (prop("format", &x) && x->IsString()) {
        Nan::Utf8String s(x);
        m->format = std::string(*s, static_cast<std::size_t>(s.length()));
    }
    return true;
}

v8::Local<v8::Value> MetadataToJS(const AudioFileMetadata& m) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("sampleRate").ToLocalChecked(),
             Nan::New(m.sample_rate));
    Nan::Set(o, Nan::New("channels").ToLocalChecked(), Nan::New(m.channels));
    Nan::Set(o, Nan::New("bitDepth").ToLocalChecked(), Nan::New(m.bit_depth));
    Nan::Set(o, Nan::New("durationSamples").ToLocalChecked(),
             Nan::New(static_cast<double>(m.duration_samples)));
    Nan::Set(o, Nan::New("durationSeconds").ToLocalChecked(),
             Nan::New(m.duration_seconds));
    Nan::Set(o, Nan::New("format").ToLocalChecked(),
             Nan::New(m.format).ToLocalChecked());
    return o;
}

bool PresetFromJS(v8::Local<v8::Value> v, EffectPreset* p) {
    if (v.IsEmpty() || !v->IsObject()) return false;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    auto prop = [&](const char* k, v8::Local<v8::Value>* dst) {
        return Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(dst);
    };
    v8::Local<v8::Value> x;
    if (prop("effectName", &x) && x->IsString()) {
        Nan::Utf8String s(x);
        p->effect_name = std::string(*s, static_cast<std::size_t>(s.length()));
    }
    if (prop("parameters", &x)) U8FromJS(x, &p->parameters);
    if (prop("ttlSeconds", &x) && x->IsNumber())
        p->ttl = std::chrono::seconds(
            static_cast<long long>(x.As<v8::Number>()->Value()));
    p->cached_at = std::chrono::system_clock::now();
    return true;
}

v8::Local<v8::Value> PresetToJS(const EffectPreset& p) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("effectName").ToLocalChecked(),
             Nan::New(p.effect_name).ToLocalChecked());
    Nan::Set(o, Nan::New("parameters").ToLocalChecked(),
             U8ToJS(p.parameters));
    Nan::Set(o, Nan::New("ttlSeconds").ToLocalChecked(),
             Nan::New(static_cast<double>(p.ttl.count())));
    return o;
}

bool AnalysisFromJS(v8::Local<v8::Value> v, AnalysisResultCache* r) {
    if (v.IsEmpty() || !v->IsObject()) return false;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    auto prop = [&](const char* k, v8::Local<v8::Value>* dst) {
        return Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(dst);
    };
    v8::Local<v8::Value> x;
    if (prop("analysisType", &x) && x->IsString()) {
        Nan::Utf8String s(x);
        r->analysis_type =
            std::string(*s, static_cast<std::size_t>(s.length()));
    }
    if (prop("resultData", &x)) U8FromJS(x, &r->result_data);
    if (prop("ttlSeconds", &x) && x->IsNumber())
        r->ttl = std::chrono::seconds(
            static_cast<long long>(x.As<v8::Number>()->Value()));
    r->cached_at = std::chrono::system_clock::now();
    return true;
}

v8::Local<v8::Value> AnalysisToJS(const AnalysisResultCache& r) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("analysisType").ToLocalChecked(),
             Nan::New(r.analysis_type).ToLocalChecked());
    Nan::Set(o, Nan::New("audioHash").ToLocalChecked(),
             Nan::New(r.audio_hash).ToLocalChecked());
    Nan::Set(o, Nan::New("resultData").ToLocalChecked(), U8ToJS(r.result_data));
    Nan::Set(o, Nan::New("ttlSeconds").ToLocalChecked(),
             Nan::New(static_cast<double>(r.ttl.count())));
    return o;
}

std::string Err(std::string* e) {
    return e && !e->empty() ? *e : std::string("cache error");
}

} // namespace

// --- metadata ---------------------------------------------------------------

void CacheManagerWrap::CacheAudioMetadata(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 2) return;
    AudioFileMetadata m;
    if (!MetadataFromJS(info[1], &m)) return;
    std::string path = StrArg(info, 0);
    std::string err;
    info.GetReturnValue().Set(Nan::New(
        Cache().CacheAudioMetadata(path, m, &err)));
}

void CacheManagerWrap::GetCachedMetadata(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    std::string err;
    auto m = Cache().GetCachedMetadata(StrArg(info, 0), &err);
    if (!m) {
        if (!err.empty()) Nan::ThrowError(Err(&err).c_str());
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(MetadataToJS(*m));
}

void CacheManagerWrap::InvalidateMetadata(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Cache().InvalidateMetadata(StrArg(info, 0));
}

void CacheManagerWrap::ClearMetadataCache(
    const Nan::FunctionCallbackInfo<v8::Value>&) {
    Cache().ClearMetadataCache();
}

// --- presets -----------------------------------------------------------------

void CacheManagerWrap::CacheEffectPreset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 2) return;
    EffectPreset p;
    if (!PresetFromJS(info[1], &p)) return;
    std::string err;
    info.GetReturnValue().Set(Nan::New(
        Cache().CacheEffectPreset(StrArg(info, 0), p, &err)));
}

void CacheManagerWrap::GetCachedPreset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    std::string err;
    auto p = Cache().GetCachedPreset(StrArg(info, 0), &err);
    if (!p) {
        if (!err.empty()) Nan::ThrowError(Err(&err).c_str());
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(PresetToJS(*p));
}

void CacheManagerWrap::InvalidatePreset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Cache().InvalidatePreset(StrArg(info, 0));
}

void CacheManagerWrap::ClearPresetCache(
    const Nan::FunctionCallbackInfo<v8::Value>&) {
    Cache().ClearPresetCache();
}

// --- analysis ----------------------------------------------------------------

void CacheManagerWrap::CacheAnalysisResult(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 3) return;
    AnalysisResultCache r;
    if (!AnalysisFromJS(info[2], &r)) return;
    std::string err;
    info.GetReturnValue().Set(Nan::New(Cache().CacheAnalysisResult(
        StrArg(info, 0), StrArg(info, 1), r, &err)));
}

void CacheManagerWrap::GetCachedAnalysisResult(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    std::string err;
    auto r = Cache().GetCachedAnalysisResult(
        StrArg(info, 0), StrArg(info, 1), &err);
    if (!r) {
        if (!err.empty()) Nan::ThrowError(Err(&err).c_str());
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(AnalysisToJS(*r));
}

void CacheManagerWrap::InvalidateAnalysisResult(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Cache().InvalidateAnalysisResult(StrArg(info, 0), StrArg(info, 1));
}

void CacheManagerWrap::ClearAnalysisCache(
    const Nan::FunctionCallbackInfo<v8::Value>&) {
    Cache().ClearAnalysisCache();
}

// --- stats + TTL -------------------------------------------------------------

void CacheManagerWrap::GetMetadataCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(Cache().GetMetadataCount())));
}

void CacheManagerWrap::GetPresetCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(Cache().GetPresetCount())));
}

void CacheManagerWrap::GetAnalysisCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(Cache().GetAnalysisCount())));
}

void CacheManagerWrap::SetDefaultMetadataTtl(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Cache().SetDefaultMetadataTtl(std::chrono::seconds(IntArg(info, 0, 0)));
}

void CacheManagerWrap::SetDefaultPresetTtl(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Cache().SetDefaultPresetTtl(std::chrono::seconds(IntArg(info, 0, 0)));
}

void CacheManagerWrap::SetDefaultAnalysisTtl(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Cache().SetDefaultAnalysisTtl(std::chrono::seconds(IntArg(info, 0, 0)));
}

void CacheManagerWrap::GetDefaultMetadataTtl(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(Nan::New(static_cast<double>(
        Cache().GetDefaultMetadataTtl().count())));
}

void CacheManagerWrap::GetDefaultPresetTtl(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(Nan::New(static_cast<double>(
        Cache().GetDefaultPresetTtl().count())));
}

void CacheManagerWrap::GetDefaultAnalysisTtl(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(Nan::New(static_cast<double>(
        Cache().GetDefaultAnalysisTtl().count())));
}


void InitCoreCacheManager(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "CacheManager", CacheManagerWrap::New,
        {{"CacheAudioMetadata", CacheManagerWrap::CacheAudioMetadata},
         {"GetCachedMetadata", CacheManagerWrap::GetCachedMetadata},
         {"InvalidateMetadata", CacheManagerWrap::InvalidateMetadata},
         {"ClearMetadataCache", CacheManagerWrap::ClearMetadataCache},
         {"CacheEffectPreset", CacheManagerWrap::CacheEffectPreset},
         {"GetCachedPreset", CacheManagerWrap::GetCachedPreset},
         {"InvalidatePreset", CacheManagerWrap::InvalidatePreset},
         {"ClearPresetCache", CacheManagerWrap::ClearPresetCache},
         {"CacheAnalysisResult", CacheManagerWrap::CacheAnalysisResult},
         {"GetCachedAnalysisResult", CacheManagerWrap::GetCachedAnalysisResult},
         {"InvalidateAnalysisResult", CacheManagerWrap::InvalidateAnalysisResult},
         {"ClearAnalysisCache", CacheManagerWrap::ClearAnalysisCache},
         {"GetMetadataCount", CacheManagerWrap::GetMetadataCount},
         {"GetPresetCount", CacheManagerWrap::GetPresetCount},
         {"GetAnalysisCount", CacheManagerWrap::GetAnalysisCount},
         {"SetDefaultMetadataTtl", CacheManagerWrap::SetDefaultMetadataTtl},
         {"SetDefaultPresetTtl", CacheManagerWrap::SetDefaultPresetTtl},
         {"SetDefaultAnalysisTtl", CacheManagerWrap::SetDefaultAnalysisTtl},
         {"GetDefaultMetadataTtl", CacheManagerWrap::GetDefaultMetadataTtl},
         {"GetDefaultPresetTtl", CacheManagerWrap::GetDefaultPresetTtl},
         {"GetDefaultAnalysisTtl", CacheManagerWrap::GetDefaultAnalysisTtl}});
}

} // namespace xer::addon::audio::core
