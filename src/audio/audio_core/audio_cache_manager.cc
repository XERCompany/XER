/**
 * This file is part of XER, Audio subsystem.
 * Copyright (C) 2026 Yoshi A.
 */
#include "audio/audio_core/audio_cache_manager.h"

#include <sstream>

namespace Engine {
namespace Audio {
namespace Core {

AudioCacheManager::AudioCacheManager()
    : cache_manager_(Engine::Cache::CacheManager::Instance()) {}

AudioCacheManager& AudioCacheManager::Instance() {
    static AudioCacheManager instance;
    return instance;
}

std::string AudioCacheManager::MetadataCacheKey(const std::string& path) const {
    return "audio_metadata:" + path;
}

std::string AudioCacheManager::PresetCacheKey(const std::string& preset_name) const {
    return "audio_preset:" + preset_name;
}

std::string AudioCacheManager::AnalysisCacheKey(const std::string& analysis_type,
                                               const std::string& audio_hash) const {
    return "audio_analysis:" + analysis_type + ":" + audio_hash;
}

template<typename EntryType>
bool AudioCacheManager::IsEntryExpired(const EntryType& entry) const {
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - entry.cached_at);
    return age > entry.ttl;
}

bool AudioCacheManager::SerializeMetadata(const AudioFileMetadata& metadata,
                                         std::vector<uint8_t>* out,
                                         std::string* error) {
    std::ostringstream oss;
    oss << metadata.sample_rate << " "
        << metadata.channels << " "
        << metadata.bit_depth << " "
        << metadata.duration_samples << " "
        << metadata.duration_seconds << " "
        << metadata.format;
    std::string str = oss.str();
    out->assign(str.begin(), str.end());
    return true;
}

bool AudioCacheManager::DeserializeMetadata(const std::vector<uint8_t>& data,
                                           AudioFileMetadata* out,
                                           std::string* error) {
    std::string str(data.begin(), data.end());
    std::istringstream iss(str);
    iss >> out->sample_rate
        >> out->channels
        >> out->bit_depth
        >> out->duration_samples
        >> out->duration_seconds
        >> out->format;
    return !iss.fail();
}

bool AudioCacheManager::SerializePreset(const EffectPreset& preset,
                                        std::vector<uint8_t>* out,
                                        std::string* error) {
    // Simple serialization: store raw parameters
    out->assign(preset.parameters.begin(), preset.parameters.end());
    return true;
}

bool AudioCacheManager::DeserializePreset(const std::vector<uint8_t>& data,
                                          EffectPreset* out,
                                          std::string* error) {
    out->parameters.assign(data.begin(), data.end());
    return true;
}

bool AudioCacheManager::SerializeAnalysisResult(const AnalysisResultCache& result,
                                                std::vector<uint8_t>* out,
                                                std::string* error) {
    // Simple serialization: store raw result data
    out->assign(result.result_data.begin(), result.result_data.end());
    return true;
}

bool AudioCacheManager::DeserializeAnalysisResult(const std::vector<uint8_t>& data,
                                                  AnalysisResultCache* out,
                                                  std::string* error) {
    out->result_data.assign(data.begin(), data.end());
    return true;
}

bool AudioCacheManager::CacheAudioMetadata(const std::string& path,
                                           const AudioFileMetadata& metadata,
                                           std::string* error) {
    std::vector<uint8_t> data;
    if (!SerializeMetadata(metadata, &data, error)) {
        return false;
    }

    return cache_manager_.WritePersistentBinary(
        "audio", MetadataCacheKey(path), "metadata", data, error);
}

std::optional<AudioFileMetadata> AudioCacheManager::GetCachedMetadata(
    const std::string& path,
    std::string* error) {
    std::vector<uint8_t> data;
    if (!cache_manager_.ReadPersistentBinary(
            "audio", MetadataCacheKey(path), "metadata", &data, error)) {
        return std::nullopt;
    }

    AudioFileMetadata metadata;
    if (!DeserializeMetadata(data, &metadata, error)) {
        return std::nullopt;
    }

    return metadata;
}

void AudioCacheManager::InvalidateMetadata(const std::string& path) {
    // TODO: Implement cache invalidation
    // This requires adding a remove method to CacheManager
}

void AudioCacheManager::ClearMetadataCache() {
    // TODO: Implement pattern-based deletion
}

bool AudioCacheManager::CacheEffectPreset(const std::string& preset_name,
                                         const EffectPreset& preset,
                                         std::string* error) {
    std::vector<uint8_t> data;
    if (!SerializePreset(preset, &data, error)) {
        return false;
    }

    return cache_manager_.WritePersistentBinary(
        "audio", PresetCacheKey(preset_name), "preset", data, error);
}

std::optional<EffectPreset> AudioCacheManager::GetCachedPreset(
    const std::string& preset_name,
    std::string* error) {
    std::vector<uint8_t> data;
    if (!cache_manager_.ReadPersistentBinary(
            "audio", PresetCacheKey(preset_name), "preset", &data, error)) {
        return std::nullopt;
    }

    EffectPreset preset_out;
    if (!DeserializePreset(data, &preset_out, error)) {
        return std::nullopt;
    }

    preset_out.cached_at = std::chrono::system_clock::now();
    preset_out.ttl = default_preset_ttl_;

    if (IsEntryExpired(preset_out)) {
        InvalidatePreset(preset_name);
        return std::nullopt;
    }

    return preset_out;
}

void AudioCacheManager::InvalidatePreset(const std::string& preset_name) {
    // TODO: Implement cache invalidation
    // This requires adding a remove method to CacheManager
}

void AudioCacheManager::ClearPresetCache() {
    // TODO: Implement pattern-based deletion
}

bool AudioCacheManager::CacheAnalysisResult(const std::string& analysis_type,
                                            const std::string& audio_hash,
                                            const AnalysisResultCache& result,
                                            std::string* error) {
    std::vector<uint8_t> data;
    if (!SerializeAnalysisResult(result, &data, error)) {
        return false;
    }

    return cache_manager_.WritePersistentBinary(
        "audio", AnalysisCacheKey(analysis_type, audio_hash), "analysis", data, error);
}

std::optional<AnalysisResultCache> AudioCacheManager::GetCachedAnalysisResult(
    const std::string& analysis_type,
    const std::string& audio_hash,
    std::string* error) {
    std::vector<uint8_t> data;
    if (!cache_manager_.ReadPersistentBinary(
            "audio", AnalysisCacheKey(analysis_type, audio_hash), "analysis", &data, error)) {
        return std::nullopt;
    }

    AnalysisResultCache result_out;
    if (!DeserializeAnalysisResult(data, &result_out, error)) {
        return std::nullopt;
    }

    result_out.cached_at = std::chrono::system_clock::now();
    result_out.ttl = default_analysis_ttl_;

    if (IsEntryExpired(result_out)) {
        InvalidateAnalysisResult(analysis_type, audio_hash);
        return std::nullopt;
    }

    return result_out;
}

void AudioCacheManager::InvalidateAnalysisResult(const std::string& analysis_type,
                                                 const std::string& audio_hash) {
    // TODO: Implement cache invalidation
    // This requires adding a remove method to CacheManager
}

void AudioCacheManager::ClearAnalysisCache() {
    // TODO: Implement pattern-based deletion
}

size_t AudioCacheManager::GetMetadataCount() const {
    // TODO: Implement count
    return 0;
}

size_t AudioCacheManager::GetPresetCount() const {
    // TODO: Implement count
    return 0;
}

size_t AudioCacheManager::GetAnalysisCount() const {
    // TODO: Implement count
    return 0;
}

}  // namespace Core
}  // namespace Audio
}  // namespace Engine
