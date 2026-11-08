#include "security_flash/token_store.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <json/json.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace XER::SecurityFlash::Detail {
namespace {

constexpr char kTokenMagic[4] = {'X', 'S', 'F', 'T'};
constexpr std::uint32_t kTokenVersion = 1u;
constexpr std::uint32_t kCipherAes256Gcm = 1u;
constexpr std::uint32_t kPbkdf2Iterations = 200000u;
constexpr std::uint32_t kMaxAcceptedIterations = 1000000u;
constexpr std::uint32_t kFlagVolumeBound = 0x00000001u;
constexpr std::uint64_t kMaxPayloadSize = 1024u * 1024u;
constexpr std::size_t kKeySize = 32;
constexpr std::size_t kIvSize = 12;
constexpr std::size_t kTagSize = 16;
constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kHeaderSize = 72;

#pragma pack(push, 1)
struct TokenFileHeader {
	char magic[4] = {};
	std::uint32_t version = kTokenVersion;
	std::uint32_t flags = 0;
	std::uint64_t payload_size = 0;
	std::uint32_t kdf_iterations = kPbkdf2Iterations;
	std::uint32_t cipher = kCipherAes256Gcm;
	std::uint8_t salt[kSaltSize] = {};
	std::uint8_t iv[kIvSize] = {};
	std::uint8_t tag[kTagSize] = {};
};
#pragma pack(pop)

static_assert(sizeof(TokenFileHeader) == kHeaderSize);

void SetError(std::string* error_out, const std::string& message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

bool FillRandomBytes(std::uint8_t* output, std::size_t size) {
	return RAND_bytes(output, static_cast<int>(size)) == 1;
}

bool DeriveKey(std::string_view passphrase,
	       const std::uint8_t* salt,
	       std::size_t salt_size,
	       std::uint32_t iterations,
	       std::array<std::uint8_t, kKeySize>* key_out) {
	return PKCS5_PBKDF2_HMAC(
		       passphrase.data(),
		       static_cast<int>(passphrase.size()),
		       salt,
		       static_cast<int>(salt_size),
		       static_cast<int>(iterations),
		       EVP_sha256(),
		       static_cast<int>(key_out->size()),
		       key_out->data()) == 1;
}

// AAD binds the header metadata into the GCM tag: magic, version,
// flags and payload_size (first 20 bytes of the serialized header).
std::array<std::uint8_t, 20> BuildAad(const TokenFileHeader& header) {
	std::array<std::uint8_t, 20> aad = {};
	std::memcpy(aad.data(), header.magic, 4);
	std::memcpy(aad.data() + 4, &header.version, sizeof(header.version));
	std::memcpy(aad.data() + 8, &header.flags, sizeof(header.flags));
	std::memcpy(aad.data() + 12, &header.payload_size, sizeof(header.payload_size));
	return aad;
}

std::string SerializePayloadJson(const TokenPayload& payload) {
	Json::Value root;
	root["v"] = 1;
	root["username"] = payload.username;
	root["password"] = payload.password;
	root["created_utc"] = payload.created_utc;
	root["host_name"] = payload.host_name;
	if (payload.volume_serial != 0) {
		root["volume_serial"] = Json::UInt64(payload.volume_serial);
	}

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	return Json::writeString(writer, root);
}

std::string GetJsonString(const Json::Value& root, const char* key) {
	const Json::Value& value = root[key];
	return value.isString() ? value.asString() : std::string();
}

bool DeserializePayloadJson(const std::string& json,
			    TokenPayload* payload_out) {
	Json::CharReaderBuilder reader_builder;
	std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());

	std::string errors;
	Json::Value root;
	if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)
	    || !root.isObject()) {
		return false;
	}

	// Guard every field: asString()/asLargestUInt() assert on type
	// mismatch, and the JSON only passes GCM auth — still, be strict.
	const Json::Value& username = root["username"];
	const Json::Value& password = root["password"];
	if (!username.isString() || !password.isString()) {
		return false;
	}

	payload_out->username = username.asString();
	payload_out->password = password.asString();
	payload_out->created_utc = GetJsonString(root, "created_utc");
	payload_out->host_name = GetJsonString(root, "host_name");

	payload_out->volume_serial = 0;
	const Json::Value& serial = root["volume_serial"];
	if (serial.isUInt64() || serial.isUInt() || serial.isInt64()
	    || serial.isInt()) {
		payload_out->volume_serial =
			static_cast<std::uint32_t>(serial.asLargestUInt());
	}
	return !payload_out->username.empty();
}

bool EncryptAes256Gcm(const std::vector<std::uint8_t>& plaintext,
		      const std::string& passphrase,
		      TokenFileHeader* header,
		      std::vector<std::uint8_t>* ciphertext_out) {
	if (!FillRandomBytes(header->salt, sizeof(header->salt))
	    || !FillRandomBytes(header->iv, sizeof(header->iv))) {
		return false;
	}

	std::array<std::uint8_t, kKeySize> key = {};
	if (!DeriveKey(passphrase, header->salt, sizeof(header->salt),
			header->kdf_iterations, &key)) {
		OPENSSL_cleanse(key.data(), key.size());
		return false;
	}

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == nullptr) {
		OPENSSL_cleanse(key.data(), key.size());
		return false;
	}

	// GCM is length-preserving: ciphertext.size() == plaintext.size(),
	// so payload_size is already correct for the AAD and the header.
	header->payload_size = plaintext.size();

	ciphertext_out->assign(plaintext.size(), 0u);
	int out_len = 0;
	int final_len = 0;
	const auto aad = BuildAad(*header);
	bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
				       static_cast<int>(sizeof(header->iv)), nullptr) == 1
		&& EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), header->iv) == 1
		&& EVP_EncryptUpdate(ctx, nullptr, &out_len, aad.data(),
				     static_cast<int>(aad.size())) == 1
		&& EVP_EncryptUpdate(ctx, ciphertext_out->data(), &out_len,
				     plaintext.data(),
				     static_cast<int>(plaintext.size())) == 1
		&& EVP_EncryptFinal_ex(ctx, ciphertext_out->data() + out_len,
				       &final_len) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
				       static_cast<int>(sizeof(header->tag)),
				       header->tag) == 1;

	EVP_CIPHER_CTX_free(ctx);
	OPENSSL_cleanse(key.data(), key.size());
	if (!ok) {
		return false;
	}

	ciphertext_out->resize(static_cast<std::size_t>(out_len + final_len));
	return true;
}

bool DecryptAes256Gcm(const std::vector<std::uint8_t>& ciphertext,
		      const TokenFileHeader& header,
		      const std::string& passphrase,
		      std::vector<std::uint8_t>* plaintext_out) {
	std::array<std::uint8_t, kKeySize> key = {};
	if (!DeriveKey(passphrase, header.salt, sizeof(header.salt),
			header.kdf_iterations, &key)) {
		OPENSSL_cleanse(key.data(), key.size());
		return false;
	}

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == nullptr) {
		OPENSSL_cleanse(key.data(), key.size());
		return false;
	}

	plaintext_out->assign(ciphertext.size(), 0u);
	int out_len = 0;
	int final_len = 0;
	const auto aad = BuildAad(header);
	bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
				       static_cast<int>(sizeof(header.iv)), nullptr) == 1
		&& EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), header.iv) == 1
		&& EVP_DecryptUpdate(ctx, nullptr, &out_len, aad.data(),
				     static_cast<int>(aad.size())) == 1
		&& EVP_DecryptUpdate(ctx, plaintext_out->data(), &out_len,
				     ciphertext.data(),
				     static_cast<int>(ciphertext.size())) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
				       static_cast<int>(sizeof(header.tag)),
				       const_cast<std::uint8_t*>(header.tag)) == 1
		&& EVP_DecryptFinal_ex(ctx, plaintext_out->data() + out_len,
				       &final_len) == 1;

	EVP_CIPHER_CTX_free(ctx);
	OPENSSL_cleanse(key.data(), key.size());
	if (!ok) {
		// GCM may have emitted partial plaintext before auth failed.
		OPENSSL_cleanse(plaintext_out->data(), plaintext_out->size());
		plaintext_out->clear();
		return false;
	}

	plaintext_out->resize(static_cast<std::size_t>(out_len + final_len));
	return true;
}

}  // namespace

bool BuildTokenFile(const TokenPayload& payload,
		    const std::string& passphrase,
		    std::vector<std::uint8_t>* file_bytes_out,
		    std::string* error_out) {
	if (file_bytes_out == nullptr) {
		SetError(error_out, "file_bytes_out must not be null");
		return false;
	}
	if (payload.username.empty()) {
		SetError(error_out, "token payload requires a username");
		return false;
	}
	if (passphrase.empty()) {
		SetError(error_out, "token passphrase must not be empty");
		return false;
	}

	std::string json = SerializePayloadJson(payload);
	std::vector<std::uint8_t> plaintext(json.begin(), json.end());
	OPENSSL_cleanse(json.data(), json.size());
	if (plaintext.size() > kMaxPayloadSize) {
		SetError(error_out, "token payload exceeds maximum size");
		OPENSSL_cleanse(plaintext.data(), plaintext.size());
		return false;
	}

	TokenFileHeader header;
	std::memcpy(header.magic, kTokenMagic, sizeof(kTokenMagic));
	header.version = kTokenVersion;
	header.flags = (payload.volume_serial != 0) ? kFlagVolumeBound : 0u;
	header.kdf_iterations = kPbkdf2Iterations;
	header.cipher = kCipherAes256Gcm;

	std::vector<std::uint8_t> ciphertext;
	if (!EncryptAes256Gcm(plaintext, passphrase, &header, &ciphertext)) {
		SetError(error_out, "AES-256-GCM encryption failed");
		OPENSSL_cleanse(plaintext.data(), plaintext.size());
		return false;
	}
	OPENSSL_cleanse(plaintext.data(), plaintext.size());

	file_bytes_out->clear();
	file_bytes_out->reserve(kHeaderSize + ciphertext.size());
	const auto* header_bytes = reinterpret_cast<const std::uint8_t*>(&header);
	file_bytes_out->insert(file_bytes_out->end(), header_bytes,
			       header_bytes + kHeaderSize);
	file_bytes_out->insert(file_bytes_out->end(), ciphertext.begin(), ciphertext.end());

	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

bool ParseTokenFile(const std::vector<std::uint8_t>& file_bytes,
		    const std::string& passphrase,
		    TokenPayload* payload_out,
		    std::string* error_out) {
	if (payload_out == nullptr) {
		SetError(error_out, "payload_out must not be null");
		return false;
	}
	if (file_bytes.size() < kHeaderSize + 1u) {
		SetError(error_out, "token file is too small");
		return false;
	}
	if (passphrase.empty()) {
		SetError(error_out, "token passphrase must not be empty");
		return false;
	}

	TokenFileHeader header;
	std::memcpy(&header, file_bytes.data(), kHeaderSize);

	if (std::memcmp(header.magic, kTokenMagic, sizeof(kTokenMagic)) != 0) {
		SetError(error_out, "not a SecurityFlash token file");
		return false;
	}
	if (header.version != kTokenVersion) {
		SetError(error_out, absl::StrFormat(
			"unsupported token version %u", header.version));
		return false;
	}
	if (header.cipher != kCipherAes256Gcm) {
		SetError(error_out, absl::StrFormat(
			"unsupported token cipher %u", header.cipher));
		return false;
	}
	if (header.kdf_iterations == 0
	    || header.kdf_iterations > kMaxAcceptedIterations) {
		SetError(error_out, "token KDF parameters out of range");
		return false;
	}
	if (header.payload_size == 0
	    || header.payload_size > kMaxPayloadSize
	    || file_bytes.size() != kHeaderSize + header.payload_size) {
		SetError(error_out, "token payload size mismatch");
		return false;
	}

	const std::vector<std::uint8_t> ciphertext(
		file_bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
		file_bytes.end());

	std::vector<std::uint8_t> plaintext;
	if (!DecryptAes256Gcm(ciphertext, header, passphrase, &plaintext)) {
		SetError(error_out,
			 "token decryption failed (wrong passphrase or corrupted file)");
		OPENSSL_cleanse(plaintext.data(), plaintext.size());
		return false;
	}

	std::string json(plaintext.begin(), plaintext.end());
	OPENSSL_cleanse(plaintext.data(), plaintext.size());

	TokenPayload payload;
	const bool parsed = DeserializePayloadJson(json, &payload);
	OPENSSL_cleanse(json.data(), json.size());
	if (!parsed) {
		SetError(error_out, "token payload is malformed");
		return false;
	}

	// Header flag and payload field must agree on binding.
	const bool header_bound = (header.flags & kFlagVolumeBound) != 0;
	const bool payload_bound = payload.volume_serial != 0;
	if (header_bound != payload_bound) {
		SetError(error_out, "token binding metadata mismatch");
		return false;
	}

	*payload_out = std::move(payload);
	if (error_out != nullptr) {
		error_out->clear();
	}
	return true;
}

}  // namespace XER::SecurityFlash::Detail
