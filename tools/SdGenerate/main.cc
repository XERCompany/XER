// sd.exe — lightweight Stable-Diffusion-style image generator.
//
// Usage:
//   sd.exe generate.json [--output out.png]
//   sd generate.json --steps 20 --size 768x768 --seed 123 --count 4
//
// generate.json example:
//   {
//     "model": "<local path | Civitai model name | model id>",
//     "prompt": "a cat astronaut, cinematic lighting",
//     "negative_prompt": "blurry, low quality",
//     "size_w": 512, "size_h": 512,
//     "steps": 30, "guidance": 7.5, "seed": -1,
//     "scheduler": "euler",               // "euler" | "ddim"
//     "sampler": "euler",
//     "preset": "balanced",               // "draft" | "balanced" | "high"
//     "count": 1,                         // batch size
//     "upscale": 1,                       // integer upscale via OpenCV
//     "lora": "adapter.safetensors",      // optional LoRA merge
//     "lora_strength": 1.0,
//     "output": "out.png",                // .png | .jpg | .jpeg
//     "civitai_model_id": "123",
//     "civitai_version_id": "456",
//     "api_key": "...",                   // or XER_CIVITAI_API_KEY env
//     "model_dir": "models/sd_base"
//   }
//
// The pipeline lives in src/content/ai/sd_base; model weights resolve via
// src/content/civitaimodels (Civitai API + streaming cache downloader).

#include "sd_config.h"
#include "sd_lora.h"
#include "sd_model_resolver.h"
#include "sd_png_writer.h"

#include "helper/string_utils.h"

#include "content/ai/sd_base/generate/sd_pipeline.h"
#include "image/core/image_buffer.h"
#include "image/io/image_saver.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if __has_include(<opencv2/imgproc.hpp>)
#  include <opencv2/core.hpp>
#  include <opencv2/imgproc.hpp>
#  define SD_HAS_OPENCV 1
#endif

namespace fs = std::filesystem;

namespace {

using XER::Tools::SdGenerate::SdCliConfig;

void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " <generate.json> [options]\n"
        << "\n"
        << "Options (override generate.json fields):\n"
        << "  --output, -o <path>     output image (default sd_<seed>.png)\n"
        << "  --prompt <text>         text prompt\n"
        << "  --negative <text>       negative prompt\n"
        << "  --steps <n>             denoising steps\n"
        << "  --size <WxH>            output resolution\n"
        << "  --seed <n>              seed (-1 = random)\n"
        << "  --guidance <f>          CFG scale\n"
        << "  --scheduler <name>      euler | ddim\n"
        << "  --count <n>             number of images\n"
        << "  --upscale <n>           integer upscale factor\n"
        << "  --lora <path>           LoRA safetensors to merge\n"
        << "\n"
        << "generate.json fields: model, prompt, negative_prompt, size_w/h,\n"
        << "  steps, guidance, seed, scheduler, preset, count, upscale, lora,\n"
        << "  lora_strength, output, civitai_model_id, civitai_version_id,\n"
        << "  api_key, model_dir.\n";
}

bool ParseSize(const std::string& s, uint32_t* w, uint32_t* h) {
    const auto x = s.find('x');
    if (x == std::string::npos) return false;
    try {
        *w = static_cast<uint32_t>(std::stoul(s.substr(0, x)));
        *h = static_cast<uint32_t>(std::stoul(s.substr(x + 1)));
    } catch (...) {
        return false;
    }
    return *w >= 64 && *h >= 64;
}

std::string InsertIndex(const std::string& path, uint32_t index,
                        uint32_t count) {
    if (count <= 1) return path;
    fs::path p(path);
    const std::string stem = p.stem().string();
    const std::string ext = p.extension().string();
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_%04u", index);
    p = p.parent_path() / (stem + suffix + ext);
    return p.string();
}

// Merge the configured LoRA into the resolved UNet safetensors.
// Returns the path to use (merged temp file or the original).
std::string ApplyLoraIfRequested(const SdCliConfig& cfg,
                                 const std::string& unet_path,
                                 std::string* note) {
    if (cfg.lora_path.empty() || unet_path.empty()) return unet_path;
    const std::string lora_ext =
        XER::Helper::String::ToLowerAscii(fs::path(cfg.lora_path).extension().string());
    const std::string base_ext =
        XER::Helper::String::ToLowerAscii(fs::path(unet_path).extension().string());
    if (lora_ext != ".safetensors" || base_ext != ".safetensors") {
        *note = "lora skipped: only .safetensors merge is supported";
        return unet_path;
    }

    const std::string merged =
        (fs::temp_directory_path() /
         ("xer_sd_lora_" + std::to_string(
              static_cast<unsigned long long>(
                  std::chrono::high_resolution_clock::now()
                      .time_since_epoch().count())) +
          ".safetensors"))
            .string();

    uint32_t pairs = 0, skipped = 0;
    std::string err;
    if (!XER::Tools::SdGenerate::MergeLoraSafetensors(
            unet_path, cfg.lora_path, cfg.lora_strength, merged, &pairs,
            &skipped, &err)) {
        *note = "lora merge failed: " + err;
        return unet_path;
    }
    *note = "lora merged: " + std::to_string(pairs) + " pairs, " +
            std::to_string(skipped) + " skipped -> " + merged;
    return merged;
}

#ifdef SD_HAS_OPENCV
// Integer-factor upscale of RGBA8 pixels via OpenCV Lanczos.
bool UpscaleRgba(std::vector<uint8_t>* rgba, uint32_t* w, uint32_t* h,
                 uint32_t factor) {
    if (factor <= 1 || !rgba || !*w || !*h) return true;
    cv::Mat src(*h, *w, CV_8UC4, rgba->data());
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(*w * factor, *h * factor), 0, 0,
               cv::INTER_LANCZOS4);
    if (dst.empty()) return false;
    rgba->assign(dst.datastart, dst.dataend);
    *w = static_cast<uint32_t>(dst.cols);
    *h = static_cast<uint32_t>(dst.rows);
    return true;
}
#else
bool UpscaleRgba(std::vector<uint8_t>*, uint32_t*, uint32_t*, uint32_t) {
    return true;
}
#endif

// Dispatch on extension: JPEG via XER_Image saver, everything else PNG.
bool SaveImage(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h,
               const std::string& path,
               const std::map<std::string, std::string>& meta,
               const SdCliConfig& cfg, std::string* error) {
    const std::string ext =
        XER::Helper::String::ToLowerAscii(fs::path(path).extension().string());
    if (ext == ".jpg" || ext == ".jpeg") {
        image::ImageBuffer buf(static_cast<int>(w), static_cast<int>(h),
                               image::PixelFormat::RGBA8);
        if (!buf.IsValid()) {
            *error = "ImageBuffer alloc failed";
            return false;
        }
        std::memcpy(buf.Data(), rgba.data(), rgba.size());
        if (!image::ImageSaver().SaveJpeg(buf, path, 95)) {
            *error = "JPEG save failed (FFmpeg bridge?)";
            return false;
        }
        return true;
    }
    return XER::Tools::SdGenerate::WritePng(
        rgba.data(), w, h, path, meta, cfg.request.prompt,
        cfg.request.negative_prompt, cfg.sampler_name, error);
}

}  // namespace

int main(int argc, char** argv) {
    std::string json_path;
    std::string cli_output;
    std::vector<std::string> positional;

    // CLI overrides collected as (flag, value) applied after JSON load.
    std::string ov_prompt, ov_negative, ov_scheduler;
    std::string ov_size, ov_seed, ov_steps, ov_guidance, ov_count, ov_upscale,
        ov_lora;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "error: " << flag << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--output" || arg == "-o") {
            cli_output = need_value("--output");
        } else if (arg == "--prompt") {
            ov_prompt = need_value("--prompt");
        } else if (arg == "--negative") {
            ov_negative = need_value("--negative");
        } else if (arg == "--steps") {
            ov_steps = need_value("--steps");
        } else if (arg == "--size") {
            ov_size = need_value("--size");
        } else if (arg == "--seed") {
            ov_seed = need_value("--seed");
        } else if (arg == "--guidance") {
            ov_guidance = need_value("--guidance");
        } else if (arg == "--scheduler") {
            ov_scheduler = need_value("--scheduler");
        } else if (arg == "--count") {
            ov_count = need_value("--count");
        } else if (arg == "--upscale") {
            ov_upscale = need_value("--upscale");
        } else if (arg == "--lora") {
            ov_lora = need_value("--lora");
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "error: unknown option " << arg << "\n";
            PrintUsage(argv[0]);
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        PrintUsage(argv[0]);
        return 2;
    }
    json_path = positional.front();

    SdCliConfig cfg;
    std::string error;
    if (!XER::Tools::SdGenerate::LoadSdConfig(json_path, &cfg, &error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    // --- Apply CLI overrides ---
    auto& req = cfg.request;
    if (!ov_prompt.empty()) req.prompt = ov_prompt;
    if (!ov_negative.empty()) req.negative_prompt = ov_negative;
    if (!ov_steps.empty()) req.steps = static_cast<uint32_t>(std::stoul(ov_steps));
    if (!ov_size.empty() && !ParseSize(ov_size, &req.width, &req.height)) {
        std::cerr << "error: bad --size (expected WxH, e.g. 768x768)\n";
        return 2;
    }
    if (!ov_seed.empty()) {
        const long long s = std::stoll(ov_seed);
        req.seed = s > 0 ? static_cast<uint64_t>(s) : 0;
    }
    if (!ov_guidance.empty()) req.guidance_scale = std::stof(ov_guidance);
    if (!ov_scheduler.empty()) {
        req.scheduler = XER::Helper::String::ToLowerAscii(ov_scheduler) == "ddim"
            ? XER::AI::SDBase::SdSchedulerType::DDIM
            : XER::AI::SDBase::SdSchedulerType::Euler;
    }
    if (!ov_count.empty()) {
        cfg.count = static_cast<uint32_t>(std::max(1, std::stoi(ov_count)));
    }
    if (!ov_upscale.empty()) {
        cfg.upscale = static_cast<uint32_t>(
            std::clamp(std::stoi(ov_upscale), 1, 8));
    }
    if (!ov_lora.empty()) cfg.lora_path = ov_lora;
    if (!cli_output.empty()) cfg.output_path = cli_output;

    if (req.prompt.empty()) {
        std::cerr << "error: empty prompt\n";
        return 2;
    }

    std::cout << "[sd] prompt: " << req.prompt << "\n";
    std::cout << "[sd] size: " << req.width << "x" << req.height
              << ", steps: " << req.steps
              << ", guidance: " << req.guidance_scale
              << ", count: " << cfg.count << "\n";

    // --- Resolve model weights (local path or Civitai download) ---
    auto resolved = XER::Tools::SdGenerate::ResolveModel(cfg);
    if (!resolved.ok) {
        std::cerr << "error: " << resolved.error << "\n";
        return 1;
    }
    // Explicit per-request paths (from JSON "unet"/"vae"/... fields or CLI)
    // take priority over resolver output.
    if (req.unet_path.empty() && !resolved.unet_path.empty()) {
        req.unet_path = resolved.unet_path;
    }
    if (req.text_encoder_path.empty() && !resolved.text_encoder_path.empty()) {
        req.text_encoder_path = resolved.text_encoder_path;
    }
    if (req.vae_decoder_path.empty() && !resolved.vae_decoder_path.empty()) {
        req.vae_decoder_path = resolved.vae_decoder_path;
    }
    if (req.controlnet_path.empty() && !resolved.controlnet_path.empty()) {
        req.controlnet_path = resolved.controlnet_path;
    }
    if (req.sdxl_text_encoder_2_path.empty() &&
        !resolved.sdxl_text_encoder_2_path.empty()) {
        req.sdxl_text_encoder_2_path = resolved.sdxl_text_encoder_2_path;
    }
    if (req.sdxl_refiner_unet_path.empty() &&
        !resolved.sdxl_refiner_unet_path.empty()) {
        req.sdxl_refiner_unet_path = resolved.sdxl_refiner_unet_path;
    }
    if (!resolved.model_dir.empty()) cfg.base.model_root = resolved.model_dir;
    if (!cfg.model_dir.empty() && resolved.model_dir.empty()) {
        cfg.base.model_root = cfg.model_dir;
    }

    // --- Optional LoRA merge into the UNet safetensors ---
    if (!cfg.lora_path.empty()) {
        std::string note;
        req.unet_path = ApplyLoraIfRequested(cfg, req.unet_path, &note);
        std::cout << "[sd] " << note << "\n";
    }

    // --- Batch generation loop ---
    const fs::path json_dir = fs::path(json_path).parent_path();
    XER::AI::SDBase::SdPipeline pipeline(cfg.base);
    const uint64_t base_seed = req.seed;
    int failures = 0;

    for (uint32_t i = 0; i < cfg.count; ++i) {
        // Explicit seed -> per-image offset; seed=0 -> pipeline derives a
        // fresh time+prompt seed on every call.
        req.seed = base_seed ? base_seed + i : 0;

        auto result = pipeline.Generate(req);
        if (!result.ok) {
            std::cerr << "error: generation " << (i + 1) << "/" << cfg.count
                      << " failed: " << result.error << "\n";
            ++failures;
            continue;
        }

        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0;
        if (!XER::Tools::SdGenerate::TensorToRgba8(result.image, &rgba, &w, &h)) {
            std::cerr << "error: unsupported image tensor format\n";
            ++failures;
            continue;
        }
        if (cfg.upscale > 1 &&
            !UpscaleRgba(&rgba, &w, &h, cfg.upscale)) {
            std::cerr << "warning: upscale failed, keeping original size\n";
        }

        std::string out_path = cfg.output_path;
        if (out_path.empty()) {
            const std::string seed = result.metadata.count("seed")
                ? result.metadata.at("seed") : "0";
            out_path = "sd_" + seed + ".png";
        }
        out_path = InsertIndex(out_path, i, cfg.count);
        if (json_dir.empty() && fs::path(out_path).parent_path().empty()) {
            out_path = (json_dir / out_path).string();
        }

        std::map<std::string, std::string> meta = result.metadata;
        meta["model"] = resolved.display_name.empty()
            ? cfg.model_spec : resolved.display_name;
        meta["guidance"] = std::to_string(req.guidance_scale);

        std::string save_error;
        if (!SaveImage(rgba, w, h, out_path, meta, cfg, &save_error)) {
            std::cerr << "error: save failed: " << save_error << "\n";
            ++failures;
            continue;
        }

        std::cout << "[sd] " << (i + 1) << "/" << cfg.count << " wrote "
                  << out_path << " (" << w << "x" << h << ") seed="
                  << result.metadata["seed"]
                  << " backend=" << result.metadata["backend"]
                  << " elapsed_ms=" << result.metadata["elapsed_ms"] << "\n";
    }

    return failures == static_cast<int>(cfg.count) ? 1 : 0;
}
