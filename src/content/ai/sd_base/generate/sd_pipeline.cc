#include "content/ai/sd_base/generate/sd_pipeline.h"

#include "content/ai/sd_base/backend/sd_model_runtime.h"
#include "content/ai/models_builder/utility/mb_logger.h"
#include "content/ai/ml/utils/ml_random_generator.h"
#include "content/ai/sd_base/controlnet/sd_controlnet_branch.h"
#include "content/ai/sd_base/fast/sd_fast_path.h"
#include "content/ai/sd_base/flow/sd_flow_context.h"
#include "content/ai/sd_base/gpu/sd_gpu_dispatch.h"
#include "content/ai/sd_base/seed/sd_seed_generator.h"
#include "content/ai/sd_base/tensor/sd_latent_tensor.h"
#include "content/ai/sd_base/vae/sd_vae_decoder.h"
#include "content/ai/sd_base/xl/sd_xl_branch.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace XER::AI::SDBase {

SdPipeline::SdPipeline(SdBaseConfig config)
    : config_(std::move(config)) {
}

SdGenerationResult SdPipeline::Generate(const SdGenerationRequest& request) {
    SdGenerationResult result;
    std::string cfg_error;
    if (!config_.Validate(&cfg_error)) {
        result.error = cfg_error;
        return result;
    }

    auto caps = SdGpuDispatch::Detect();
    const auto backend = SdGpuDispatch::SelectBackend(caps, request, config_);
    caps.selected_backend = SdGpuDispatch::BackendName(backend);
    SdFlowContext flow;
    flow.request = request;
    flow.caps = caps;

    auto& log = XER::ModelsBuilder::Utility::ModelBuilderLogger::GetInstance();
    log.Info("SDBase: generation started");

    SdGenerationRequest req = request;
    req.width = std::min(req.width, config_.max_width);
    req.height = std::min(req.height, config_.max_height);
    req.steps = std::clamp(SdFastPath::ResolveStepCount(req), config_.min_steps, config_.max_steps);
    req.enable_sdxl = req.enable_sdxl && config_.enable_sdxl;
    req.enable_controlnet = req.enable_controlnet && config_.enable_controlnet;
    req.enable_vae_decode = req.enable_vae_decode && config_.enable_vae;

    SdModelRuntime runtime;
    std::string load_error;
    if (!runtime.Load(config_, req, caps, backend, &load_error)) {
        result.error = load_error;
        return result;
    }

    SdXlBranch sdxl_branch;
    const std::string expanded_prompt =
        sdxl_branch.BuildPrompt(req, backend, runtime.loaded().sdxl_branch_loaded);

    const uint64_t seed = SdSeedGenerator::FromPrompt(expanded_prompt, req.negative_prompt, req.seed);
    XER::ML::Utils::MlRandomGenerator rng(seed);

    scheduler_.BuildSchedule(req.scheduler, req.steps);

    auto text_embedding = runtime.EncodeText(expanded_prompt, config_.text_embedding_dim);
    auto uncond_embedding = runtime.EncodeText(req.negative_prompt, config_.text_embedding_dim);

    auto latent = SdLatentTensor::Make(req.width, req.height);
    SdLatentTensor::FillGaussian(latent, rng, config_.init_noise_sigma);

    SdControlNetBranch controlnet_branch;

    float* latent_data = static_cast<float*>(latent.GetData());
    std::vector<float> predicted_noise(latent.GetElementCount(), 0.0f);
    for (uint32_t t = 0; t < req.steps; ++t) {
        const float sigma = scheduler_.SigmaAt(t);
        const float guidance = std::clamp(req.guidance_scale, 0.0f, 30.0f);

        const uint64_t n = latent.GetElementCount();
        std::copy(latent_data, latent_data + n, predicted_noise.begin());

        runtime.PredictNoiseCfg(text_embedding,
                                uncond_embedding,
                                predicted_noise.data(),
                                n,
                                t,
                                sigma,
                                guidance * sdxl_branch.RefinerGain(backend, runtime.loaded().sdxl_branch_loaded));

        scheduler_.ApplyStep(latent_data,
                             predicted_noise.data(),
                             n,
                             t,
                             config_.eta);

        if (req.enable_controlnet) {
            controlnet_branch.Apply(latent,
                                    req,
                                    backend,
                                    runtime.loaded().controlnet_loaded);
        }
    }

    SdVaeDecoder vae_decoder;
    auto image = vae_decoder.Decode(latent,
                                    req.width,
                                    req.height,
                                    req.enable_vae_decode && runtime.loaded().vae_loaded);

    result.ok = true;
    result.latent = std::move(latent);
    result.image = std::move(image);
    result.metadata["seed"] = std::to_string(seed);
    result.metadata["steps"] = std::to_string(req.steps);
    result.metadata["scheduler"] = req.scheduler == SdSchedulerType::DDIM ? "ddim" : "euler";
    result.metadata["backend"] = caps.selected_backend;
    result.metadata["weights_loaded_text"] = runtime.loaded().text_encoder_loaded ? "true" : "false";
    result.metadata["weights_loaded_unet"] = runtime.loaded().unet_loaded ? "true" : "false";
    result.metadata["unet_real_weights"] = runtime.HasRealUnet() ? "true" : "false";
    result.metadata["weights_loaded_vae"] = runtime.loaded().vae_loaded ? "true" : "false";
    result.metadata["weights_loaded_controlnet"] = runtime.loaded().controlnet_loaded ? "true" : "false";
    result.metadata["weights_loaded_sdxl"] = runtime.loaded().sdxl_branch_loaded ? "true" : "false";
    result.metadata["sdxl"] = req.enable_sdxl ? "true" : "false";
    result.metadata["controlnet"] = req.enable_controlnet ? "true" : "false";
    result.metadata["vae"] = req.enable_vae_decode ? "true" : "false";
    result.metadata["cudnn"] = caps.cudnn_available ? "true" : "false";
    result.metadata["cutlass"] = caps.cutlass_available ? "true" : "false";
    result.metadata["openvino"] = caps.openvino_available ? "true" : "false";
    result.metadata["onnx"] = caps.onnx_available ? "true" : "false";
    result.metadata["tensorflow"] = caps.tensorflow_available ? "true" : "false";
    result.metadata["cuda"] = caps.cuda_available ? "true" : "false";
    result.metadata["opencv"] = caps.opencv_available ? "true" : "false";
    result.metadata["ffmpeg"] = caps.ffmpeg_available ? "true" : "false";
    result.metadata["eigen"] = caps.eigen_available ? "true" : "false";
    result.metadata["xnnpack"] = caps.xnnpack_available ? "true" : "false";
    result.metadata["elapsed_ms"] = std::to_string(flow.ElapsedMs());

    log.Info("SDBase: generation finished");
    return result;
}

}  // namespace XER::AI::SDBase
