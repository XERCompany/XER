#include "custom_effect.h"

#include "custom_effect_algorithms.h"
#include "custom_effect_advanced_processing.h"
#include "custom_effect_core.h"
#include "custom_effect_eq_lite.h"
#include "custom_effect_fxdata.h"
#include "custom_effect_invoke.h"
#include "custom_effect_list.h"

#include "audio/audio_core/audio_interleave_processor.h"
#include "audio/fx_customs/fx_customs_effect_builder.h"

#include <algorithm>

namespace XER::Engine::Audio::FX {

namespace {

CustomEffectNode MakeNode(const std::string& algorithm_name, const std::string& label, uint32_t stage_index, float mix = 1.0f) {
	CustomEffectNode node = CreateDefaultCustomEffectNode(algorithm_name);
	node.label = label;
	node.stage_index = stage_index;
	node.mix_curve = {CustomEffectCurvePoint{0u, mix}};
	return node;
}

void SetParam(CustomEffectNode* node, const std::string& name, float value) {
	SetCustomEffectParameterValue(&node->parameters, name, value);
}

CustomEffectPackage MakePresetPackage(const std::string& label, std::vector<CustomEffectNode> nodes) {
	CustomEffectPackage package;
	package.label = label;
	package.render_config.block_size = 1024u;
	package.render_config.enable_multicore_render = true;
	package.nodes = std::move(nodes);
	return package;
}

}  // namespace

CustomEffectPackage BuildSmokeCustomEffectPackage() {
	return BuildExampleCustomEffectPackage();
}

std::vector<std::string> ListAvailableCustomEffects() {
	return ListCustomEffectPresets();
}

CustomEffectPackage BuildNamedCustomEffectPackage(const std::string& effect_name, const std::string& clap_plugin_reference) {
	CustomEffectPackage package;
	if (Customs::BuildFxCustomEffectPackage(effect_name, clap_plugin_reference, &package)) {
		return package;
	}

	return BuildExampleCustomEffectPackage(clap_plugin_reference);
}

CustomEffectPackage BuildExampleCustomEffectPackage(const std::string& clap_plugin_reference) {
	CustomEffectPackage package = BuildNamedCustomEffectPackage("AIrVoice", clap_plugin_reference);
	package.label = "custom_effect_example";
	CustomEffectNode clap_gain = MakeNode("clap_plugin", "clap_gain_parallel", 2u, 1.0f);
	clap_gain.plugin_reference = clap_plugin_reference.empty() ? std::string("builtin://gain") : clap_plugin_reference;
	SetParam(&clap_gain, "gain_db", 2.5f);
	package.nodes.push_back(clap_gain);
	return package;
}

bool RenderCustomEffectExample(
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	std::string* error_out) {
	return RunCustomEffectPackage(BuildExampleCustomEffectPackage(), sample_rate, input, output, report_out, error_out);
}

bool RunCustomEffectPackage(
	const CustomEffectPackage& package,
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	std::string* error_out) {
	bool advanced_handled = false;
	if (TryRunAdvancedCustomEffectPackage(package, sample_rate, input, output, report_out, &advanced_handled, error_out)) {
		return true;
	}
	if (advanced_handled) {
		return false;
	}

	CustomEffectFlow flow;
	if (!flow.Initialize(package, sample_rate, package.render_config.block_size, error_out)) {
		return false;
	}
	if (!flow.Process(input, output, error_out)) {
		return false;
	}
	if (report_out != nullptr) {
		*report_out = flow.GetLastReport();
	}
	return true;
}

bool RunCustomEffectPackageInterleaved(
	const CustomEffectPackage& package,
	float sample_rate,
	const std::vector<float>& input,
	int channels,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	std::string* error_out) {
	if (output == nullptr || channels <= 0 || input.empty() || (input.size() % static_cast<size_t>(channels)) != 0u) {
		if (error_out != nullptr) {
			*error_out = "custom effect interleaved buffer is invalid";
		}
		return false;
	}

	const size_t frame_count = input.size() / static_cast<size_t>(channels);
	std::vector<std::vector<float>> input_channels;
	if (!XER::Engine::Audio::Core::AudioInterleaveProcessor::DeinterleavePlanar(
			input.data(),
			channels,
			frame_count,
			input_channels)) {
		if (error_out != nullptr) {
			*error_out = "failed to deinterleave custom effect input";
		}
		return false;
	}

	std::vector<std::vector<float>> output_channels(static_cast<size_t>(channels));
	CustomEffectReport aggregate_report;
	aggregate_report.label = package.label;
	aggregate_report.stage_count = 0u;
	aggregate_report.node_count = 0u;
	aggregate_report.channel_count = static_cast<size_t>(channels);
	aggregate_report.worker_count_used = 1u;
	for (int channel_index = 0; channel_index < channels; ++channel_index) {
		CustomEffectReport channel_report;
		if (!RunCustomEffectPackage(
				package,
				sample_rate,
				input_channels[static_cast<size_t>(channel_index)],
				&output_channels[static_cast<size_t>(channel_index)],
				&channel_report,
				error_out)) {
			return false;
		}
		aggregate_report.node_count += channel_report.node_count;
		aggregate_report.stage_count = std::max(aggregate_report.stage_count, channel_report.stage_count);
		aggregate_report.worker_count_used = std::max(aggregate_report.worker_count_used, channel_report.worker_count_used);
		for (const auto& stage_report : channel_report.stage_reports) {
			aggregate_report.stage_reports.push_back("channel=" + std::to_string(channel_index) + "," + stage_report);
		}
		for (const auto& node_report : channel_report.node_reports) {
			aggregate_report.node_reports.push_back("channel=" + std::to_string(channel_index) + "," + node_report);
		}
	}

	output->assign(frame_count * static_cast<size_t>(channels), 0.0f);
	std::vector<const float*> channel_ptrs(static_cast<size_t>(channels), nullptr);
	for (int channel_index = 0; channel_index < channels; ++channel_index) {
		channel_ptrs[static_cast<size_t>(channel_index)] = output_channels[static_cast<size_t>(channel_index)].data();
	}
	if (!XER::Engine::Audio::Core::AudioInterleaveProcessor::Interleave(
			channel_ptrs.data(),
			channels,
			frame_count,
			output->data())) {
		if (error_out != nullptr) {
			*error_out = "failed to interleave custom effect output";
		}
		return false;
	}
	ComputeCustomEffectReportStats(*output, &aggregate_report);
	if (report_out != nullptr) {
		*report_out = aggregate_report;
	}
	return true;
}

}  // namespace XER::Engine::Audio::FX
