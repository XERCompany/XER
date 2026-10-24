#ifndef XER_UI_WIDGETS_OPENAI_AUDIO_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_AUDIO_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <vector>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * TranscriptionSegmentUI - UI representation of transcription segment
 */
struct TranscriptionSegmentUI {
    float start_time;
    float end_time;
    std::string text;
    int speaker_id;
    float confidence;
};

/**
 * AudioTranscriptionWidget - UI widget for audio transcription
 *
 * Provides:
 * - Audio file selection
 * - Language selection
 * - Real-time transcription display
 * - Timestamped segments
 * - Speaker diarization visualization
 * - Export transcription
 * - Audio waveform visualization
 */
class AudioTranscriptionWidget : public Widget {
public:
    AudioTranscriptionWidget();
    ~AudioTranscriptionWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    
    void LoadAudioFile(const std::string& file_path);
    void StartTranscription();
    void StopTranscription();
    void ClearTranscription();
    
    // Real-time transcription
    void EnableRealTimeTranscription(bool enable);
    void FeedAudioChunk(const Content::OpenAI::Audio::AudioBuffer& chunk);
    
    // Export
    void ExportTranscription(const std::string& path, const std::string& format = "txt");
    
    // Callbacks
    using TranscriptionCallback = std::function<void(const TranscriptionSegmentUI&)>;
    void OnTranscriptionSegment(TranscriptionCallback callback);
    
    using CompletionCallback = std::function<void(const std::string& full_text)>;
    void OnTranscriptionComplete(CompletionCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * WaveformWidget - Audio waveform visualization
 */
class WaveformWidget : public Widget {
public:
    WaveformWidget();
    ~WaveformWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetAudioBuffer(const Content::OpenAI::Audio::AudioBuffer& buffer);
    void SetPlaybackPosition(float position);  // 0.0 to 1.0
    void SetHighlightRegion(float start, float end);
    
    // Styling
    void SetWaveformColor(const std::string& color);
    void SetBackgroundColor(const std::string& color);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_AUDIO_WIDGET_H
