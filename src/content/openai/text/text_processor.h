#pragma once

#include "../openai_types.h"
#include "../client/openai_client.h"
#include <range/v3/all.hpp>
#include <absl/strings/string_view.h>
#include <absl/strings/str_split.h>
#include <absl/strings/str_join.h>
#include <absl/strings/str_replace.h>
#include <re2/re2.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Content::OpenAI::Text {

// Text processing operation
enum class TextOperation {
    kSummarize,
    kTranslate,
    kExtract,
    kRewrite,
    kClassify,
    kSentiment,
    kKeyword,
    kEntity
};

// Text processing parameters
struct TextProcessingParams {
    TextOperation operation;
    std::string input_text;
    std::string target_language;  // for translation
    int max_length = 0;
    float temperature = 0.7f;
    std::string style;  // for rewrite
    std::vector<std::string> categories;  // for classification
};

// Text analysis result
struct TextAnalysisResult {
    std::string processed_text;
    std::string summary;
    std::string translation;
    std::vector<std::string> extracted_entities;
    std::vector<std::string> keywords;
    float sentiment_score;  // -1.0 to 1.0
    std::string sentiment_label;  // positive, negative, neutral
    std::string classification;
    std::map<std::string, float> confidence_scores;
    int word_count;
    int sentence_count;
    int character_count;
};

// Text processor interface
class ITextProcessor {
public:
    virtual ~ITextProcessor() = default;

    // Core text operations
    virtual std::future<TextAnalysisResult> ProcessText(
        const TextProcessingParams& params
    ) = 0;

    // Specific operations
    virtual std::future<std::string> Summarize(
        const std::string& text,
        int max_length = 100
    ) = 0;

    virtual std::future<std::string> Translate(
        const std::string& text,
        const std::string& target_language
    ) = 0;

    virtual std::future<std::vector<std::string>> ExtractEntities(
        const std::string& text
    ) = 0;

    virtual std::future<std::string> Rewrite(
        const std::string& text,
        const std::string& style
    ) = 0;

    virtual std::future<std::pair<float, std::string>> AnalyzeSentiment(
        const std::string& text
    ) = 0;

    // Batch operations
    virtual std::future<std::vector<TextAnalysisResult>> ProcessBatch(
        const std::vector<TextProcessingParams>& params_list
    ) = 0;

    // Utility functions
    virtual int CountWords(const std::string& text) const = 0;
    virtual int CountSentences(const std::string& text) const = 0;
    virtual std::vector<std::string> SplitIntoSentences(const std::string& text) const = 0;
};

// OpenAI text processor
class OpenAITextProcessor : public ITextProcessor {
public:
    explicit OpenAITextProcessor(
        std::shared_ptr<Client::OpenAIClient> client
    );
    ~OpenAITextProcessor() override;

    std::future<TextAnalysisResult> ProcessText(
        const TextProcessingParams& params
    ) override;

    std::future<std::string> Summarize(
        const std::string& text,
        int max_length = 100
    ) override;

    std::future<std::string> Translate(
        const std::string& text,
        const std::string& target_language
    ) override;

    std::future<std::vector<std::string>> ExtractEntities(
        const std::string& text
    ) override;

    std::future<std::string> Rewrite(
        const std::string& text,
        const std::string& style
    ) override;

    std::future<std::pair<float, std::string>> AnalyzeSentiment(
        const std::string& text
    ) override;

    std::future<std::vector<TextAnalysisResult>> ProcessBatch(
        const std::vector<TextProcessingParams>& params_list
    ) override;

    int CountWords(const std::string& text) const override;
    int CountSentences(const std::string& text) const override;
    std::vector<std::string> SplitIntoSentences(const std::string& text) const override;

private:
    std::shared_ptr<Client::OpenAIClient> client_;
    std::string BuildPrompt(TextOperation operation, const std::string& text, const std::string& extra = "");
};

// Local text processor (rule-based)
class LocalTextProcessor : public ITextProcessor {
public:
    LocalTextProcessor();
    ~LocalTextProcessor() override;

    std::future<TextAnalysisResult> ProcessText(
        const TextProcessingParams& params
    ) override;

    std::future<std::string> Summarize(
        const std::string& text,
        int max_length = 100
    ) override;

    std::future<std::string> Translate(
        const std::string& text,
        const std::string& target_language
    ) override;

    std::future<std::vector<std::string>> ExtractEntities(
        const std::string& text
    ) override;

    std::future<std::string> Rewrite(
        const std::string& text,
        const std::string& style
    ) override;

    std::future<std::pair<float, std::string>> AnalyzeSentiment(
        const std::string& text
    ) override;

    std::future<std::vector<TextAnalysisResult>> ProcessBatch(
        const std::vector<TextProcessingParams>& params_list
    ) override;

    int CountWords(const std::string& text) const override;
    int CountSentences(const std::string& text) const override;
    std::vector<std::string> SplitIntoSentences(const std::string& text) const override;

private:
    std::vector<std::string> ExtractKeywords(const std::string& text) const;
    float SimpleSentimentScore(const std::string& text) const;
};

// Text processor factory
class TextProcessorFactory {
public:
    static std::unique_ptr<ITextProcessor> CreateOpenAIProcessor(
        std::shared_ptr<Client::OpenAIClient> client
    );

    static std::unique_ptr<ITextProcessor> CreateLocalProcessor();

    static std::unique_ptr<ITextProcessor> CreateDefault();
};

// Text utilities
class TextUtils {
public:
    static std::string Trim(const std::string& text);
    static std::string ToLower(const std::string& text);
    static std::string ToUpper(const std::string& text);
    static std::string RemovePunctuation(const std::string& text);
    static std::vector<std::string> Tokenize(const std::string& text);
    static std::string Join(const std::vector<std::string>& tokens, const std::string& delimiter = " ");
    static bool IsEmpty(const std::string& text);
    static std::string Truncate(const std::string& text, int max_length, const std::string& ellipsis = "...");
};

}  // namespace Content::OpenAI::Text
