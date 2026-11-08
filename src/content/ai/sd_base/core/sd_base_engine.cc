#include "content/ai/sd_base/core/sd_base_engine.h"

#include "content/ai/sd_base/generate/sd_pipeline.h"

namespace XER::AI::SDBase {

SdBaseEngine::SdBaseEngine(SdBaseConfig config)
    : pipeline_(std::make_unique<SdPipeline>(std::move(config))) {
}

SdBaseEngine::~SdBaseEngine() = default;

SdGenerationResult SdBaseEngine::Generate(const SdGenerationRequest& request) {
    return pipeline_->Generate(request);
}

}  // namespace XER::AI::SDBase
