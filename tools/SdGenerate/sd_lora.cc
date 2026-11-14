#include "sd_lora.h"

#include <json/json.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace XER::Tools::SdGenerate {

namespace {

struct TensorInfo {
    std::string name;
    std::string dtype;                    // "F32", "F16", ...
    std::vector<int64_t> shape;
    uint64_t data_begin = 0;              // absolute offset in file
    uint64_t data_size = 0;
};

struct SafetensorsFile {
    std::vector<TensorInfo> tensors;      // in header order
    std::map<std::string, size_t> by_name;
    std::string blob;                     // raw tensor bytes (after header)
    std::string header_json;
};

uint64_t ReadU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

// Parse the safetensors container: [8B header_len][JSON header][tensor data].
bool LoadSafetensors(const std::string& path, SafetensorsFile* out,
                     std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *error = "cannot open " + path;
        return false;
    }
    uint8_t len_buf[8];
    f.read(reinterpret_cast<char*>(len_buf), 8);
    if (!f) {
        *error = "truncated safetensors header: " + path;
        return false;
    }
    const uint64_t header_len = ReadU64LE(len_buf);
    if (header_len == 0 || header_len > (64ULL << 20)) {
        *error = "implausible safetensors header size in " + path;
        return false;
    }
    out->header_json.resize(static_cast<size_t>(header_len));
    f.read(out->header_json.data(), static_cast<std::streamsize>(header_len));
    if (!f) {
        *error = "truncated safetensors JSON: " + path;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(out->header_json);
    if (!Json::parseFromStream(rb, ss, &root, &errs) || !root.isObject()) {
        *error = "bad safetensors JSON in " + path + ": " + errs;
        return false;
    }

    const uint64_t data_base = 8 + header_len;
    for (const auto& member : root.getMemberNames()) {
        if (member == "__metadata__") continue;
        const Json::Value& t = root[member];
        TensorInfo info;
        info.name = member;
        info.dtype = t.get("dtype", "").asString();
        for (const auto& d : t["shape"]) info.shape.push_back(d.asInt64());
        const Json::Value& offs = t["data_offsets"];
        if (!offs.isArray() || offs.size() != 2) {
            *error = "missing data_offsets for " + member;
            return false;
        }
        info.data_begin = data_base + offs[0].asUInt64();
        info.data_size =
            static_cast<uint64_t>(offs[1].asUInt64() - offs[0].asUInt64());
        info.data_begin -= data_base;  // store blob-relative below
        out->by_name[member] = out->tensors.size();
        out->tensors.push_back(std::move(info));
    }

    // Slurp the whole data section once (offsets are relative to it).
    const uint64_t file_size =
        static_cast<uint64_t>(std::filesystem::file_size(path));
    if (file_size <= data_base) {
        *error = "empty tensor data in " + path;
        return false;
    }
    f.seekg(static_cast<std::streamoff>(data_base));
    out->blob.resize(static_cast<size_t>(file_size - data_base));
    f.read(out->blob.data(), static_cast<std::streamsize>(out->blob.size()));
    if (!f) {
        *error = "failed reading tensor data: " + path;
        return false;
    }
    return true;
}

// LoRA naming conventions:
//   diffusers:  <module>.lora_A.weight / <module>.lora_B.weight
//   kohya/sd.cpp: <module>.lora_down.weight / <module>.lora_up.weight
// The A/Down matrix maps in->rank, B/Up maps rank->out.
std::string LoRAStem(const std::string& name, bool* is_a, bool* is_b) {
    static const char* kASuffixes[] = {"lora_A.weight", "lora_down.weight",
                                       "lora_a.weight", "lora_down"};
    static const char* kBSuffixes[] = {"lora_B.weight", "lora_up.weight",
                                       "lora_b.weight", "lora_up"};
    auto strip = [](std::string s) {
        while (!s.empty() && (s.back() == '.' || s.back() == '_')) {
            s.pop_back();
        }
        return s;
    };
    for (const char* s : kASuffixes) {
        const std::string suf = s;
        if (name.size() > suf.size() &&
            name.compare(name.size() - suf.size(), suf.size(), suf) == 0) {
            *is_a = true;
            *is_b = false;
            return strip(name.substr(0, name.size() - suf.size()));
        }
    }
    for (const char* s : kBSuffixes) {
        const std::string suf = s;
        if (name.size() > suf.size() &&
            name.compare(name.size() - suf.size(), suf.size(), suf) == 0) {
            *is_a = false;
            *is_b = true;
            return strip(name.substr(0, name.size() - suf.size()));
        }
    }
    return {};
}

size_t Numel(const std::vector<int64_t>& shape) {
    size_t n = 1;
    for (int64_t d : shape) n *= static_cast<size_t>(d);
    return n;
}

// F32 matmul C[m,n] = A[m,k] @ B[k,n], all row-major.
void MatMulF32(const float* A, const float* B, float* C, int64_t m, int64_t k,
               int64_t n) {
    for (int64_t i = 0; i < m; ++i) {
        for (int64_t j = 0; j < n; ++j) {
            float acc = 0.0f;
            for (int64_t p = 0; p < k; ++p) {
                acc += A[i * k + p] * B[p * n + j];
            }
            C[i * n + j] = acc;
        }
    }
}

}  // namespace

bool MergeLoraSafetensors(const std::string& base_path,
                          const std::string& lora_path, float scale,
                          const std::string& out_path,
                          uint32_t* merged_pairs,
                          uint32_t* skipped_tensors,
                          std::string* error) {
    SafetensorsFile base, lora;
    if (!LoadSafetensors(base_path, &base, error)) return false;
    if (!LoadSafetensors(lora_path, &lora, error)) return false;

    // Group LoRA tensors by stem: stem -> {a_tensor, b_tensor}.
    struct Pair { const TensorInfo* a = nullptr; const TensorInfo* b = nullptr; };
    std::map<std::string, Pair> pairs;
    std::set<std::string> consumed;
    for (const auto& t : lora.tensors) {
        bool is_a = false, is_b = false;
        const std::string stem = LoRAStem(t.name, &is_a, &is_b);
        if (stem.empty()) continue;
        if (t.dtype != "F32") {
            if (skipped_tensors) ++*skipped_tensors;
            continue;
        }
        auto& p = pairs[stem];
        if (is_a) p.a = &t; else p.b = &t;
    }

    // name -> replacement F32 bytes for merged tensors.
    std::map<std::string, std::vector<float>> merged_values;
    uint32_t pairs_done = 0;
    for (auto& [stem, p] : pairs) {
        if (!p.a || !p.b) continue;
        // Base weight name conventions: stem may carry a .weight suffix in
        // diffusers layouts; try a few candidates.
        const std::vector<std::string> candidates = {
            stem + ".weight", stem, stem + ".diff", stem + ".gamma"};
        const TensorInfo* base_t = nullptr;
        for (const auto& c : candidates) {
            const auto it = base.by_name.find(c);
            if (it != base.by_name.end()) {
                base_t = &base.tensors[it->second];
                break;
            }
        }
        if (!base_t) continue;
        if (base_t->dtype != "F32" || base_t->shape.size() != 2) {
            if (skipped_tensors) ++*skipped_tensors;
            continue;
        }
        // Shapes: A is [rank, in] (down), B is [out, rank] (up) for the
        // kohya layout; diffusers lora_A is [in, rank], lora_B [rank, out].
        // Detect orientation by matching dims against the base [out, in].
        const int64_t out_f = base_t->shape[0];
        const int64_t in_f = base_t->shape[1];
        const int64_t a0 = p.a->shape.size() == 2 ? p.a->shape[0] : 0;
        const int64_t a1 = p.a->shape.size() == 2 ? p.a->shape[1] : 0;
        const int64_t b0 = p.b->shape.size() == 2 ? p.b->shape[0] : 0;
        const int64_t b1 = p.b->shape.size() == 2 ? p.b->shape[1] : 0;

        const float* lora_a = reinterpret_cast<const float*>(
            lora.blob.data() + p.a->data_begin);
        const float* lora_b = reinterpret_cast<const float*>(
            lora.blob.data() + p.b->data_begin);
        (void)lora_a; (void)lora_b;

        // Try both orientations: pick the one consistent with base dims.
        // Orientation 1 (kohya):  A[rank,in] B[out,rank]
        // Orientation 2 (diffusers): A[in,rank] B[rank,out]
        bool o1 = (a1 == in_f && b0 == out_f && a0 == b1);
        bool o2 = (a0 == in_f && b1 == out_f && a1 == b0);
        if (!o1 && !o2) {
            if (skipped_tensors) ++*skipped_tensors;
            continue;
        }
        const int64_t rank = o1 ? a0 : a1;
        if (rank <= 0 || rank > 4096) {
            if (skipped_tensors) ++*skipped_tensors;
            continue;
        }

        // Build rank-space product P[out,in] = B_eff @ A_eff.
        std::vector<float> delta(static_cast<size_t>(out_f) * in_f, 0.0f);
        if (o1) {
            // A[rank,in] row-major, B[out,rank] row-major.
            // delta[o,i] = sum_r B[o,r]*A[r,i]
            for (int64_t o = 0; o < out_f; ++o) {
                for (int64_t r = 0; r < rank; ++r) {
                    const float bv = lora_b[o * rank + r] * scale;
                    if (bv == 0.0f) continue;
                    for (int64_t i = 0; i < in_f; ++i) {
                        delta[o * in_f + i] += bv * lora_a[r * in_f + i];
                    }
                }
            }
        } else {
            // A[in,rank], B[rank,out]: delta[o,i] = sum_r B[r,o]*A[i,r]
            for (int64_t i = 0; i < in_f; ++i) {
                for (int64_t r = 0; r < rank; ++r) {
                    const float av = lora_a[i * rank + r] * scale;
                    if (av == 0.0f) continue;
                    for (int64_t o = 0; o < out_f; ++o) {
                        delta[o * in_f + i] += lora_b[r * out_f + o] * av;
                    }
                }
            }
        }

        // W' = W + delta — copy base values then add.
        auto& slot = merged_values[base_t->name];
        slot.assign(reinterpret_cast<const float*>(
                        base.blob.data() + base_t->data_begin),
                    reinterpret_cast<const float*>(
                        base.blob.data() + base_t->data_begin) +
                        Numel(base_t->shape));
        for (size_t i = 0; i < slot.size(); ++i) slot[i] += delta[i];
        ++pairs_done;
    }

    if (pairs_done == 0) {
        *error = "no compatible LoRA pairs found in " + lora_path;
        return false;
    }

    // Write merged safetensors: new JSON header + concatenated tensor data.
    // Reuse the base header JSON but patch data_offsets and __metadata__.
    Json::Value header;
    {
        Json::CharReaderBuilder rb;
        std::string errs;
        std::istringstream hs(base.header_json);
        if (!Json::parseFromStream(rb, hs, &header, &errs)) {
            *error = "cannot re-parse base header: " + errs;
            return false;
        }
    }

    std::string data_blob;
    data_blob.reserve(base.blob.size() + merged_values.size() * 64);
    uint64_t cursor = 0;
    for (const auto& t : base.tensors) {
        const std::string rel = t.name;
        Json::Value entry;
        entry["dtype"] = t.dtype;
        Json::Value shape(Json::arrayValue);
        for (int64_t d : t.shape) shape.append(d);
        entry["shape"] = shape;

        const auto mit = merged_values.find(t.name);
        if (mit != merged_values.end()) {
            const size_t bytes = mit->second.size() * sizeof(float);
            entry["data_offsets"] = Json::Value(Json::arrayValue);
            entry["data_offsets"].append(cursor);
            entry["data_offsets"].append(cursor + bytes);
            data_blob.append(reinterpret_cast<const char*>(mit->second.data()),
                             bytes);
            cursor += bytes;
        } else {
            entry["data_offsets"] = Json::Value(Json::arrayValue);
            entry["data_offsets"].append(cursor);
            entry["data_offsets"].append(cursor + t.data_size);
            data_blob.append(base.blob.data() + t.data_begin, t.data_size);
            cursor += t.data_size;
        }
        header[rel] = entry;
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string header_str = Json::writeString(wb, header);

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        *error = "cannot write " + out_path;
        return false;
    }
    uint8_t len_buf[8];
    const uint64_t hlen = header_str.size();
    for (int i = 0; i < 8; ++i) {
        len_buf[i] = static_cast<uint8_t>((hlen >> (8 * i)) & 0xFF);
    }
    out.write(reinterpret_cast<const char*>(len_buf), 8);
    out.write(header_str.data(), static_cast<std::streamsize>(hlen));
    out.write(data_blob.data(), static_cast<std::streamsize>(data_blob.size()));
    if (!out) {
        *error = "write failed for " + out_path;
        return false;
    }

    if (merged_pairs) *merged_pairs = pairs_done;
    return true;
}

}  // namespace XER::Tools::SdGenerate
