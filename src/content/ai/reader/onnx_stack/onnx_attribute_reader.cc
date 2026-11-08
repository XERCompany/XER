#include "onnx_attribute_reader.h"

#include <algorithm>
#include <string_view>

namespace XER::ModelsBuilder::Reader::Onnx {

namespace {

bool Contains(std::string_view haystack, std::string_view needle) {
	return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

std::map<std::string, std::string> OnnxAttributeReader::ReadCommonAttributes(
		const uint8_t* buffer,
		size_t size) {
	std::map<std::string, std::string> attrs;
	if (buffer == nullptr || size == 0U) {
		return attrs;
	}

	const std::string_view bytes(reinterpret_cast<const char*>(buffer), size);
	attrs["has_bias"] = Contains(bytes, "bias") ? "true" : "false";
	attrs["has_batch_norm"] = Contains(bytes, "BatchNormalization") ? "true" : "false";
	attrs["quantized"] = Contains(bytes, "QuantizeLinear") ? "true" : "false";

	if (Contains(bytes, "hidden_size")) {
		attrs["units_hint"] = "256";
	} else if (Contains(bytes, "out_features")) {
		attrs["units_hint"] = "128";
	} else {
		attrs["units_hint"] = "64";
	}

	return attrs;
}

uint32_t OnnxAttributeReader::ReadUnitsHint(const std::map<std::string, std::string>& attrs,
																						uint32_t fallback) {
	const auto it = attrs.find("units_hint");
	if (it == attrs.end()) {
		return fallback;
	}

	try {
		const unsigned long units = std::stoul(it->second);
		return units > 0U ? static_cast<uint32_t>(units) : fallback;
	} catch (...) {
		return fallback;
	}
}

}  // namespace XER::ModelsBuilder::Reader::Onnx

