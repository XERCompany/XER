#pragma once

// xer.audio.core.CacheManager — wraps XER::Audio::Core::AudioCacheManager
// (process-wide singleton: metadata / preset / analysis caches with TTL).

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class CacheManagerWrap : public Nan::ObjectWrap {
public:
    explicit CacheManagerWrap();
    ~CacheManagerWrap() override;

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);

    // --- metadata cache ---
    static void CacheAudioMetadata(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCachedMetadata(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void InvalidateMetadata(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ClearMetadataCache(
        const Nan::FunctionCallbackInfo<v8::Value>& info);

    // --- effect preset cache ---
    static void CacheEffectPreset(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCachedPreset(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void InvalidatePreset(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ClearPresetCache(
        const Nan::FunctionCallbackInfo<v8::Value>& info);

    // --- analysis result cache ---
    static void CacheAnalysisResult(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCachedAnalysisResult(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void InvalidateAnalysisResult(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ClearAnalysisCache(
        const Nan::FunctionCallbackInfo<v8::Value>& info);

    // --- stats + TTL ---
    static void GetMetadataCount(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPresetCount(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAnalysisCount(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDefaultMetadataTtl(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDefaultPresetTtl(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDefaultAnalysisTtl(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDefaultMetadataTtl(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDefaultPresetTtl(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDefaultAnalysisTtl(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
};

void InitCoreCacheManager(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
