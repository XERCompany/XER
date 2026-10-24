#ifndef XER_UI_WIDGETS_OPENAI_TEXT_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_TEXT_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <vector>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * TextOperationUI - UI representation of text processing operation
 */
enum class TextOperationUI {
    kSummarize,
    kTranslate,
    kExtract,
    kRewrite,
    kClassify,
    kSentiment,
    kKeyword,
    kEntity
};

/**
 * TextProcessingWidget - UI widget for text processing
 *
 * Provides:
 * - Text input area
 * - Operation selector
 * - Language selection (for translation)
 * - Style selection (for rewrite)
 * - Result display
 * - Batch processing
 * - Export functionality
 */
class TextProcessingWidget : public Widget {
public:
    TextProcessingWidget();
    ~TextProcessingWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    
    void ProcessText(const std::string& text, TextOperationUI operation);
    void ClearResults();
    
    // Configuration
    void SetTargetLanguage(const std::string& language);
    void SetStyle(const std::string& style);
    void SetMaxOutputLength(int length);
    
    // Callbacks
    using ResultCallback = std::function<void(const std::string& result)>;
    void OnProcessingComplete(ResultCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * SentimentAnalysisWidget - Widget for sentiment analysis visualization
 */
class SentimentAnalysisWidget : public Widget {
public:
    SentimentAnalysisWidget();
    ~SentimentAnalysisWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetSentiment(float score, const std::string& label);
    void SetSentimentHistory(const std::vector<float>& history);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * KeywordExtractionWidget - Widget for keyword extraction display
 */
class KeywordExtractionWidget : public Widget {
public:
    KeywordExtractionWidget();
    ~KeywordExtractionWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetKeywords(const std::vector<std::string>& keywords);
    void SetKeywordWeights(const std::map<std::string, float>& weights);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_TEXT_WIDGET_H
