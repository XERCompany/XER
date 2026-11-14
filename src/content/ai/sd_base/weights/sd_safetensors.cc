#include "sd_safetensors.h"

#include <json/json.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#if __has_include(<fp16.h>)
#  include <fp16.h>
#  define SD_ST_HAS_FP16 1
#endif

namespace XER::AI::SDBase {

namespace {

uint64_t ReadU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

size_t DtypeSize(const std::string& dtype) {
    if (dtype == "F64" || dtype == "I64" || dtype == "U64") return 8;
    if (dtype == "F32" || dtype == "I32" || dtype == "U32") return 4;
    if (dtype == "F16" || dtype == "BF16" || dtype == "I16" || dtype == "U16") {
        return 2;
    }
    if (dtype == "I8" || dtype == "U8" || dtype == "BOOL") return 1;
    return 0;
}

}  // namespace

bool SdWeightSet::Load(const std::string& path, std::string* error) {
    tensors_.clear();
    skipped_ = 0;

    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }

    uint8_t len_buf[8];
    f.read(reinterpret_cast<char*>(len_buf), 8);
    if (!f) {
        if (error) *error = "truncated safetensors header: " + path;
        return false;
    }
    const uint64_t header_len = ReadU64LE(len_buf);
    if (header_len == 0 || header_len > (256ULL << 20)) {
        if (error) *error = "implausible safetensors header size in " + path;
        return false;
    }

    std::string header_json(static_cast<size_t>(header_len), '\0');
    f.read(header_json.data(), static_cast<std::streamsize>(header_len));
    if (!f) {
        if (error) *error = "truncated safetensors JSON: " + path;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream hs(header_json);
    if (!Json::parseFromStream(rb, hs, &root, &errs) || !root.isObject()) {
        if (error) *error = "bad safetensors JSON in " + path + ": " + errs;
        return false;
    }

    const uint64_t file_size =
        static_cast<uint64_t>(std::filesystem::file_size(path));
    const uint64_t data_base = 8 + header_len;
    if (file_size < data_base) {
        if (error) *error = "safetensors file too small: " + path;
        return false;
    }
    const uint64_t blob_size = file_size - data_base;
    std::string blob(static_cast<size_t>(blob_size), '\0');
    f.seekg(static_cast<std::streamoff>(data_base));
    f.read(blob.data(), static_cast<std::streamsize>(blob_size));
    if (!f && blob_size > 0) {
        if (error) *error = "failed reading tensor data: " + path;
        return false;
    }

    for (const auto& member : root.getMemberNames()) {
        if (member == "__metadata__") continue;
        const Json::Value& t = root[member];
        const std::string dtype = t.get("dtype", "").asString();
        const Json::Value& offs = t["data_offsets"];
        if (!offs.isArray() || offs.size() != 2) {
            if (error) *error = "missing data_offsets for " + member;
            return false;
        }
        const uint64_t begin = offs[0].asUInt64();
        const uint64_t end = offs[1].asUInt64();
        if (end < begin || end > blob_size) {
            if (error) *error = "bad data_offsets for " + member;
            return false;
        }

        SdWeightTensor wt;
        wt.name = member;
        wt.dtype = dtype;
        for (const auto& d : t["shape"]) {
            wt.shape.push_back(d.asInt64());
        }
        size_t numel = 1;
        for (const auto d : wt.shape) numel *= static_cast<size_t>(d);
        const size_t esize = DtypeSize(dtype);
        if (esize == 0 || static_cast<uint64_t>(numel) * esize != end - begin) {
            if (error) {
                *error = "dtype/shape mismatch for " + member + " (" + dtype +
                         ")";
            }
            return false;
        }

        const uint8_t* src =
            reinterpret_cast<const uint8_t*>(blob.data()) + begin;
        wt.data.resize(numel);
        if (dtype == "F32") {
            std::memcpy(wt.data.data(), src, numel * sizeof(float));
        } else if (dtype == "F16") {
#if SD_ST_HAS_FP16
            const uint16_t* h = reinterpret_cast<const uint16_t*>(src);
            for (size_t i = 0; i < numel; ++i) {
                wt.data[i] = fp16_ieee_to_fp32_value(h[i]);
            }
#else
            ++skipped_;
            continue;
#endif
        } else {
            ++skipped_;
            continue;
        }
        tensors_.emplace(member, std::move(wt));
    }
    return true;
}

const SdWeightTensor* SdWeightSet::Find(const std::string& name) const {
    const auto it = tensors_.find(name);
    return it == tensors_.end() ? nullptr : &it->second;
}

const SdWeightTensor* SdWeightSet::FindModel(const std::string& name) const {
    static const char* kPrefixes[] = {
        "", "unet.", "model.diffusion_model.", "diffusion_model.",
        "model.unet.", "unet_model.", "model."};
    for (const char* p : kPrefixes) {
        const auto it = tensors_.find(std::string(p) + name);
        if (it != tensors_.end()) return &it->second;
    }
    return nullptr;
}

}  // namespace XER::AI::SDBase
