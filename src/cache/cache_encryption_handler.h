#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Engine::Cache {

class CacheEncryptionHandler {
public:
	static bool CompressText(const std::string& text,
				 std::vector<std::uint8_t>* bytes,
				 std::string* error_out);
	static bool DecompressText(const std::vector<std::uint8_t>& bytes,
				   std::string* text,
				   std::string* error_out);
};

}  // namespace XER::Engine::Cache
