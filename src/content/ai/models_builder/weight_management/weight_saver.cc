#include "weight_saver.h"

#include "helper/string_utils.h"

#include "../utility/mb_error_handler.h"
#include "../utility/mb_logger.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
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
#if __has_include(<brotli/encode.h>)
#include <brotli/encode.h>
#define XER_WGT_HAS_BROTLI 1
#else
#define XER_WGT_HAS_BROTLI 0
#endif

namespace XER::ModelsBuilder::Weights {

namespace {

// Binary container: "XERWGT02" magic, then u64 tensor count, then per tensor
// u32 name_len + name, u32 ndim + ndim*u32 dims, u64 value_count + raw f32,
// u64 bias_count + raw f32 (v2 only; v1 files end after values).
// Integers are written little-endian explicitly; float payloads are written
// as raw IEEE-754 bytes (all supported targets are little-endian).
// Optional outer compression is selected by file extension:
//   .zst -> zstd, .lz4 -> LZ4, .sz -> snappy, .br -> brotli, else raw.
constexpr char kWeightMagic[8] = {'X', 'E', 'R', 'W', 'G', 'T', '0', '2'};

void WriteU32(std::ostream& out, uint32_t value) {
	const char bytes[4] = {
		static_cast<char>(value & 0xFFU),
		static_cast<char>((value >> 8U) & 0xFFU),
		static_cast<char>((value >> 16U) & 0xFFU),
		static_cast<char>((value >> 24U) & 0xFFU)};
	out.write(bytes, sizeof(bytes));
}

void WriteU64(std::ostream& out, uint64_t value) {
	char bytes[8];
	for (size_t i = 0; i < 8; ++i) {
		bytes[i] = static_cast<char>((value >> (i * 8U)) & 0xFFU);
	}
	out.write(bytes, sizeof(bytes));
}

std::string SerializeTensors(const std::vector<WeightTensor>& tensors) {
	std::ostringstream out(std::ios::binary);
	out.write(kWeightMagic, sizeof(kWeightMagic));
	WriteU64(out, tensors.size());
	for (const WeightTensor& tensor : tensors) {
		WriteU32(out, static_cast<uint32_t>(tensor.name.size()));
		out.write(tensor.name.data(), static_cast<std::streamsize>(tensor.name.size()));
		WriteU32(out, static_cast<uint32_t>(tensor.shape.size()));
		for (const uint32_t dimension : tensor.shape) {
			WriteU32(out, dimension);
		}
		WriteU64(out, tensor.values.size());
		if (!tensor.values.empty()) {
			out.write(reinterpret_cast<const char*>(tensor.values.data()),
				static_cast<std::streamsize>(tensor.values.size() * sizeof(float)));
		}
		WriteU64(out, tensor.bias.size());
		if (!tensor.bias.empty()) {
			out.write(reinterpret_cast<const char*>(tensor.bias.data()),
				static_cast<std::streamsize>(tensor.bias.size() * sizeof(float)));
		}
	}
	return out.str();
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

bool CompressPayload(WeightCodec codec, const std::string& input, std::string& output) {
	switch (codec) {
#if XER_WGT_HAS_ZSTD
		case WeightCodec::Zstd: {
			const size_t bound = ZSTD_compressBound(input.size());
			output.resize(bound);
			const size_t written = ZSTD_compress(output.data(), bound,
				input.data(), input.size(), 3);
			if (ZSTD_isError(written)) return false;
			output.resize(written);
			return true;
		}
#endif
#if XER_WGT_HAS_LZ4
		case WeightCodec::Lz4: {
			const int bound = LZ4_compressBound(static_cast<int>(input.size()));
			output.resize(static_cast<size_t>(bound));
			const int written = LZ4_compress_default(input.data(), output.data(),
				static_cast<int>(input.size()), bound);
			if (written <= 0) return false;
			output.resize(static_cast<size_t>(written));
			return true;
		}
#endif
#if XER_WGT_HAS_SNAPPY
		case WeightCodec::Snappy:
			return snappy::Compress(input.data(), input.size(), &output);
#endif
#if XER_WGT_HAS_BROTLI
		case WeightCodec::Brotli: {
			size_t encoded_size = BrotliEncoderMaxCompressedSize(input.size());
			output.resize(encoded_size);
			if (!BrotliEncoderCompress(5, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC,
				input.size(), reinterpret_cast<const uint8_t*>(input.data()),
				&encoded_size, reinterpret_cast<uint8_t*>(output.data()))) {
				return false;
			}
			output.resize(encoded_size);
			return true;
		}
#endif
		default:
			return false;
	}
}

} // namespace

bool WeightSaver::Save(const std::string& filepath, const std::vector<WeightTensor>& tensors) const {
	const std::string payload = SerializeTensors(tensors);

	std::string output_data;
	const WeightCodec codec = CodecForPath(filepath);
	if (codec != WeightCodec::None) {
		if (!CompressPayload(codec, payload, output_data)) {
			Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
				Utility::ModelBuilderErrorCode::IoFailure,
				"Failed to compress weights for: " + filepath,
				"ModelsBuilder::Weights::WeightSaver::Save");
			return false;
		}
	} else {
		output_data = payload;
	}

	std::ofstream output(filepath, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) {
		Utility::ModelBuilderErrorHandler::GetInstance().ReportError(
			Utility::ModelBuilderErrorCode::IoFailure,
			"Failed to open weight file for writing: " + filepath,
			"ModelsBuilder::Weights::WeightSaver::Save");
		return false;
	}

	output.write(output_data.data(), static_cast<std::streamsize>(output_data.size()));
	const bool ok = output.good();
	Utility::ModelBuilderLogger::GetInstance().Info("Saved weights to " + filepath);
	return ok;
}

} // namespace XER::ModelsBuilder::Weights
