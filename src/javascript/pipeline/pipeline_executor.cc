#include "javascript/pipeline/pipeline_executor.h"

#include "javascript/common/compression_codec.h"
#include "javascript/common/text_normalizer.h"

namespace XER::JavaScript::Pipeline {

PipelineExecutor::PipelineExecutor(XER::JavaScript::Events::EventRouter* router)
    : router_(router) {}

std::size_t PipelineExecutor::PublishRaw(const std::string& topic, const std::string& text) {
    if (router_ == nullptr) {
        return 0;
    }

    const std::string normalized_topic = XER::JavaScript::Common::TextNormalizer::NormalizeTopic(topic);
    const std::string normalized_text = XER::JavaScript::Common::TextNormalizer::NormalizeText(text);
    return router_->Publish(normalized_topic, normalized_text);
}

std::size_t PipelineExecutor::PublishCompressed(
    const std::string& topic,
    const std::vector<std::uint8_t>& compressed) {
    if (router_ == nullptr) {
        return 0;
    }

    auto text = XER::JavaScript::Common::CompressionCodec::DecompressToString(compressed);
    if (!text.has_value()) {
        return 0;
    }

    return PublishRaw(topic, *text);
}

}  // namespace XER::JavaScript::Pipeline
