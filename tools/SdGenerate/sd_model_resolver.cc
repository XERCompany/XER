#include "sd_model_resolver.h"

#include "cache/cache_manager.h"
#include "content/civitaimodels/civitai_model_downloader.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>

namespace XER::Tools::SdGenerate {

namespace {

namespace fs = std::filesystem;

using content::civitai::CivitaiModel;
using content::civitai::CivitaiModelFile;
using content::civitai::CivitaiModelVersion;

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(
        static_cast<unsigned char>(c)));
    return s;
}

bool IsWeightFile(const fs::path& p) {
    static const std::set<std::string> exts = {
        ".safetensors", ".ckpt", ".pt", ".pth", ".gguf", ".bin", ".onnx"};
    return exts.count(Lower(p.extension().string())) > 0;
}

// Classify a weight filename into a pipeline slot by keyword matching.
// Order matters: specific roles before generic checkpoint.
enum class Slot { Unet, TextEncoder, Vae, ControlNet, SdxlText2, SdxlRefiner,
                  Unknown };

Slot ClassifyFile(const std::string& name) {
    const std::string n = Lower(name);
    if (n.find("vae") != std::string::npos) return Slot::Vae;
    if (n.find("controlnet") != std::string::npos ||
        n.find("control_net") != std::string::npos) {
        return Slot::ControlNet;
    }
    if (n.find("refiner") != std::string::npos) return Slot::SdxlRefiner;
    if (n.find("text_encoder_2") != std::string::npos ||
        n.find("clip2") != std::string::npos ||
        n.find("pooled") != std::string::npos ||
        n.find("t5") != std::string::npos ||
        n.find("llama") != std::string::npos) {
        return Slot::SdxlText2;
    }
    if (n.find("text_encoder") != std::string::npos ||
        n.find("textencoder") != std::string::npos ||
        n.find("clip") != std::string::npos ||
        n.find("condition") != std::string::npos ||
        n.find("encoder") != std::string::npos) {
        return Slot::TextEncoder;
    }
    if (n.find("unet") != std::string::npos ||
        n.find("denoiser") != std::string::npos ||
        n.find("diffusion") != std::string::npos ||
        n.find("transformer") != std::string::npos ||
        n.find("dit") != std::string::npos) {
        return Slot::Unet;
    }
    return Slot::Unknown;
}

void AssignSlot(ResolvedModel* out, Slot slot, const std::string& path) {
    switch (slot) {
        case Slot::Unet: out->unet_path = path; break;
        case Slot::TextEncoder: out->text_encoder_path = path; break;
        case Slot::Vae: out->vae_decoder_path = path; break;
        case Slot::ControlNet: out->controlnet_path = path; break;
        case Slot::SdxlText2: out->sdxl_text_encoder_2_path = path; break;
        case Slot::SdxlRefiner: out->sdxl_refiner_unet_path = path; break;
        case Slot::Unknown: break;
    }
}

// Single-file checkpoint: one file carries every component.
void AssignCheckpoint(ResolvedModel* out, const std::string& path) {
    out->unet_path = path;
    out->text_encoder_path = path;
    out->vae_decoder_path = path;
}

bool SlotPathEmpty(const ResolvedModel& out, Slot slot) {
    switch (slot) {
        case Slot::Unet: return out.unet_path.empty();
        case Slot::TextEncoder: return out.text_encoder_path.empty();
        case Slot::Vae: return out.vae_decoder_path.empty();
        case Slot::ControlNet: return out.controlnet_path.empty();
        case Slot::SdxlText2: return out.sdxl_text_encoder_2_path.empty();
        case Slot::SdxlRefiner: return out.sdxl_refiner_unet_path.empty();
        case Slot::Unknown: return true;
    }
    return true;
}

ResolvedModel ResolveLocalPath(const fs::path& path) {
    ResolvedModel out;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        if (!IsWeightFile(path)) {
            out.error = "not a recognized weight file: " + path.string();
            return out;
        }
        AssignCheckpoint(&out, fs::absolute(path, ec).string());
        out.model_dir = fs::absolute(path.parent_path(), ec).string();
        out.display_name = path.filename().string();
        out.ok = true;
        return out;
    }

    if (fs::is_directory(path, ec)) {
        out.model_dir = fs::absolute(path, ec).string();
        out.display_name = path.filename().string();

        std::vector<fs::path> weights;
        for (const auto& entry : fs::directory_iterator(path, ec)) {
            if (entry.is_regular_file(ec) && IsWeightFile(entry.path())) {
                weights.push_back(entry.path());
            }
        }
        if (weights.empty()) {
            // Allow config-style names (unet.onnx etc.) even with odd ext.
            for (const char* known :
                 {"unet", "text_encoder", "vae_decoder", "vae", "controlnet",
                  "sdxl_text_encoder_2", "sdxl_refiner_unet"}) {
                for (const auto& entry : fs::directory_iterator(path, ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    const std::string stem = Lower(entry.path().stem().string());
                    if (stem.find(known) != std::string::npos) {
                        weights.push_back(entry.path());
                        break;
                    }
                }
            }
        }
        if (weights.empty()) {
            out.error = "no weight files found in " + path.string();
            return out;
        }
        if (weights.size() == 1) {
            AssignCheckpoint(&out, fs::absolute(weights.front(), ec).string());
        } else {
            for (const auto& w : weights) {
                const std::string abs = fs::absolute(w, ec).string();
                const Slot slot = ClassifyFile(w.filename().string());
                if (slot != Slot::Unknown &&
                    SlotPathEmpty(out, slot)) {
                    AssignSlot(&out, slot, abs);
                } else if (slot == Slot::Unknown) {
                    // Unclassified weight in a multi-file dir: treat as UNet
                    // only when nothing else claimed the slot.
                    if (out.unet_path.empty()) out.unet_path = abs;
                }
            }
        }
        out.ok = true;
        return out;
    }

    out.error = "model path does not exist: " + path.string();
    return out;
}

const CivitaiModelVersion* PickVersion(const CivitaiModel& model,
                                       const std::string& version_id) {
    if (version_id.empty()) {
        for (const auto& v : model.versions) {
            const bool has_model =
                std::any_of(v.files.begin(), v.files.end(),
                            [](const CivitaiModelFile& f) {
                                return f.type == "Model";
                            });
            if (has_model) return &v;
        }
        return model.versions.empty() ? nullptr : &model.versions.front();
    }
    for (const auto& v : model.versions) {
        if (v.id == version_id || v.name == version_id) return &v;
    }
    return nullptr;
}

// Prefer SafeTensor > GGUF > Pickle for the primary download target; we
// download every "Model" file anyway, this only orders classification.
int FormatRank(const std::string& format) {
    const std::string f = Lower(format);
    if (f.find("safetensor") != std::string::npos) return 0;
    if (f.find("gguf") != std::string::npos) return 1;
    if (f.find("pickle") != std::string::npos) return 2;
    return 3;
}

ResolvedModel ResolveCivitai(const SdCliConfig& cfg) {
    ResolvedModel out;
    auto& dl = content::civitai::CivitaiModelDownloader::Instance();
    if (!cfg.api_key.empty()) dl.SetApiKey(cfg.api_key);

    std::optional<content::civitai::CivitaiModel> model;
    if (!cfg.civitai_model_id.empty()) {
        model = dl.GetModel(cfg.civitai_model_id);
        if (!model) {
            out.error = "Civitai: model id '" + cfg.civitai_model_id +
                        "' not found or API error";
            return out;
        }
    } else {
        auto results = dl.SearchModels(cfg.model_spec, 5);
        if (results.empty()) {
            out.error = "Civitai: no models matched '" + cfg.model_spec + "'";
            return out;
        }
        // Prefer the first Checkpoint/LORA-style hit; take first otherwise.
        model = results.front();
        for (const auto& r : results) {
            if (r.type.find("Checkpoint") != std::string::npos ||
                r.type.empty()) {
                model = r;
                break;
            }
        }
    }

    const auto* version = PickVersion(*model, cfg.civitai_version_id);
    if (!version) {
        out.error = "Civitai: version '" + cfg.civitai_version_id +
                    "' not found on model " + model->name;
        return out;
    }

    std::vector<const content::civitai::CivitaiModelFile*> model_files;
    for (const auto& f : version->files) {
        if (f.type == "Model") model_files.push_back(&f);
    }
    if (model_files.empty()) {
        out.error = "Civitai: version '" + version->name +
                    "' has no downloadable Model files";
        return out;
    }
    std::sort(model_files.begin(), model_files.end(),
              [](const auto* a, const auto* b) {
                  return FormatRank(a->format) < FormatRank(b->format);
              });

    out.display_name = model->name + " [" + version->name + "]";
    std::cerr << "[sd] resolving model: " << out.display_name << " ("
              << model_files.size() << " file(s))\n";

    // Download every Model file; on_done fires once per file.
    const size_t total = model_files.size();
    std::atomic<size_t> done{0};
    std::promise<std::string> result_promise;
    std::mutex err_mutex;
    std::string first_error;

    auto on_progress = [](uint64_t d, uint64_t t) {
        if (t == 0) return;
        const int pct = static_cast<int>(d * 100 / t);
        static std::atomic<int> last{-1};
        const int bucket = pct / 10 * 10;
        int expected = last.load();
        while (bucket > expected &&
               !last.compare_exchange_weak(expected, bucket)) {
        }
        if (bucket > expected) {
            std::cerr << "\r[sd] downloading... " << pct << "%" << std::flush;
        }
    };
    auto on_done = [&](bool ok, const std::string& error,
                       const fs::path& /*path*/) {
        if (!ok) {
            std::lock_guard<std::mutex> lk(err_mutex);
            if (first_error.empty()) first_error = error;
        }
        if (++done == total) {
            std::cerr << "\n";
            result_promise.set_value(first_error);
        }
    };

    if (!dl.DownloadModelVersion(*version, model->id, on_progress, on_done)) {
        out.error = "Civitai: failed to queue downloads (network/cache)";
        return out;
    }

    const std::string err = result_promise.get_future().get();
    if (!err.empty()) {
        out.error = "Civitai download failed: " + err;
        return out;
    }
    out.downloaded = true;

    // Reconstruct cache paths and classify into pipeline slots.
    const std::string key =
        "civitai/" + model->id + "/" + version->id;
    for (const auto* f : model_files) {
        const auto local = XER::Cache::CacheManager::Instance().PathFor(
            "models", key, f->name);
        const std::string abs = fs::absolute(local).string();
        const Slot slot = ClassifyFile(f->name);
        if (slot != Slot::Unknown) {
            AssignSlot(&out, slot, abs);
        } else {
            // Unclassified file: first one becomes the checkpoint/UNet.
            if (out.unet_path.empty()) out.unet_path = abs;
        }
    }

    // Single effective component (e.g. one safetensors checkpoint): point
    // text encoder and VAE at it too — checkpoints carry all components.
    const bool only_unet =
        !out.unet_path.empty() && out.text_encoder_path.empty() &&
        out.vae_decoder_path.empty() && out.controlnet_path.empty();
    if (only_unet && model_files.size() == 1) {
        AssignCheckpoint(&out, out.unet_path);
    }

    out.ok = true;
    return out;
}

}  // namespace

ResolvedModel ResolveModel(const SdCliConfig& cfg) {
    // Explicit local directory always wins over name search.
    if (!cfg.model_dir.empty()) {
        ResolvedModel local = ResolveLocalPath(fs::path(cfg.model_dir));
        if (local.ok) return local;
        // Fall through to Civitai only when model_spec itself is a name.
        if (!cfg.model_spec.empty() &&
            fs::exists(fs::path(cfg.model_spec))) {
            return local;  // report the path error
        }
    }

    if (!cfg.model_spec.empty()) {
        std::error_code ec;
        const fs::path p(cfg.model_spec);
        if (fs::exists(p, ec)) {
            return ResolveLocalPath(p);
        }
        // Not a local path -> Civitai name search.
    }

    if (cfg.civitai_model_id.empty() && cfg.model_spec.empty()) {
        ResolvedModel out;
        out.error = "no model specified: set \"model\" to a local path or a "
                    "Civitai model name/id";
        return out;
    }
    return ResolveCivitai(cfg);
}

}  // namespace XER::Tools::SdGenerate
