#include "cache/cache_encryption_handler.h"

#include <string>
#include <vector>

#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/any_of.hpp>

#include "helper/compression_codec.h"

namespace XER::Cache {

bool CacheEncryptionHandler::CompressText(const std::string& text,
					 std::vector<std::uint8_t>* bytes,
					 std::string* error_out) {
	if (bytes == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Compressed cache target is null";
		}
		return false;
	}

	auto compressed = XER::Common::CompressionCodec::Compress(text);
	if (!compressed.has_value()) {
		if (error_out != nullptr) {
			*error_out = "Failed to compress cache payload";
		}
		return false;
	}
	*bytes = std::move(*compressed);
	return true;
}

bool CacheEncryptionHandler::DecompressText(const std::vector<std::uint8_t>& bytes,
					   std::string* text,
					   std::string* error_out) {
	if (text == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Decompressed cache target is null";
		}
		return false;
	}

	auto decompressed = XER::Common::CompressionCodec::DecompressToString(bytes);
	if (!decompressed.has_value()) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to decompress cache payload, bytes=", bytes.size());
		}
		return false;
	}
	*text = std::move(*decompressed);
	return true;
}

}  // namespace XER::Cache
