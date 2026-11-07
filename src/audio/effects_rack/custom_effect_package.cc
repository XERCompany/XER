#include "custom_effect_package.h"

#include <charconv>
#include <sstream>

#include "custom_effect_core.h"
#include "custom_effect_fxdata.h"

namespace XER::Engine::Audio::FX {

bool SerializeCustomEffectPackage(
	const CustomEffectPackage& package,
	std::string* text_out,
	std::string* error_out) {
	if (text_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "custom effect serialization target is null";
		}
		return false;
	}
	if (!ValidateCustomEffectPackage(package, error_out)) {
		return false;
	}

	std::ostringstream output;
	output << "label=" << package.label << "\n";
	output << "config=" << package.render_config.block_size
		<< "," << package.render_config.worker_count
		<< "," << (package.render_config.enable_multicore_render ? 1 : 0) << "\n";
	for (const auto& node : package.nodes) {
		output << "node=" << node.label
			<< "," << CustomEffectBackendToString(node.backend)
			<< "," << CustomEffectAlgorithmToString(node.algorithm)
			<< "," << node.stage_index
			<< "," << node.plugin_reference
			<< ",";
		for (size_t index = 0; index < node.parameters.size(); ++index) {
			if (index != 0) {
				output << ";";
			}
			output << node.parameters[index].name << "=" << node.parameters[index].value;
		}
		output << "\n";
	}
	*text_out = output.str();
	return true;
}

bool DeserializeCustomEffectPackage(
	const std::string& text,
	CustomEffectPackage* package_out,
	std::string* error_out) {
	if (package_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "custom effect parse target is null";
		}
		return false;
	}

	CustomEffectPackage parsed;
	std::istringstream input(text);
	std::string line;
	while (std::getline(input, line)) {
		if (line.rfind("label=", 0) == 0) {
			parsed.label = line.substr(6);
			continue;
		}
		if (line.rfind("config=", 0) == 0) {
			std::istringstream config_stream(line.substr(7));
			std::string field;
			if (std::getline(config_stream, field, ',')) {
				parsed.render_config.block_size = static_cast<size_t>(std::stoull(field));
			}
			if (std::getline(config_stream, field, ',')) {
				parsed.render_config.worker_count = static_cast<uint32_t>(std::stoul(field));
			}
			if (std::getline(config_stream, field, ',')) {
				parsed.render_config.enable_multicore_render = field != "0";
			}
			continue;
		}
		if (line.rfind("node=", 0) != 0) {
			continue;
		}
		const std::string body = line.substr(5);
		const size_t first_comma = body.find(',');
		const size_t second_comma = body.find(',', first_comma == std::string::npos ? first_comma : first_comma + 1);
		const size_t third_comma = body.find(',', second_comma == std::string::npos ? second_comma : second_comma + 1);
		const size_t fourth_comma = body.find(',', third_comma == std::string::npos ? third_comma : third_comma + 1);
		const size_t fifth_comma = body.find(',', fourth_comma == std::string::npos ? fourth_comma : fourth_comma + 1);
		if (first_comma == std::string::npos || second_comma == std::string::npos || third_comma == std::string::npos || fourth_comma == std::string::npos || fifth_comma == std::string::npos) {
			if (error_out != nullptr) {
				*error_out = "invalid custom effect node serialization: " + line;
			}
			return false;
		}
		CustomEffectNode node;
		node.label = body.substr(0, first_comma);
		if (!ParseCustomEffectBackend(body.substr(first_comma + 1, second_comma - first_comma - 1), &node.backend)) {
			if (error_out != nullptr) {
				*error_out = "invalid custom effect backend in serialization: " + line;
			}
			return false;
		}
		if (!ParseCustomEffectAlgorithm(body.substr(second_comma + 1, third_comma - second_comma - 1), &node.algorithm)) {
			if (error_out != nullptr) {
				*error_out = "invalid custom effect algorithm in serialization: " + line;
			}
			return false;
		}
		node.stage_index = static_cast<uint32_t>(std::stoul(body.substr(third_comma + 1, fourth_comma - third_comma - 1)));
		node.plugin_reference = body.substr(fourth_comma + 1, fifth_comma - fourth_comma - 1);
		std::istringstream parameter_stream(body.substr(fifth_comma + 1));
		std::string parameter_text;
		while (std::getline(parameter_stream, parameter_text, ';')) {
			const size_t equals = parameter_text.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			SetCustomEffectParameterValue(
				&node.parameters,
				parameter_text.substr(0, equals),
				std::stof(parameter_text.substr(equals + 1)));
		}
		node.mix_curve.push_back(CustomEffectCurvePoint{0u, 1.0f});
		parsed.nodes.push_back(node);
	}

	if (!ValidateCustomEffectPackage(parsed, error_out)) {
		return false;
	}
	*package_out = parsed;
	return true;
}

}  // namespace XER::Engine::Audio::FX
