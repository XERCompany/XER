#include "weight_loader.h"

#include "helper/string_utils.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#if __has_include(<zstd.h>)
#include <zstd.h>
#define XER_WGT_HAS_ZSTD 1
#else
#define XER_WGT_HAS_ZSTD 0
#endif
#if __has_include(<lz4.h>)
#include <lz4.h>
#define XER_WGT_HAS_LZ4 1
#else
#define XER_WGT_HAS_LZ4 0
#endif
#if __has_include(<snappy.h>)
#include <snappy.h>
#define XER_WGT_HAS_SNAPPY 1
#else
#define XER_WGT_HAS_SNAPPY 0
#endif
#if __has_include(<brotli/decode.h>)
#include <brotli/decode.h>
#define XER_WGT_HAS_BROTLI 1
#else
#define XER_WGT_HAS_BROTLI 0
#endif

namespace XER::ModelsBuilder::Weights {

namespace {

constexpr char kWeightMagicV1[8] = {'X', 'E', 'R', 'W', 'G', 'T', '0', '1'};
constexpr char kWeightMagicV2[8] = {'X', 'E', 'R', 'W', 'G', 'T', '0', '2'};

bool ReadU32(std::istream& in, uint32_t& value) {
	char bytes[4];
	in.read(bytes, sizeof(bytes));
	if (!in) {
		return false;
	}
	value = 0U;
	for (size_t i = 0; i < 4; ++i) {
		value |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << (i * 8U);
	}
	return true;
}

bool ReadU64(std::istream& in, uint64_t& value) {
	char bytes[8];
	in.read(bytes, sizeof(bytes));
	if (!in) {
		return false;
	}
	value = 0U;
	for (size_t i = 0; i < 8; ++i) {
		value |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[i])) << (i * 8U);
	}
	return true;
}

bool LoadF32Block(std::istream& input, std::vector<float>& target, uint64_t count) {
	if (count > (std::numeric_limits<size_t>::max() / sizeof(float))) {
		return false;
	}
	target.resize(static_cast<size_t>(count));
	if (count > 0U) {
		input.read(reinterpret_cast<char*>(target.data()),
			static_cast<std::streamsize>(count * sizeof(float)));
		if (!input) {
			return false;
		}
	}
	return true;
}

bool LoadBinary(std::istream& input, bool has_bias, std::vector<WeightTensor>& tensors) {
	uint64_t tensor_count = 0U;
	if (!ReadU64(input, tensor_count) || tensor_count > 1000000U) {
		return false;
	}

	tensors.reserve(static_cast<size_t>(tensor_count));
	for (uint64_t tensor_index = 0; tensor_index < tensor_count; ++tensor_index) {
		WeightTensor tensor{};

		uint32_t name_length = 0U;
		if (!ReadU32(input, name_length) || name_length > (1U << 20U)) {
			return false;
		}
		tensor.name.resize(name_length);
		if (name_length > 0U) {
			input.read(tensor.name.data(), static_cast<std::streamsize>(name_length));
			if (!input) {
				return false;
			}
		}

		uint32_t shape_size = 0U;
		if (!ReadU32(input, shape_size) || shape_size > 64U) {
			return false;
		}
		tensor.shape.resize(shape_size);
		for (uint32_t shape_index = 0; shape_index < shape_size; ++shape_index) {
			if (!ReadU32(input, tensor.shape[shape_index])) {
				return false;
			}
		}

		uint64_t value_count = 0U;
		if (!ReadU64(input, value_count) ||
			!LoadF32Block(input, tensor.values, value_count)) {
			return false;
		}
		if (has_bias) {
			uint64_t bias_count = 0U;
			if (!ReadU64(input, bias_count) ||
				!LoadF32Block(input, tensor.bias, bias_count)) {
				return false;
			}
		}
		tensors.push_back(std::move(tensor));
	}
	return true;
}

std::vector<WeightTensor> LoadText(std::istream& input) {
	std::vector<WeightTensor> tensors;
	size_t tensor_count = 0U;
	input >> tensor_count;
	input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	tensors.reserve(tensor_count);

	for (size_t tensor_index = 0; tensor_index < tensor_count; ++tensor_index) {
		WeightTensor tensor{};
		std::getline(input, tensor.name);

		size_t shape_size = 0U;
		input >> shape_size;
		tensor.shape.resize(shape_size, 0U);
		for (size_t shape_index = 0; shape_index < shape_size; ++shape_index) {
			input >> tensor.shape[shape_index];
		}
		input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

		size_t value_count = 0U;
		input >> value_count;
		tensor.values.resize(value_count, 0.0f);
		for (size_t value_index = 0; value_index < value_count; ++value_index) {
			input >> tensor.values[value_index];
		}
		input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		tensors.push_back(std::move(tensor));
	}
	return tensors;
}

enum class WeightCodec { None, Zstd, Lz4, Snappy, Brotli };

WeightCodec CodecForPath(const std::string& filepath) {
	namespace fs = std::filesystem;
	const std::string ext = XER::Helper::String::ToLowerAscii(fs::path(filepath).extension().string());
	if (ext == ".zst") return WeightCodec::Zstd;
	if (ext == ".lz4") return WeightCodec::Lz4;
	if (ext == ".sz" || ext == ".snappy") return WeightCodec::Snappy;
	if (ext == ".br") return WeightCodec::Brotli;
	return WeightCodec::None;
}

bool DecompressPayload(WeightCodec codec, const std::string& input, std::string& output) {
	switch (codec) {
#if XER_WGT_HAS_ZSTD
		case WeightCodec::Zstd: {
			const unsigned long long bound = ZSTD_getFrameContentSize(
				reinterpret_cast<const void*>(input.data()), input.size());
			if (bound == ZSTD_CONTENTSIZE_UNKNOWN || bound == ZSTD_CONTENTSIZE_ERROR ||
				bound > (512ULL << 20U)) {
				return false;
			}
			output.resize(static_cast<size_t>(bound));
			const size_t written = ZSTD_decompress(output.data(), bound,
				input.data(), input.size());
			if (ZSTD_isError(written)) return false;
			output.resize(written);
			return true;
		}
#endif
#if XER_WGT_HAS_LZ4
		case WeightCodec::Lz4: {
			// Decompressed size is unknown; grow geometrically from a guess.
			size_t capacity = input.size() * 4U + (1U << 20U);
			for (int attempt = 0; attempt < 8; ++attempt) {
				output.resize(capacity);
				const int written = LZ4_decompress_safe(input.data(), output.data(),
					static_cast<int>(input.size()), static_cast<int>(capacity));
				if (written >= 0) {
					output.resize(static_cast<size_t>(written));
					return true;
				}
				capacity *= 2U;
			}
			return false;
		}
#endif
#if XER_WGT_HAS_SNAPPY
		case WeightCodec::Snappy: {
			size_t length = 0;
			if (!snappy::GetUncompressedLength(input.data(), input.size(), &length) ||
				length > (512ULL << 20U)) {
				return false;
			}
			output.resize(length);
			return snappy::RawUncompress(input.data(), input.size(), output.data());
		}
#endif
#if XER_WGT_HAS_BROTLI
		case WeightCodec::Brotli: {
			size_t decoded_size = input.size() * 8U + (1U << 20U);
			for (int attempt = 0; attempt < 8; ++attempt) {
				output.resize(decoded_size);
				size_t produced = decoded_size;
				const BrotliDecoderResult result = BrotliDecoderDecompress(
					input.size(), reinterpret_cast<const uint8_t*>(input.data()),
					&produced, reinterpret_cast<uint8_t*>(output.data()));
				if (result == BROTLI_DECODER_RESULT_SUCCESS) {
					output.resize(produced);
					return true;
				}
				if (result == BROTLI_DECODER_RESULT_ERROR) {
					return false;
				}
				decoded_size *= 2U;
			}
			return false;
		}
#endif
		default:
			return false;
	}
}

} // namespace

std::vector<WeightTensor> WeightLoader::Load(const std::string& filepath) const {
	std::ifstream file(filepath, std::ios::binary);
	if (!file.is_open()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::IoFailure,
			"Failed to open weight file for reading: " + filepath,
			"ModelsBuilder::Weights::WeightLoader::Load");
		return {};
	}
	std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	std::string payload;
	const WeightCodec codec = CodecForPath(filepath);
	if (codec != WeightCodec::None) {
		if (!DecompressPayload(codec, raw, payload)) {
			Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
				Utility::ModelBuilderErrorCode::IoFailure,
				"Failed to decompress weights: " + filepath,
				"ModelsBuilder::Weights::WeightLoader::Load");
			return {};
		}
	} else {
		payload = std::move(raw);
	}

	std::istringstream input(payload, std::ios::binary);
	char magic[sizeof(kWeightMagicV2)] = {};
	input.read(magic, sizeof(magic));
	std::vector<WeightTensor> tensors;
	const bool is_v1 = std::memcmp(magic, kWeightMagicV1, sizeof(kWeightMagicV1)) == 0;
	const bool is_v2 = std::memcmp(magic, kWeightMagicV2, sizeof(kWeightMagicV2)) == 0;
	if (is_v1 || is_v2) {
		if (!LoadBinary(input, is_v2, tensors)) {
			Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
				Utility::ModelBuilderErrorCode::IoFailure,
				"Corrupt binary weight file: " + filepath,
				"ModelsBuilder::Weights::WeightLoader::Load");
			return {};
		}
	} else {
		input.clear();
		input.seekg(0, std::ios::beg);
		tensors = LoadText(input);
	}

	Utility::ModelBuilderLogger::GetInstance().Info("Loaded weights from " + filepath);
	return tensors;
}

} // namespace XER::ModelsBuilder::Weights
