#include "audio_config_helper.h"
#include "json_config_helper.h"
#include <absl/strings/str_cat.h>

namespace XER {

std::vector<std::string> AudioConfigHelper::GetJsonConfigEffects(const Json::Value& root) {
    std::vector<std::string> effects;
    const Json::Value* value = JsonConfigHelper::FindJsonConfigValue(
        root,
        {"effects", "effectNames", "effect_names", "audioEffects", "audio_effects"});
    if (value == nullptr || value->isNull()) {
        return effects;
    }
    if (value->isArray()) {
        for (const Json::Value& item : *value) {
            if (item.isString()) {
                const std::string effect = JsonConfigHelper::TrimCopy(item.asString());
                if (!effect.empty()) {
                    effects.push_back(effect);
                }
            }
        }
        return effects;
    }
    if (value->isString()) {
        JsonConfigHelper::AppendCsvStrings(value->asString(), &effects);
    }
    return effects;
}

bool AudioConfigHelper::ApplyAudioConfigFile(
    AppCommand::Parsed* parsed,
    std::string* error_out) {
    if (parsed == nullptr || parsed->audio_config_file.empty()) {
        return true;
    }

    Json::Value root;
    if (!JsonConfigHelper::ReadJsonConfigFile(parsed->audio_config_file, &root, error_out)) {
        return false;
    }

    parsed->audio_input_path = JsonConfigHelper::GetJsonConfigString(
        root,
        {"input", "inputPath", "input_path", "audioInput", "audio_input"},
        parsed->audio_input_path);
    parsed->output_dir = JsonConfigHelper::GetJsonConfigString(
        root,
        {"outputDir", "output_dir", "output"},
        parsed->output_dir);
    parsed->audio_clap_plugin_reference = JsonConfigHelper::GetJsonConfigString(
        root,
        {"clapPluginReference", "clap_plugin_reference", "clapPlugin", "clap_plugin"},
        parsed->audio_clap_plugin_reference);

    std::vector<std::string> clap_plugin_references = JsonConfigHelper::GetJsonConfigStringList(
        root,
        {"clapPluginReferences", "clap_plugin_references", "clapPlugins", "clap_plugins"});
    if (!clap_plugin_references.empty()) {
        parsed->audio_clap_plugin_references = std::move(clap_plugin_references);
        parsed->audio_clap_plugin_reference = parsed->audio_clap_plugin_references.front();
    }

    parsed->target_sample_rate = JsonConfigHelper::GetJsonConfigInt(
        root,
        {"targetSampleRate", "target_sample_rate"},
        parsed->target_sample_rate);
    parsed->audio_target_channels = JsonConfigHelper::GetJsonConfigInt(
        root,
        {"targetChannels", "target_channels"},
        parsed->audio_target_channels);
    parsed->audio_raw_sample_rate = JsonConfigHelper::GetJsonConfigInt(
        root,
        {"rawSampleRate", "raw_sample_rate", "audioRawSampleRate", "audio_raw_sample_rate"},
        parsed->audio_raw_sample_rate);
    parsed->audio_batch_mode = JsonConfigHelper::GetJsonConfigString(
        root,
        {"batchMode", "batch_mode", "audioBatchMode", "audio_batch_mode"},
        parsed->audio_batch_mode);
    parsed->json_output = JsonConfigHelper::GetJsonConfigBool(
        root,
        {"json", "jsonOutput", "json_output"},
        parsed->json_output);
    parsed->audio_pipe_mp3 = JsonConfigHelper::GetJsonConfigBool(
        root,
        {"pipeMp3", "pipe_mp3", "mp3Pipe", "mp3_pipe"},
        parsed->audio_pipe_mp3);

    std::vector<std::string> effects = GetJsonConfigEffects(root);
    const std::string single_effect = JsonConfigHelper::TrimCopy(
        JsonConfigHelper::GetJsonConfigString(
            root,
            {"effect", "effectName", "effect_name", "audioEffect", "audio_effect"},
            parsed->audio_effect_name));
    if (!effects.empty()) {
        parsed->audio_effect_names = effects;
        parsed->audio_effect_name = effects.front();
    } else if (!single_effect.empty()) {
        parsed->audio_effect_name = single_effect;
        parsed->audio_effect_names = {single_effect};
    }

    const std::string command = JsonConfigHelper::NormalizeConfigToken(
        JsonConfigHelper::GetJsonConfigString(root, {"command", "type", "mode"}));
    if (command == "audiofxcustom" || command == "custom" || command == "single") {
        parsed->type = AppCommand::Type::kAudioFxCustom;
    } else if (command == "audiofxbatch" || command == "batch" || command == "chain") {
        parsed->type = AppCommand::Type::kAudioFxBatch;
    } else if (parsed->type == AppCommand::Type::kAudioFxConfig || parsed->type == AppCommand::Type::kRun) {
        parsed->type = parsed->audio_effect_names.size() > 1u
            ? AppCommand::Type::kAudioFxBatch
            : AppCommand::Type::kAudioFxCustom;
    }

    if (parsed->type == AppCommand::Type::kAudioFxCustom && parsed->audio_effect_name.empty() && parsed->audio_effect_names.size() == 1u) {
        parsed->audio_effect_name = parsed->audio_effect_names.front();
    }
    if (parsed->type == AppCommand::Type::kAudioFxBatch && parsed->audio_effect_names.empty() && !parsed->audio_effect_name.empty()) {
        parsed->audio_effect_names.push_back(parsed->audio_effect_name);
    }
    if (parsed->audio_pipe_mp3 && parsed->audio_input_path.empty()) {
        parsed->audio_input_path = "-";
    }
    if ((parsed->type == AppCommand::Type::kAudioFxCustom || parsed->type == AppCommand::Type::kAudioFxBatch) && parsed->audio_effect_names.empty() && parsed->audio_effect_name.empty()) {
        if (error_out != nullptr) {
            *error_out = "audio config JSON requires effect/effects";
        }
        return false;
    }
    return true;
}

} // namespace XER
