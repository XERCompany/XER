#include "text_processor.h"
#include "../../../async_io/io_thread_pool.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <set>

namespace Content::OpenAI::Text {

OpenAITextProcessor::OpenAITextProcessor(
    std::shared_ptr<Client::OpenAIClient> client)
    : client_(client) {
}

OpenAITextProcessor::~OpenAITextProcessor() = default;

std::future<TextAnalysisResult> OpenAITextProcessor::ProcessText(
    const TextProcessingParams& params) {

    std::promise<TextAnalysisResult> promise;
    auto future = promise.get_future();

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, params, promise = std::move(promise)]() mutable {
        TextAnalysisResult result;

        std::string prompt = BuildPrompt(params.operation, params.input_text,
            params.target_language.empty() ? "" : params.target_language);

        std::vector<ChatMessage> messages;
        ChatMessage system_msg;
        system_msg.role = MessageRole::kSystem;
        system_msg.content.push_back({ContentType::kText,
            "You are a helpful text processing assistant."});
        messages.push_back(system_msg);

        ChatMessage user_msg;
        user_msg.role = MessageRole::kUser;
        user_msg.content.push_back({ContentType::kText, prompt});
        messages.push_back(user_msg);

        CompletionOptions options;
        options.model = "gpt-4";
        options.temperature = params.temperature;
        options.max_tokens = params.max_length > 0 ? params.max_length : 500;

        auto response = client_->CreateChatCompletion(messages, options).get();

        if (!response.choices.empty()) {
            result.processed_text = response.choices[0].content[0].text;
        }

        result.word_count = CountWords(params.input_text);
        result.sentence_count = CountSentences(params.input_text);
        result.character_count = params.input_text.length();

        promise.set_value(result);
    });

    return future;
}

std::future<std::string> OpenAITextProcessor::Summarize(
    const std::string& text,
    int max_length) {

    TextProcessingParams params;
    params.operation = TextOperation::kSummarize;
    params.input_text = text;
    params.max_length = max_length;

    auto result_future = ProcessText(params);

    std::promise<std::string> promise;
    auto future = promise.get_future();

    result_future.then([promise = std::move(promise)](auto&& result) mutable {
        promise.set_value(result.get().processed_text);
    });

    return future;
}

std::future<std::string> OpenAITextProcessor::Translate(
    const std::string& text,
    const std::string& target_language) {

    TextProcessingParams params;
    params.operation = TextOperation::kTranslate;
    params.input_text = text;
    params.target_language = target_language;

    auto result_future = ProcessText(params);

    std::promise<std::string> promise;
    auto future = promise.get_future();

    result_future.then([promise = std::move(promise)](auto&& result) mutable {
        promise.set_value(result.get().processed_text);
    });

    return future;
}

std::future<std::vector<std::string>> OpenAITextProcessor::ExtractEntities(
    const std::string& text) {

    std::promise<std::vector<std::string>> promise;
    auto future = promise.get_future();

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, text, promise = std::move(promise)]() mutable {
        std::string prompt = BuildPrompt(TextOperation::kEntity, text);

        std::vector<ChatMessage> messages;
        ChatMessage user_msg;
        user_msg.role = MessageRole::kUser;
        user_msg.content.push_back({ContentType::kText, prompt});
        messages.push_back(user_msg);

        CompletionOptions options;
        options.model = "gpt-4";
        options.temperature = 0.3f;

        auto response = client_->CreateChatCompletion(messages, options).get();

        std::vector<std::string> entities;
        if (!response.choices.empty()) {
            // Parse entities from response
            std::string result = response.choices[0].content[0].text;
            // TODO: Parse entities properly
            entities.push_back(result);
        }

        promise.set_value(entities);
    });

    return future;
}

std::future<std::string> OpenAITextProcessor::Rewrite(
    const std::string& text,
    const std::string& style) {

    TextProcessingParams params;
    params.operation = TextOperation::kRewrite;
    params.input_text = text;
    params.style = style;

    auto result_future = ProcessText(params);

    std::promise<std::string> promise;
    auto future = promise.get_future();

    result_future.then([promise = std::move(promise)](auto&& result) mutable {
        promise.set_value(result.get().processed_text);
    });

    return future;
}

std::future<std::pair<float, std::string>> OpenAITextProcessor::AnalyzeSentiment(
    const std::string& text) {

    std::promise<std::pair<float, std::string>> promise;
    auto future = promise.get_future();

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, text, promise = std::move(promise)]() mutable {
        std::string prompt = BuildPrompt(TextOperation::kSentiment, text);

        std::vector<ChatMessage> messages;
        ChatMessage user_msg;
        user_msg.role = MessageRole::kUser;
        user_msg.content.push_back({ContentType::kText, prompt});
        messages.push_back(user_msg);

        CompletionOptions options;
        options.model = "gpt-4";
        options.temperature = 0.1f;

        auto response = client_->CreateChatCompletion(messages, options).get();

        float score = 0.0f;
        std::string label = "neutral";

        if (!response.choices.empty()) {
            std::string result = response.choices[0].content[0].text;
            // TODO: Parse sentiment properly
            label = result;
            if (label.find("positive") != std::string::npos) {
                score = 0.5f;
            } else if (label.find("negative") != std::string::npos) {
                score = -0.5f;
            }
        }

        promise.set_value({score, label});
    });

    return future;
}

std::future<std::vector<TextAnalysisResult>> OpenAITextProcessor::ProcessBatch(
    const std::vector<TextProcessingParams>& params_list) {

    std::promise<std::vector<TextAnalysisResult>> promise;
    auto future = promise.get_future();

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, params_list, promise = std::move(promise)]() mutable {
        std::vector<TextAnalysisResult> results;
        results.reserve(params_list.size());

        for (const auto& params : params_list) {
            auto result = ProcessText(params).get();
            results.push_back(result);
        }

        promise.set_value(results);
    });

    return future;
}

int OpenAITextProcessor::CountWords(const std::string& text) const {
    std::istringstream iss(text);
    return std::distance(std::istream_iterator<std::string>{iss},
                        std::istream_iterator<std::string>{});
}

int OpenAITextProcessor::CountSentences(const std::string& text) const {
    int count = 0;
    bool in_sentence = false;

    for (char c : text) {
        if (c == '.' || c == '!' || c == '?') {
            if (in_sentence) {
                count++;
                in_sentence = false;
            }
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            in_sentence = true;
        }
    }

    return count;
}

std::vector<std::string> OpenAITextProcessor::SplitIntoSentences(
    const std::string& text) const {

    std::vector<std::string> sentences;
    std::string current;

    for (char c : text) {
        current += c;
        if (c == '.' || c == '!' || c == '?') {
            sentences.push_back(TextUtils::Trim(current));
            current.clear();
        }
    }

    if (!current.empty()) {
        sentences.push_back(TextUtils::Trim(current));
    }

    return sentences;
}

std::string OpenAITextProcessor::BuildPrompt(
    TextOperation operation,
    const std::string& text,
    const std::string& extra) {

    switch (operation) {
        case TextOperation::kSummarize:
            return "Summarize the following text concisely:\n\n" + text;
        case TextOperation::kTranslate:
            return "Translate the following text to " + extra + ":\n\n" + text;
        case TextOperation::kExtract:
            return "Extract key information from the following text:\n\n" + text;
        case TextOperation::kRewrite:
            return "Rewrite the following text in " + extra + " style:\n\n" + text;
        case TextOperation::kClassify:
            return "Classify the following text:\n\n" + text;
        case TextOperation::kSentiment:
            return "Analyze the sentiment of the following text (positive/negative/neutral):\n\n" + text;
        case TextOperation::kKeyword:
            return "Extract keywords from the following text:\n\n" + text;
        case TextOperation::kEntity:
            return "Extract named entities from the following text:\n\n" + text;
        default:
            return "Process the following text:\n\n" + text;
    }
}

// LocalTextProcessor implementation
LocalTextProcessor::LocalTextProcessor() {
}

LocalTextProcessor::~LocalTextProcessor() = default;

std::future<TextAnalysisResult> LocalTextProcessor::ProcessText(
    const TextProcessingParams& params) {

    std::promise<TextAnalysisResult> promise;
    auto future = promise.get_future();

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, params, promise = std::move(promise)]() mutable {
        TextAnalysisResult result;

        result.processed_text = params.input_text;
        result.word_count = CountWords(params.input_text);
        result.sentence_count = CountSentences(params.input_text);
        result.character_count = params.input_text.length();
        result.keywords = ExtractKeywords(params.input_text);

        auto sentiment = AnalyzeSentiment(params.input_text).get();
        result.sentiment_score = sentiment.first;
        result.sentiment_label = sentiment.second;

        promise.set_value(result);
    });

    return future;
}

std::future<std::string> LocalTextProcessor::Summarize(
    const std::string& text,
    int max_length) {

    std::promise<std::string> promise;
    std::string summary = text.substr(0, max_length);
    if (text.length() > max_length) {
        summary += "...";
    }
    promise.set_value(summary);
    return promise.get_future();
}

std::future<std::string> LocalTextProcessor::Translate(
    const std::string& text,
    const std::string& target_language) {

    std::promise<std::string> promise;
    // Placeholder - actual translation would require translation library
    promise.set_value("[Translation to " + target_language + "]: " + text);
    return promise.get_future();
}

std::future<std::vector<std::string>> LocalTextProcessor::ExtractEntities(
    const std::string& text) {

    std::promise<std::vector<std::string>> promise;
    std::vector<std::string> entities;
    // Placeholder - would use NER library
    promise.set_value(entities);
    return promise.get_future();
}

std::future<std::string> LocalTextProcessor::Rewrite(
    const std::string& text,
    const std::string& style) {

    std::promise<std::string> promise;
    // Placeholder - would use NLP library
    promise.set_value(text);
    return promise.get_future();
}

std::future<std::pair<float, std::string>> LocalTextProcessor::AnalyzeSentiment(
    const std::string& text) {

    std::promise<std::pair<float, std::string>> promise;
    float score = SimpleSentimentScore(text);

    std::string label;
    if (score > 0.3f) {
        label = "positive";
    } else if (score < -0.3f) {
        label = "negative";
    } else {
        label = "neutral";
    }

    promise.set_value({score, label});
    return promise.get_future();
}

std::future<std::vector<TextAnalysisResult>> LocalTextProcessor::ProcessBatch(
    const std::vector<TextProcessingParams>& params_list) {

    std::promise<std::vector<TextAnalysisResult>> promise;
    std::vector<TextAnalysisResult> results;

    for (const auto& params : params_list) {
        results.push_back(ProcessText(params).get());
    }

    promise.set_value(results);
    return promise.get_future();
}

int LocalTextProcessor::CountWords(const std::string& text) const {
    std::istringstream iss(text);
    return std::distance(std::istream_iterator<std::string>{iss},
                        std::istream_iterator<std::string>{});
}

int LocalTextProcessor::CountSentences(const std::string& text) const {
    int count = 0;
    for (char c : text) {
        if (c == '.' || c == '!' || c == '?') {
            count++;
        }
    }
    return count;
}

std::vector<std::string> LocalTextProcessor::SplitIntoSentences(
    const std::string& text) const {

    std::vector<std::string> sentences;
    std::string current;

    for (char c : text) {
        current += c;
        if (c == '.' || c == '!' || c == '?') {
            sentences.push_back(TextUtils::Trim(current));
            current.clear();
        }
    }

    if (!current.empty()) {
        sentences.push_back(TextUtils::Trim(current));
    }

    return sentences;
}

std::vector<std::string> LocalTextProcessor::ExtractKeywords(
    const std::string& text) const {

    std::vector<std::string> keywords;
    std::set<std::string> stop_words = {"the", "a", "an", "and", "or", "but", "in", "on", "at", "to", "for", "of", "with", "by"};

    auto tokens = TextUtils::Tokenize(text);
    std::map<std::string, int> word_counts;

    for (const auto& token : tokens) {
        std::string lower = TextUtils::ToLower(token);
        if (stop_words.find(lower) == stop_words.end() && lower.length() > 3) {
            word_counts[lower]++;
        }
    }

    // Get top 5 keywords
    std::vector<std::pair<std::string, int>> sorted(word_counts.begin(), word_counts.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    for (int i = 0; i < 5 && i < static_cast<int>(sorted.size()); ++i) {
        keywords.push_back(sorted[i].first);
    }

    return keywords;
}

float LocalTextProcessor::SimpleSentimentScore(const std::string& text) const {
    std::set<std::string> positive_words = {"good", "great", "excellent", "amazing", "wonderful", "love", "happy", "joy"};
    std::set<std::string> negative_words = {"bad", "terrible", "awful", "hate", "sad", "angry", "poor"};

    std::string lower = TextUtils::ToLower(text);
    float score = 0.0f;

    for (const auto& word : positive_words) {
        if (lower.find(word) != std::string::npos) {
            score += 0.1f;
        }
    }

    for (const auto& word : negative_words) {
        if (lower.find(word) != std::string::npos) {
            score -= 0.1f;
        }
    }

    return std::clamp(score, -1.0f, 1.0f);
}

// TextProcessorFactory implementation
std::unique_ptr<ITextProcessor> TextProcessorFactory::CreateOpenAIProcessor(
    std::shared_ptr<Client::OpenAIClient> client) {

    return std::make_unique<OpenAITextProcessor>(client);
}

std::unique_ptr<ITextProcessor> TextProcessorFactory::CreateLocalProcessor() {
    return std::make_unique<LocalTextProcessor>();
}

std::unique_ptr<ITextProcessor> TextProcessorFactory::CreateDefault() {
    return CreateLocalProcessor();
}

// TextUtils implementation
std::string TextUtils::Trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = text.find_last_not_of(" \t\n\r");
    return text.substr(start, end - start + 1);
}

std::string TextUtils::ToLower(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string TextUtils::ToUpper(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string TextUtils::RemovePunctuation(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (!std::ispunct(static_cast<unsigned char>(c)) || c == '\'') {
            result += c;
        }
    }
    return result;
}

std::vector<std::string> TextUtils::Tokenize(const std::string& text) {
    std::istringstream iss(text);
    return {std::istream_iterator<std::string>{iss},
            std::istream_iterator<std::string>{}};
}

std::string TextUtils::Join(const std::vector<std::string>& tokens, const std::string& delimiter) {
    std::stringstream ss;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) ss << delimiter;
        ss << tokens[i];
    }
    return ss.str();
}

bool TextUtils::IsEmpty(const std::string& text) {
    return Trim(text).empty();
}

std::string TextUtils::Truncate(const std::string& text, int max_length, const std::string& ellipsis) {
    if (static_cast<int>(text.length()) <= max_length) {
        return text;
    }
    return text.substr(0, max_length) + ellipsis;
}

}  // namespace Content::OpenAI::Text
