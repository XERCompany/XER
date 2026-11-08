#include "custom_effect_flow.h"

#include <algorithm>
#include <map>

#include "custom_effect_core.h"

namespace XER::Audio::FX {

bool CustomEffectFlow::Initialize(const CustomEffectPackage& package, float sample_rate, size_t max_block_size, std::string* error_out) {
	if (!ValidateCustomEffectPackage(package, error_out)) {
		return false;
	}
	package_ = package;
	render_config_ = package_.render_config;
	if (max_block_size > 0) {
		render_config_.block_size = max_block_size;
	}
	stage_indices_.clear();
	stage_processors_.clear();
	std::map<uint32_t, std::vector<const CustomEffectNode*>> stage_map;
	for (const auto& node : package_.nodes) {
		stage_map[node.stage_index].push_back(&node);
	}
	for (const auto& entry : stage_map) {
		stage_indices_.push_back(entry.first);
		stage_processors_.emplace_back();
		auto& stage_processors = stage_processors_.back();
		stage_processors.resize(entry.second.size());
		for (size_t index = 0; index < entry.second.size(); ++index) {
			if (!stage_processors[index].Initialize(*entry.second[index], sample_rate, render_config_.block_size, error_out)) {
				return false;
			}
		}
	}
	last_report_ = CustomEffectReport{};
	last_report_.label = package_.label;
	last_report_.node_count = package_.nodes.size();
	last_report_.stage_count = stage_processors_.size();
	return true;
}

bool CustomEffectFlow::Process(const std::vector<float>& input, std::vector<float>* output, std::string* error_out) {
	if (output == nullptr) {
		if (error_out != nullptr) {
			*error_out = "custom effect flow output target is null";
		}
		return false;
	}
	std::vector<float> current = input;
	std::vector<float> stage_output;
	last_report_ = CustomEffectReport{};
	last_report_.label = package_.label;
	last_report_.node_count = package_.nodes.size();
	last_report_.stage_count = stage_processors_.size();
	last_report_.worker_count_used = 1;
	for (size_t stage = 0; stage < stage_processors_.size(); ++stage) {
		size_t stage_workers = 1;
		std::string stage_error;
		if (!ApplyCustomEffectProcessors(&stage_processors_[stage], current, render_config_, &stage_output, &stage_workers, &stage_error, !streaming_)) {
			if (error_out != nullptr) {
				*error_out = stage_error;
			}
			return false;
		}
		current.swap(stage_output);
		last_report_.worker_count_used = std::max(last_report_.worker_count_used, stage_workers);
		last_report_.stage_reports.push_back(
			"stage=" + std::to_string(stage_indices_[stage]) +
			", branches=" + std::to_string(stage_processors_[stage].size()) +
			", workers=" + std::to_string(stage_workers));
		for (const auto& processor : stage_processors_[stage]) {
			last_report_.node_reports.push_back(processor.GetReport());
		}
	}
	*output = current;
	ComputeCustomEffectReportStats(*output, &last_report_);
	return true;
}

std::string CustomEffectFlow::GetReport() const {
	return BuildCustomEffectReportText(last_report_);
}

}  // namespace XER::Audio::FX
