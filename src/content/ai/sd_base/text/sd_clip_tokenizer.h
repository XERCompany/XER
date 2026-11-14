#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::AI::SDBase {

// Minimal byte-level BPE tokenizer compatible with CLIP/GPT-2 style
// vocab.json + merges.txt shipped alongside diffusers checkpoints.
class SdClipTokenizer {
public:
    // Load vocab.json + merges.txt from a directory.
    bool Load(const std::string& dir, std::string* error);
    bool IsLoaded() const { return !token_to_id_.empty(); }

    // Encode text -> token ids, padded/truncated to max_len with eot_id.
    // Appends eot after the content tokens (CLIP convention).
    std::vector<int64_t> Encode(const std::string& text,
                                int max_len = 77) const;

    int64_t EotId() const { return eot_id_; }

private:
    std::unordered_map<std::string, int64_t> token_to_id_;
    std::vector<std::pair<std::string, std::string>> merges_;
    std::unordered_map<std::string, int> merge_rank_;
    int64_t eot_id_ = 49407;
};

}  // namespace XER::AI::SDBase
