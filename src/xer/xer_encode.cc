#include "xer/xer_encode.h"

#include "helper/env_utils.h"
#include "helper/tool_to.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Xer {

namespace {

constexpr std::uint32_t kLegacyContainerVersion = 0x00020001u;
constexpr std::uint32_t kContainerMagic = 0x32425853u;
constexpr std::uint32_t kContainerVersion = 0x00030001u;
constexpr std::uint32_t kContainerFlagEncrypted = 0x00000001u;
constexpr std::uint32_t kMaxSectionCount = 8u;
constexpr std::uint32_t kCryptoSectionMagic = 0x4F525843u;
constexpr std::uint32_t kCryptoSectionVersion = 1u;
constexpr std::uint32_t kCipherAes256Gcm = 1u;
constexpr std::uint32_t kPbkdf2Iterations = 200000u;

struct BinHeader {
    std::uint32_t magic = kContainerMagic;
    std::uint32_t version = kContainerVersion;
    std::uint64_t nonce = 0;
    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;
    std::uint32_t source_crc = 0;
    std::uint32_t payload_crc = 0;
    std::uint32_t manifest_crc = 0;
    std::uint32_t header_crc = 0;
    std::uint32_t section_count = 2u;
    std::uint32_t flags = 0;
};

struct BinSection {
    char name[16] = {};
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
    std::uint32_t flags = 0;
};

struct CryptoSection {
    std::uint32_t magic = kCryptoSectionMagic;
    std::uint32_t version = kCryptoSectionVersion;
    std::uint32_t kdf_iterations = kPbkdf2Iterations;
    std::uint32_t cipher = kCipherAes256Gcm;
    std::uint8_t salt[16] = {};
    std::uint8_t iv[12] = {};
    std::uint8_t tag[16] = {};
};

bool ReadTextFile(const std::filesystem::path& path, std::string* text_out, std::string* error_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error_out != nullptr) {
            *error_out = absl::StrCat("unable to open '", path.string(), "'");
        }
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        if (error_out != nullptr) {
            *error_out = absl::StrCat("failed to read '", path.string(), "'");
        }
        return false;
    }

    *text_out = buffer.str();
    if (error_out != nullptr) {
        error_out->clear();
    }
    return true;
}

std::string TrimAsciiWhitespace(std::string text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string SectionName(const BinSection& section) {
    std::size_t name_size = 0;
    while (name_size < sizeof(section.name) && section.name[name_size] != '\0') {
        ++name_size;
    }
    return std::string(section.name, name_size);
}

std::uint64_t MakeNonce(std::size_t input_size) {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return static_cast<std::uint64_t>(now) ^ (static_cast<std::uint64_t>(input_size) << 17u) ^ 0x84321BADC0DE55AAULL;
}

std::string BuildManifest(
    std::string_view logical_name,
    std::size_t original_size,
    std::uint64_t nonce,
    std::uint32_t source_crc,
    std::uint32_t payload_crc,
    bool encrypted,
    std::uint32_t kdf_iterations,
    const XerCompileResult* compile_result) {
    std::string manifest;
    absl::StrAppend(&manifest, "SXER84321\n");
    absl::StrAppendFormat(&manifest, "name=%s\n", logical_name);
    absl::StrAppendFormat(&manifest, "original_size=%zu\n", original_size);
    absl::StrAppendFormat(&manifest, "nonce=%llu\n", static_cast<unsigned long long>(nonce));
    absl::StrAppendFormat(&manifest, "source_crc=%u\n", source_crc);
    absl::StrAppendFormat(&manifest, "payload_crc=%u\n", payload_crc);
    absl::StrAppendFormat(&manifest, "cipher=%s\n", encrypted ? "aes-256-gcm" : "none");
    if (encrypted) {
        absl::StrAppend(&manifest, "kdf=pbkdf2-hmac-sha256\n");
        absl::StrAppendFormat(&manifest, "kdf_iterations=%u\n", kdf_iterations);
    }
    if (compile_result != nullptr) {
        absl::StrAppendFormat(&manifest, "token_count=%zu\n", compile_result->tokens.size());
        absl::StrAppendFormat(&manifest, "import_count=%zu\n", compile_result->program.imports.size());
        absl::StrAppendFormat(&manifest, "function_count=%zu\n", compile_result->program.functions.size());
    }
    return manifest;
}

bool FillRandomBytes(std::uint8_t* output, std::size_t size, std::string* error_out) {
    if (RAND_bytes(output, static_cast<int>(size)) == 1) {
        return true;
    }
    if (error_out != nullptr) {
        *error_out = "OpenSSL RAND_bytes failed";
    }
    return false;
}

bool DeriveEncryptionKey(
    std::string_view secret,
    const std::uint8_t* salt,
    std::size_t salt_size,
    std::uint32_t iterations,
    std::array<unsigned char, 32>* key_out,
    std::string* error_out) {
    if (secret.empty()) {
        if (error_out != nullptr) {
            *error_out = "XER encryption key is empty";
        }
        return false;
    }

    if (PKCS5_PBKDF2_HMAC(
            secret.data(),
            static_cast<int>(secret.size()),
            salt,
            static_cast<int>(salt_size),
            static_cast<int>(iterations),
            EVP_sha256(),
            static_cast<int>(key_out->size()),
            key_out->data()) == 1) {
        return true;
    }

    if (error_out != nullptr) {
        *error_out = "OpenSSL PBKDF2 key derivation failed";
    }
    return false;
}

std::array<std::uint8_t, 24> BuildEncryptionAad(
    std::uint64_t nonce,
    std::uint64_t original_size,
    std::uint32_t source_crc,
    std::uint32_t payload_crc) {
    std::array<std::uint8_t, 24> aad = {};
    std::memcpy(aad.data(), &nonce, sizeof(nonce));
    std::memcpy(aad.data() + sizeof(nonce), &original_size, sizeof(original_size));
    std::memcpy(aad.data() + sizeof(nonce) + sizeof(original_size), &source_crc, sizeof(source_crc));
    std::memcpy(aad.data() + sizeof(nonce) + sizeof(original_size) + sizeof(source_crc), &payload_crc, sizeof(payload_crc));
    return aad;
}

bool EncryptPayload(
    const std::vector<std::uint8_t>& plaintext,
    const XerProtectionOptions& options,
    std::uint64_t nonce,
    std::uint64_t original_size,
    std::uint32_t source_crc,
    std::uint32_t payload_crc,
    CryptoSection* crypto_out,
    std::vector<std::uint8_t>* ciphertext_out,
    std::string* error_out) {
    if (crypto_out == nullptr || ciphertext_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "invalid encryption output buffers";
        }
        return false;
    }

    if (!FillRandomBytes(crypto_out->salt, sizeof(crypto_out->salt), error_out)
        || !FillRandomBytes(crypto_out->iv, sizeof(crypto_out->iv), error_out)) {
        return false;
    }

    std::array<unsigned char, 32> key = {};
    if (!DeriveEncryptionKey(
            options.encryption_key,
            crypto_out->salt,
            sizeof(crypto_out->salt),
            crypto_out->kdf_iterations,
            &key,
            error_out)) {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        if (error_out != nullptr) {
            *error_out = "OpenSSL EVP_CIPHER_CTX_new failed";
        }
        return false;
    }

    ciphertext_out->assign(plaintext.size(), 0u);
    int out_len = 0;
    int final_len = 0;
    const auto aad = BuildEncryptionAad(nonce, original_size, source_crc, payload_crc);
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(crypto_out->iv), nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), crypto_out->iv) == 1
        && EVP_EncryptUpdate(ctx, nullptr, &out_len, aad.data(), static_cast<int>(aad.size())) == 1
        && EVP_EncryptUpdate(
            ctx,
            ciphertext_out->data(),
            &out_len,
            plaintext.data(),
            static_cast<int>(plaintext.size())) == 1
        && EVP_EncryptFinal_ex(ctx, ciphertext_out->data() + out_len, &final_len) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(crypto_out->tag), crypto_out->tag) == 1;

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        if (error_out != nullptr) {
            *error_out = "OpenSSL AES-256-GCM encryption failed";
        }
        return false;
    }

    ciphertext_out->resize(static_cast<std::size_t>(out_len + final_len));
    return true;
}

bool DecryptPayload(
    const std::vector<std::uint8_t>& ciphertext,
    const XerProtectionOptions& options,
    std::uint64_t nonce,
    std::uint64_t original_size,
    std::uint32_t source_crc,
    std::uint32_t payload_crc,
    const CryptoSection& crypto,
    std::vector<std::uint8_t>* plaintext_out,
    std::string* error_out) {
    if (options.encryption_key.empty()) {
        if (error_out != nullptr) {
            *error_out = "encrypted XER container requires --xer_key or --xer_key_env";
        }
        return false;
    }

    std::array<unsigned char, 32> key = {};
    if (!DeriveEncryptionKey(
            options.encryption_key,
            crypto.salt,
            sizeof(crypto.salt),
            crypto.kdf_iterations,
            &key,
            error_out)) {
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        if (error_out != nullptr) {
            *error_out = "OpenSSL EVP_CIPHER_CTX_new failed";
        }
        return false;
    }

    plaintext_out->assign(ciphertext.size(), 0u);
    int out_len = 0;
    int final_len = 0;
    const auto aad = BuildEncryptionAad(nonce, original_size, source_crc, payload_crc);
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(crypto.iv), nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), crypto.iv) == 1
        && EVP_DecryptUpdate(ctx, nullptr, &out_len, aad.data(), static_cast<int>(aad.size())) == 1
        && EVP_DecryptUpdate(
            ctx,
            plaintext_out->data(),
            &out_len,
            ciphertext.data(),
            static_cast<int>(ciphertext.size())) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof(crypto.tag), const_cast<std::uint8_t*>(crypto.tag)) == 1;

    if (ok) {
        ok = EVP_DecryptFinal_ex(ctx, plaintext_out->data() + out_len, &final_len) == 1;
    }

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        if (error_out != nullptr) {
            *error_out = "OpenSSL AES-256-GCM decrypt/auth failed";
        }
        return false;
    }

    plaintext_out->resize(static_cast<std::size_t>(out_len + final_len));
    return true;
}

bool SectionNameEquals(const BinSection& section, const char* name) {
    return std::strncmp(section.name, name, sizeof(section.name)) == 0;
}

const BinSection* FindSection(const std::vector<BinSection>& sections, const char* name) {
    for (const BinSection& section : sections) {
        if (SectionNameEquals(section, name)) {
            return &section;
        }
    }
    return nullptr;
}

bool ReadSectionBytes(
    const XerBuffer& payload,
    const BinSection& section,
    std::vector<std::uint8_t>* bytes_out,
    std::string* error_out) {
    if (section.offset + section.size > payload.size()) {
        if (error_out != nullptr) {
            *error_out = absl::StrFormat("section '%s' exceeds container size", section.name);
        }
        return false;
    }

    bytes_out->assign(
        payload.bytes().begin() + static_cast<std::ptrdiff_t>(section.offset),
        payload.bytes().begin() + static_cast<std::ptrdiff_t>(section.offset + section.size));

    if (section.crc != 0u && sxer84321_checksum(bytes_out->data(), bytes_out->size()) != section.crc) {
        if (error_out != nullptr) {
            *error_out = absl::StrFormat("section '%s' failed checksum validation", section.name);
        }
        return false;
    }
    return true;
}

bool ValidateHeader(const XerBuffer& payload, const BinHeader& header, std::string* error_out) {
    if (header.magic != kContainerMagic) {
        if (error_out != nullptr) {
            *error_out = "invalid sxer container header";
        }
        return false;
    }

    if (header.version != kLegacyContainerVersion && header.version != kContainerVersion) {
        if (error_out != nullptr) {
            *error_out = "unsupported sxer container version";
        }
        return false;
    }

    if (header.section_count < 2u || header.section_count > kMaxSectionCount) {
        if (error_out != nullptr) {
            *error_out = "invalid sxer section count";
        }
        return false;
    }

    const std::size_t table_bytes = sizeof(BinHeader) + sizeof(BinSection) * header.section_count;
    if (payload.size() < table_bytes) {
        if (error_out != nullptr) {
            *error_out = "container is too small for section table";
        }
        return false;
    }

    if (header.header_crc != 0u) {
        std::vector<std::uint8_t> header_bytes(table_bytes);
        BinHeader crc_header = header;
        crc_header.header_crc = 0u;
        std::memcpy(header_bytes.data(), &crc_header, sizeof(crc_header));
        std::memcpy(
            header_bytes.data() + sizeof(crc_header),
            payload.bytes().data() + sizeof(crc_header),
            table_bytes - sizeof(crc_header));
        if (sxer84321_checksum(header_bytes.data(), header_bytes.size()) != header.header_crc) {
            if (error_out != nullptr) {
                *error_out = "container header checksum mismatch";
            }
            return false;
        }
    }

    return true;
}

bool ReadContainerHeaderAndSections(
    const XerBuffer& payload,
    BinHeader* header_out,
    std::vector<BinSection>* sections_out,
    std::string* error_out) {
    if (payload.size() < sizeof(BinHeader) + sizeof(BinSection) * 2u) {
        if (error_out != nullptr) {
            *error_out = "container is too small";
        }
        return false;
    }

    BinHeader header;
    std::memcpy(&header, payload.bytes().data(), sizeof(header));
    if (!ValidateHeader(payload, header, error_out)) {
        return false;
    }

    std::vector<BinSection> sections(header.section_count);
    std::memcpy(
        sections.data(),
        payload.bytes().data() + sizeof(header),
        sizeof(BinSection) * header.section_count);

    *header_out = header;
    *sections_out = std::move(sections);
    if (error_out != nullptr) {
        error_out->clear();
    }
    return true;
}

XerEncodedBlock EncodeInternal(
    const XerBuffer& buffer,
    std::string_view logical_name,
    XerProtectionOptions options,
    const XerCompileResult* compile_result,
    std::string* error_out) {
    XerEncodedBlock block;
    block.algorithm = "sxer84321";
    block.encrypted = options.HasEncryption();
    block.encryption = block.encrypted ? "aes-256-gcm" : "none";
    block.nonce = MakeNonce(buffer.size());

    std::vector<std::uint8_t> encoded(sxer84321_max_encoded_size(buffer.size()));
    std::size_t encoded_size = 0;
    const Sxer84321Status status = sxer84321_encode(
        buffer.bytes().data(),
        buffer.size(),
        block.nonce,
        encoded.data(),
        encoded.size(),
        &encoded_size,
        &block.source_crc,
        &block.payload_crc);
    if (status != SXER84321_STATUS_OK) {
        if (error_out != nullptr) {
            *error_out = absl::StrCat("sxer84321 encode failed: ", sxer84321_status_string(status));
        }
        return XerEncodedBlock();
    }
    encoded.resize(encoded_size);

    std::vector<std::uint8_t> payload_bytes = encoded;
    CryptoSection crypto_section_data;
    if (options.HasEncryption()) {
        if (!EncryptPayload(
                encoded,
                options,
                block.nonce,
                buffer.size(),
                block.source_crc,
                block.payload_crc,
                &crypto_section_data,
                &payload_bytes,
                error_out)) {
            return XerEncodedBlock();
        }
    }

    block.bak_manifest = BuildManifest(
        logical_name,
        buffer.size(),
        block.nonce,
        block.source_crc,
        block.payload_crc,
        options.HasEncryption(),
        crypto_section_data.kdf_iterations,
        compile_result);

    BinHeader header;
    header.nonce = block.nonce;
    header.original_size = buffer.size();
    header.payload_size = payload_bytes.size();
    header.source_crc = block.source_crc;
    header.payload_crc = block.payload_crc;
    header.section_count = options.HasEncryption() ? 3u : 2u;
    header.flags = options.HasEncryption() ? kContainerFlagEncrypted : 0u;
    header.manifest_crc = sxer84321_checksum(
        reinterpret_cast<const std::uint8_t*>(block.bak_manifest.data()),
        block.bak_manifest.size());

    BinSection manifest_section;
    std::memcpy(manifest_section.name, "manifest", 8);
    BinSection payload_section;
    std::memcpy(payload_section.name, "payload", 7);
    BinSection crypto_section;
    if (options.HasEncryption()) {
        std::memcpy(crypto_section.name, "crypto", 6);
    }

    const std::uint64_t header_bytes = sizeof(BinHeader) + sizeof(BinSection) * header.section_count;
    manifest_section.offset = header_bytes;
    manifest_section.size = block.bak_manifest.size();
    manifest_section.crc = header.manifest_crc;

    std::uint64_t next_offset = manifest_section.offset + manifest_section.size;
    if (options.HasEncryption()) {
        crypto_section.offset = next_offset;
        crypto_section.size = sizeof(CryptoSection);
        crypto_section.crc = sxer84321_checksum(
            reinterpret_cast<const std::uint8_t*>(&crypto_section_data),
            sizeof(CryptoSection));
        next_offset += crypto_section.size;
    }

    payload_section.offset = next_offset;
    payload_section.size = payload_bytes.size();
    payload_section.crc = payload_bytes.empty() ? 0u : sxer84321_checksum(payload_bytes.data(), payload_bytes.size());

    std::vector<std::uint8_t> container(payload_section.offset + payload_section.size);
    std::memcpy(container.data(), &header, sizeof(header));
    std::memcpy(container.data() + sizeof(header), &manifest_section, sizeof(manifest_section));
    std::size_t section_table_offset = sizeof(header) + sizeof(manifest_section);
    if (options.HasEncryption()) {
        std::memcpy(container.data() + section_table_offset, &crypto_section, sizeof(crypto_section));
        section_table_offset += sizeof(crypto_section);
    }
    std::memcpy(container.data() + section_table_offset, &payload_section, sizeof(payload_section));
    std::memcpy(container.data() + manifest_section.offset, block.bak_manifest.data(), block.bak_manifest.size());
    if (options.HasEncryption()) {
        std::memcpy(container.data() + crypto_section.offset, &crypto_section_data, sizeof(crypto_section_data));
    }
    std::memcpy(container.data() + payload_section.offset, payload_bytes.data(), payload_bytes.size());

    header.header_crc = sxer84321_checksum(container.data(), sizeof(header) + sizeof(BinSection) * header.section_count);
    std::memcpy(container.data(), &header, sizeof(header));

    block.bin_payload = XerBuffer(std::move(container));
    if (error_out != nullptr) {
        error_out->clear();
    }
    return block;
}

}  // namespace

XerProtectionOptions ResolveProtectionOptionsFromEnvironment(std::string* error_out) {
    XerProtectionOptions options;

    const std::string direct_key = XER::Helper::GetEnv("ENGINE_XER_KEY");
    if (!direct_key.empty()) {
        options.encryption_key = direct_key;
        if (error_out != nullptr) {
            error_out->clear();
        }
        return options;
    }

    const std::string key_file_path = XER::Helper::GetEnv("ENGINE_XER_KEY_FILE");
    if (!key_file_path.empty()) {
        std::string key_text;
        if (!ReadTextFile(key_file_path, &key_text, error_out)) {
            return options;
        }
        options.encryption_key = TrimAsciiWhitespace(std::move(key_text));
        if (options.encryption_key.empty()) {
            if (error_out != nullptr) {
                *error_out = absl::StrCat("XER key file is empty: ", key_file_path);
            }
            return XerProtectionOptions();
        }
        if (error_out != nullptr) {
            error_out->clear();
        }
        return options;
    }

    const std::string key_env_name = XER::Helper::GetEnv("ENGINE_XER_KEY_ENV");
    if (!key_env_name.empty()) {
        const std::string resolved_key = XER::Helper::GetEnv(key_env_name);
        if (resolved_key.empty()) {
            if (error_out != nullptr) {
                *error_out = absl::StrCat("environment variable '", key_env_name, "' is not set");
            }
            return options;
        }
        options.encryption_key = resolved_key;
    }

    if (error_out != nullptr) {
        error_out->clear();
    }
    return options;
}

XerEncodedBlock XerEncode::Encode(
    const XerBuffer& buffer,
    std::string_view logical_name,
    XerProtectionOptions options) const {
    return EncodeInternal(buffer, logical_name, std::move(options), nullptr, nullptr);
}

XerEncodedBlock XerEncode::Compile(
    const std::filesystem::path& source_path,
    XerProtectionOptions options,
    std::string* error_out) const {
    XerCompiler compiler;
    std::string compile_error;
    const XerCompileResult result = compiler.CompileFromFile(source_path, &compile_error);
    if (result.normalized_source.empty() && !std::filesystem::exists(source_path)) {
        if (error_out != nullptr) {
            *error_out = compile_error.empty() ? "source file does not exist" : compile_error;
        }
        return XerEncodedBlock();
    }
    return EncodeInternal(
        XerBuffer::FromString(result.normalized_source),
        source_path.filename().string(),
        std::move(options),
        &result,
        error_out);
}

XerBuffer XerEncode::Decode(const XerBuffer& payload, XerProtectionOptions options, std::string* error_out) const {
    BinHeader header;
    std::vector<BinSection> sections;
    if (!ReadContainerHeaderAndSections(payload, &header, &sections, error_out)) {
        return XerBuffer();
    }

    const BinSection* payload_section = FindSection(sections, "payload");
    if (payload_section == nullptr) {
        if (error_out != nullptr) {
            *error_out = "payload section is missing";
        }
        return XerBuffer();
    }

    std::vector<std::uint8_t> payload_bytes;
    if (!ReadSectionBytes(payload, *payload_section, &payload_bytes, error_out)) {
        return XerBuffer();
    }

    if ((header.flags & kContainerFlagEncrypted) != 0u) {
        const BinSection* crypto_section = FindSection(sections, "crypto");
        if (crypto_section == nullptr) {
            if (error_out != nullptr) {
                *error_out = "crypto section is missing for encrypted container";
            }
            return XerBuffer();
        }

        std::vector<std::uint8_t> crypto_bytes;
        if (!ReadSectionBytes(payload, *crypto_section, &crypto_bytes, error_out)) {
            return XerBuffer();
        }
        if (crypto_bytes.size() != sizeof(CryptoSection)) {
            if (error_out != nullptr) {
                *error_out = "crypto section has invalid size";
            }
            return XerBuffer();
        }

        CryptoSection crypto = {};
        std::memcpy(&crypto, crypto_bytes.data(), sizeof(crypto));
        if (crypto.magic != kCryptoSectionMagic || crypto.version != kCryptoSectionVersion || crypto.cipher != kCipherAes256Gcm) {
            if (error_out != nullptr) {
                *error_out = "unsupported crypto section metadata";
            }
            return XerBuffer();
        }

        std::vector<std::uint8_t> decrypted_payload;
        if (!DecryptPayload(
                payload_bytes,
                options,
                header.nonce,
                header.original_size,
                header.source_crc,
                header.payload_crc,
                crypto,
                &decrypted_payload,
                error_out)) {
            return XerBuffer();
        }
        payload_bytes = std::move(decrypted_payload);
    }

    if (!payload_bytes.empty() && sxer84321_checksum(payload_bytes.data(), payload_bytes.size()) != header.payload_crc) {
        if (error_out != nullptr) {
            *error_out = "decoded payload checksum mismatch";
        }
        return XerBuffer();
    }

    std::vector<std::uint8_t> decoded(header.original_size);
    std::size_t decoded_size = 0;
    const Sxer84321Status status = sxer84321_decode(
        payload_bytes.data(),
        payload_bytes.size(),
        header.nonce,
        decoded.data(),
        decoded.size(),
        &decoded_size,
        header.source_crc,
        header.payload_crc);
    if (status != SXER84321_STATUS_OK) {
        if (error_out != nullptr) {
            *error_out = absl::StrCat("sxer84321 decode failed: ", sxer84321_status_string(status));
        }
        return XerBuffer();
    }

    decoded.resize(decoded_size);
    if (error_out != nullptr) {
        error_out->clear();
    }
    return XerBuffer(std::move(decoded));
}

bool XerEncode::InspectFile(
    const std::filesystem::path& artifact_path,
    std::string* report_out,
    std::string* error_out) const {
    if (report_out == nullptr) {
        if (error_out != nullptr) {
            *error_out = "inspection report target is null";
        }
        return false;
    }

    std::string extension = artifact_path.extension().string();
    for (char& ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (extension == ".bak") {
        std::string manifest;
        if (!ReadTextFile(artifact_path, &manifest, error_out)) {
            return false;
        }
        *report_out = absl::StrFormat(
            "artifact=%s\nartifact_type=bak\nformat=sxer84321-manifest\n\n%s",
            artifact_path.string(),
            manifest);
        if (error_out != nullptr) {
            error_out->clear();
        }
        return true;
    }

    if (extension != ".bin") {
        if (error_out != nullptr) {
            *error_out = "inspect supports only .bin and .bak artifacts";
        }
        return false;
    }

    std::string buffer_error;
    const XerBuffer payload = XerBuffer::FromFile(artifact_path, &buffer_error);
    if (!buffer_error.empty()) {
        if (error_out != nullptr) {
            *error_out = buffer_error;
        }
        return false;
    }

    BinHeader header;
    std::vector<BinSection> sections;
    if (!ReadContainerHeaderAndSections(payload, &header, &sections, error_out)) {
        return false;
    }

    std::vector<std::uint8_t> manifest_bytes;
    const BinSection* manifest_section = FindSection(sections, "manifest");
    if (manifest_section != nullptr && !ReadSectionBytes(payload, *manifest_section, &manifest_bytes, error_out)) {
        return false;
    }
    const std::string manifest_text(
        reinterpret_cast<const char*>(manifest_bytes.data()),
        manifest_bytes.size());

    std::string sections_csv;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        if (index != 0u) {
            absl::StrAppend(&sections_csv, ",");
        }
        absl::StrAppend(&sections_csv, SectionName(sections[index]));
    }

    std::string report;
    absl::StrAppendFormat(&report, "artifact=%s\n", artifact_path.string());
    absl::StrAppend(&report, "artifact_type=bin\n");
    absl::StrAppend(&report, "format=sxer84321-container\n");
    absl::StrAppendFormat(&report, "container_version=0x%08X\n", header.version);
    absl::StrAppendFormat(&report, "encrypted=%s\n", (header.flags & kContainerFlagEncrypted) != 0u ? "true" : "false");
    absl::StrAppendFormat(&report, "nonce=%llu\n", static_cast<unsigned long long>(header.nonce));
    absl::StrAppendFormat(&report, "original_size=%llu\n", static_cast<unsigned long long>(header.original_size));
    absl::StrAppendFormat(&report, "payload_size=%llu\n", static_cast<unsigned long long>(header.payload_size));
    absl::StrAppendFormat(&report, "source_crc=%u\n", header.source_crc);
    absl::StrAppendFormat(&report, "payload_crc=%u\n", header.payload_crc);
    absl::StrAppendFormat(&report, "section_count=%u\n", header.section_count);
    absl::StrAppendFormat(&report, "sections=%s\n", sections_csv);

    if ((header.flags & kContainerFlagEncrypted) != 0u) {
        const BinSection* crypto_section = FindSection(sections, "crypto");
        if (crypto_section == nullptr) {
            if (error_out != nullptr) {
                *error_out = "crypto section is missing for encrypted container";
            }
            return false;
        }

        std::vector<std::uint8_t> crypto_bytes;
        if (!ReadSectionBytes(payload, *crypto_section, &crypto_bytes, error_out)) {
            return false;
        }
        if (crypto_bytes.size() != sizeof(CryptoSection)) {
            if (error_out != nullptr) {
                *error_out = "crypto section has invalid size";
            }
            return false;
        }

        CryptoSection crypto = {};
        std::memcpy(&crypto, crypto_bytes.data(), sizeof(crypto));
        if (crypto.magic != kCryptoSectionMagic || crypto.version != kCryptoSectionVersion) {
            if (error_out != nullptr) {
                *error_out = "unsupported crypto section metadata";
            }
            return false;
        }

        absl::StrAppendFormat(&report, "cipher=%s\n", crypto.cipher == kCipherAes256Gcm ? "aes-256-gcm" : "unknown");
        absl::StrAppend(&report, "kdf=pbkdf2-hmac-sha256\n");
        absl::StrAppendFormat(&report, "kdf_iterations=%u\n", crypto.kdf_iterations);
    }

    if (!manifest_text.empty()) {
        absl::StrAppend(&report, "\nmanifest:\n");
        absl::StrAppend(&report, manifest_text);
    }

    *report_out = std::move(report);
    if (error_out != nullptr) {
        error_out->clear();
    }
    return true;
}

bool XerEncode::WriteArtifacts(
    const std::filesystem::path& source_path,
    const XerEncodedBlock& block,
    const std::filesystem::path& output_dir,
    std::string* error_out) const {
    const std::filesystem::path destination_dir = output_dir.empty() ? source_path.parent_path() : output_dir;
    std::error_code fs_error;
    std::filesystem::create_directories(destination_dir, fs_error);
    if (fs_error) {
        if (error_out != nullptr) {
            *error_out = "failed to create output directory";
        }
        return false;
    }

    const std::string stem = source_path.stem().string();
    const std::filesystem::path bin_path = destination_dir / (stem + ".bin");
    const std::filesystem::path bak_path = destination_dir / (stem + ".bak");

    std::ofstream output(bin_path, std::ios::binary);
    if (!output.is_open()) {
        if (error_out != nullptr) {
            *error_out = "failed to open .bin output file";
        }
        return false;
    }

    output.write(
        reinterpret_cast<const char*>(block.bin_payload.bytes().data()),
        static_cast<std::streamsize>(block.bin_payload.size()));
    output.close();
    if (!output) {
        if (error_out != nullptr) {
            *error_out = "failed to write .bin output file";
        }
        return false;
    }

    std::string tool_error;
    if (!ToolTo::WriteTextFile(bak_path, block.bak_manifest, false, &tool_error)) {
        if (error_out != nullptr) {
            *error_out = tool_error.empty() ? "failed to write .bak output file" : tool_error;
        }
        return false;
    }

    if (error_out != nullptr) {
        error_out->clear();
    }
    return true;
}

}  // namespace Xer