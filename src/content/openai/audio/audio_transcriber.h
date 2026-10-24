#pragma once

#include "../openai_types.h"
#include "../client/openai_client.h"
#include "../../../audio/audio_core/audio_engine.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Content::OpenAI::Audio {

// Audio format for transcription
enum class AudioFormat {
    kWAV,
    kMP3,
    kFLAC,
    kM4A,
    kOGG,
    kWEBM
};

// Audio buffer for processing
struct AudioBuffer {
    std::vector<float> samples;
    int sample_rate;
    int channels;
    float duration_seconds;
};

// Transcription options
struct TranscriptionOptions {
    std::string language;
    std::string prompt;
    float temperature = 0.0f;
    std::string response_format = "json";
    int timestamp_granularities = 0;
    bool enable_vad = false;  // Voice Activity Detection
    bool enable_diarization = false;  // Speaker diarization
};

// Transcription result with timestamps
struct TranscriptionSegment {
    float start_time;
    float end_time;
    std::string text;
    int speaker_id;  // For diarization
    float confidence;
};

struct TranscriptionResult {
    std::string full_text;
    std::string language;
    float duration;
    std::vector<TranscriptionSegment> segments;
    std::map<std::string, std::string> metadata;
};

// Audio transcriber interface
class IAudioTranscriber {
public:
    virtual ~IAudioTranscriber() = default;

    // Transcribe from file
    virtual std::future<TranscriptionResult> TranscribeFile(
        const std::string& file_path,
        const TranscriptionOptions& options = TranscriptionOptions()
    ) = 0;

    // Transcribe from audio buffer
    virtual std::future<TranscriptionResult> TranscribeBuffer(
        const AudioBuffer& buffer,
        const TranscriptionOptions& options = TranscriptionOptions()
    ) = 0;

    // Real-time transcription
    virtual void StartRealTimeTranscription(
        const TranscriptionOptions& options = TranscriptionOptions()
    ) = 0;

    virtual void StopRealTimeTranscription() = 0;

    virtual void FeedAudioChunk(const AudioBuffer& chunk) = 0;

    // Callback for real-time results
    using RealTimeCallback = std::function<void(const TranscriptionSegment&)>;
    virtual void SetRealTimeCallback(RealTimeCallback callback) = 0;

    // Utility functions
    virtual AudioBuffer LoadAudioFile(const std::string& file_path) = 0;
    virtual bool SaveAudioFile(const AudioBuffer& buffer, const std::string& file_path) = 0;
    virtual AudioBuffer ConvertFormat(const AudioBuffer& buffer, AudioFormat target_format) = 0;
};

// OpenAI Whisper transcriber
class WhisperTranscriber : public IAudioTranscriber {
public:
    explicit WhisperTranscriber(std::shared_ptr<Client::OpenAIClient> client);
    ~WhisperTranscriber() override;

    std::future<TranscriptionResult> TranscribeFile(
        const std::string& file_path,
        const TranscriptionOptions& options = TranscriptionOptions()
    ) override;

    std::future<TranscriptionResult> TranscribeBuffer(
        const AudioBuffer& buffer,
        const TranscriptionOptions& options = TranscriptionOptions()
    ) override;

    void StartRealTimeTranscription(
        const TranscriptionOptions& options = TranscriptionOptions()
    ) override;

    void StopRealTimeTranscription() override;

    void FeedAudioChunk(const AudioBuffer& chunk) override;

    void SetRealTimeCallback(RealTimeCallback callback) override;

    AudioBuffer LoadAudioFile(const std::string& file_path) override;
    bool SaveAudioFile(const AudioBuffer& buffer, const std::string& file_path) override;
    AudioBuffer ConvertFormat(const AudioBuffer& buffer, AudioFormat target_format) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Local Whisper transcriber (using XER audio subsystem)
class LocalWhisperTranscriber : public IAudioTranscriber {
public:
    LocalWhisperTranscriber();
    ~LocalWhisperTranscriber() override;

    std::future<TranscriptionResult> TranscribeFile(
        const std::string& file_path,
        const TranscriptionOptions& options = TranscriptionOptions()
    ) override;

    std::future<TranscriptionResult> TranscribeBuffer(
        const AudioBuffer& buffer,
        const TranscriptionOptions& options = TranscriptionOptions()
    ) override;

    void StartRealTimeTranscription(
        const TranscriptionOptions& options = TranscriptionOptions()
    ) override;

    void StopRealTimeTranscription() override;

    void FeedAudioChunk(const AudioBuffer& chunk) override;

    void SetRealTimeCallback(RealTimeCallback callback) override;

    AudioBuffer LoadAudioFile(const std::string& file_path) override;
    bool SaveAudioFile(const AudioBuffer& buffer, const std::string& file_path) override;
    AudioBuffer ConvertFormat(const AudioBuffer& buffer, AudioFormat target_format) override;

private:
    std::shared_ptr<Engine::Audio::Core::AudioEngine> audio_engine_;
    bool real_time_active_;
    RealTimeCallback real_time_callback_;
};

// Audio transcriber factory
class AudioTranscriberFactory {
public:
    static std::unique_ptr<IAudioTranscriber> CreateWhisperTranscriber(
        std::shared_ptr<Client::OpenAIClient> client
    );

    static std::unique_ptr<IAudioTranscriber> CreateLocalTranscriber();

    static std::unique_ptr<IAudioTranscriber> CreateDefault();
};

// Audio utilities
class AudioUtils {
public:
    static AudioFormat DetectFormat(const std::string& file_path);
    static std::string GetFormatExtension(AudioFormat format);
    static AudioBuffer Resample(const AudioBuffer& buffer, int target_sample_rate);
    static AudioBuffer ConvertToMono(const AudioBuffer& buffer);
    static AudioBuffer Normalize(const AudioBuffer& buffer);
    static float CalculateRMS(const AudioBuffer& buffer);
    static std::vector<float> ExtractMFCC(const AudioBuffer& buffer, int num_coefficients = 13);
};

}  // namespace Content::OpenAI::Audio
