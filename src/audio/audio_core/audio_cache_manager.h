/**
 * This file is part of XER, Audio subsystem.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "cache/cache_manager.h"

namespace XER {
namespace Audio {
namespace Core {

// Audio file metadata cache entry
struct AudioFileMetadata {
    int sample_rate;
    int channels;
    int bit_depth;
    size_t duration_samples;
    double duration_seconds;
    std::string format;
};

// Effect preset cache entry
struct EffectPreset {
    std::string effect_name;
    std::vector<uint8_t> parameters;
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;
};

// Analysis result cache entry
struct AnalysisResultCache {
    std::string analysis_type;
    std::string audio_hash;  // Hash of audio content
    std::vector<uint8_t> result_data;
    std::chrono::system_clock::time_point cached_at;
    std::chrono::seconds ttl;
};

class AudioCacheManager {
public:
    static AudioCacheManager& Instance();

    // Audio file metadata cache
    bool CacheAudioMetadata(const std::string& path,
                           const AudioFileMetadata& metadata,
                           std::string* error);
    std::optional<AudioFileMetadata> GetCachedMetadata(const std::string& path,
                                                      std::string* error);
    void InvalidateMetadata(const std::string& path);
    void ClearMetadataCache();

    // Effect preset cache
    bool CacheEffectPreset(const std::string& preset_name,
                         const EffectPreset& preset,
                         std::string* error);
    std::optional<EffectPreset> GetCachedPreset(const std::string& preset_name,
                                                std::string* error);
    void InvalidatePreset(const std::string& preset_name);
    void ClearPresetCache();

    // Analysis result cache
    bool CacheAnalysisResult(const std::string& analysis_type,
                            const std::string& audio_hash,
                            const AnalysisResultCache& result,
                            std::string* error);
    std::optional<AnalysisResultCache> GetCachedAnalysisResult(
        const std::string& analysis_type,
        const std::string& audio_hash,
        std::string* error);
    void InvalidateAnalysisResult(const std::string& analysis_type,
                                 const std::string& audio_hash);
    void ClearAnalysisCache();

    // Cache statistics
    size_t GetMetadataCount() const;
    size_t GetPresetCount() const;
    size_t GetAnalysisCount() const;

    // Configuration
    void SetDefaultMetadataTtl(std::chrono::seconds ttl) { default_metadata_ttl_ = ttl; }
    void SetDefaultPresetTtl(std::chrono::seconds ttl) { default_preset_ttl_ = ttl; }
    void SetDefaultAnalysisTtl(std::chrono::seconds ttl) { default_analysis_ttl_ = ttl; }

    std::chrono::seconds GetDefaultMetadataTtl() const { return default_metadata_ttl_; }
    std::chrono::seconds GetDefaultPresetTtl() const { return default_preset_ttl_; }
    std::chrono::seconds GetDefaultAnalysisTtl() const { return default_analysis_ttl_; }

private:
    AudioCacheManager();
    ~AudioCacheManager() = default;

    AudioCacheManager(const AudioCacheManager&) = delete;
    AudioCacheManager& operator=(const AudioCacheManager&) = delete;

    // Helper to generate cache keys
    std::string MetadataCacheKey(const std::string& path) const;
    std::string PresetCacheKey(const std::string& preset_name) const;
    std::string AnalysisCacheKey(const std::string& analysis_type,
                                const std::string& audio_hash) const;

    // Helper to check if entry is expired
    template<typename EntryType>
    bool IsEntryExpired(const EntryType& entry) const;

    // Helper to serialize/deserialize
    bool SerializeMetadata(const AudioFileMetadata& metadata,
                         std::vector<uint8_t>* out,
                         std::string* error);
    bool DeserializeMetadata(const std::vector<uint8_t>& data,
                           AudioFileMetadata* out,
                           std::string* error);

    bool SerializePreset(const EffectPreset& preset,
                        std::vector<uint8_t>* out,
                        std::string* error);
    bool DeserializePreset(const std::vector<uint8_t>& data,
                          EffectPreset* out,
                          std::string* error);

    bool SerializeAnalysisResult(const AnalysisResultCache& result,
                                std::vector<uint8_t>* out,
                                std::string* error);
    bool DeserializeAnalysisResult(const std::vector<uint8_t>& data,
                                  AnalysisResultCache* out,
                                  std::string* error);

    XER::Cache::CacheManager& cache_manager_;
    std::chrono::seconds default_metadata_ttl_ = std::chrono::seconds(3600);   // 1 hour
    std::chrono::seconds default_preset_ttl_ = std::chrono::seconds(86400);     // 24 hours
    std::chrono::seconds default_analysis_ttl_ = std::chrono::seconds(1800);    // 30 minutes
};

}  // namespace Core
}  // namespace Audio
}  // namespace XER