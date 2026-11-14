#include "sd_clip_tokenizer.h"

#include <json/json.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace XER::AI::SDBase {

namespace {

// GPT-2/CLIP byte->unicode mapping.
std::string BytesToUnicodeToken(const std::string& bytes) {
    // Each byte maps to a unicode char; build via codepoint table.
    std::string out;
    out.reserve(bytes.size());
    for (unsigned char b : bytes) {
        if (b >= '!' && b <= '~') {
            out.push_back(static_cast<char>(b));
        } else if (b >= 0xA1 && b <= 0xAC) {
            out.push_back(static_cast<char>(b));
        } else if (b >= 0xAE && b <= 0xFF) {
            out.push_back(static_cast<char>(b));
        } else {
            // map to U+0100 + n as UTF-8
            const uint32_t cp = 0x100 + b;
            if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
    }
    return out;
}

std::vector<std::string> SplitWords(const std::string& text) {
    // CLIP regex approximation: words = \w+|[^\w\s]+|\s+  (simplified:
    // maximal runs of word chars, non-word non-space, or whitespace).
    std::vector<std::string> out;
    size_t i = 0;
    const size_t n = text.size();
    auto is_word = [](unsigned char c) {
        return std::isalnum(c) || c == '_' ||
               static_cast<unsigned char>(c) >= 0x80;
    };
    while (i < n) {
        const bool cls_word = is_word(text[i]);
        const bool cls_space = std::isspace(static_cast<unsigned char>(text[i]));
        size_t j = i;
        if (cls_space) {
            while (j < n && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        } else if (cls_word) {
            while (j < n && is_word(text[j])) ++j;
        } else {
            while (j < n && !is_word(text[j]) &&
                   !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        }
        out.push_back(text.substr(i, j - i));
        i = j;
    }
    return out;
}

// Byte-level BPE on one pretoken.
std::vector<std::string> BpeTokenize(const std::string& word,
                                     const std::vector<std::pair<std::string, std::string>>& merges,
                                     const std::unordered_map<std::string, int>& rank) {
    std::string uni = BytesToUnicodeToken(word);
    // start as sequence of single unicode chars
    std::vector<std::string> seq;
    // iterate UTF-8 codepoints
    for (size_t i = 0; i < uni.size();) {
        unsigned char c = static_cast<unsigned char>(uni[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        seq.push_back(uni.substr(i, std::min(len, uni.size() - i)));
        i += len;
    }
    if (seq.empty()) return seq;

    while (seq.size() > 1) {
        // find lowest-rank adjacent pair
        int best_rank = 0x7FFFFFFF;
        size_t best_i = seq.size();
        for (size_t i = 0; i + 1 < seq.size(); ++i) {
            const std::string pair = seq[i] + " " + seq[i + 1];
            const auto it = rank.find(pair);
            if (it != rank.end() && it->second < best_rank) {
                best_rank = it->second;
                best_i = i;
            }
        }
        if (best_i == seq.size()) break;
        // apply merge: find merged token string from merges table
        const std::string pair = seq[best_i] + " " + seq[best_i + 1];
        for (const auto& m : merges) {
            if (m.first + " " + m.second == pair) {
                // merged token = concatenation of unicode forms
                // (merges store raw byte-pair strings already in unicode space)
                seq[best_i] = m.first + m.second;
                break;
            }
        }
        seq.erase(seq.begin() + best_i + 1);
    }
    return seq;
}

}  // namespace

bool SdClipTokenizer::Load(const std::string& dir, std::string* error) {
    token_to_id_.clear();
    merges_.clear();
    merge_rank_.clear();

    const auto vocab_path = std::filesystem::path(dir) / "vocab.json";
    const auto merges_path = std::filesystem::path(dir) / "merges.txt";

    {
        std::ifstream vf(vocab_path);
        if (!vf) {
            if (error) *error = "cannot open " + vocab_path.string();
            return false;
        }
        Json::Value root;
        Json::CharReaderBuilder rb;
        std::string errs;
        if (!Json::parseFromStream(rb, vf, &root, &errs) || !root.isObject()) {
            if (error) *error = "bad vocab.json: " + errs;
            return false;
        }
        for (const auto& name : root.getMemberNames()) {
            token_to_id_[name] = root[name].asInt64();
        }
    }

    {
        std::ifstream mf(merges_path);
        if (!mf) {
            if (error) *error = "cannot open " + merges_path.string();
            return false;
        }
        std::string line;
        int rank = 0;
        std::getline(mf, line);  // version header
        while (std::getline(mf, line)) {
            if (line.empty()) continue;
            const size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            // merges.txt is space-separated pairs; parts may contain spaces
            // in unicode-escaped form — take first token and rest trimmed.
            std::string a = line.substr(0, sp);
            std::string b = line.substr(sp + 1);
            const size_t lt = b.find_last_not_of(" \t\r");
            if (lt != std::string::npos) b = b.substr(0, lt + 1);
            const size_t bt = b.find_first_not_of(" \t");
            if (bt != std::string::npos) b = b.substr(bt);
            merges_.push_back({a, b});
            merge_rank_[a + " " + b] = rank++;
        }
    }

    // EOT token: CLIP vocab uses "</w>" (id 49407); GPT-2 style uses
    // "<|endoftext|>". Fall back to the largest id in the vocab.
    for (const char* name : {"</w>", "<|endoftext|>"}) {
        const auto it = token_to_id_.find(name);
        if (it != token_to_id_.end()) {
            eot_id_ = it->second;
            return true;
        }
    }
    int64_t best = -1;
    for (const auto& kv : token_to_id_) best = std::max(best, kv.second);
    if (best >= 0) eot_id_ = best;
    return true;
}

std::vector<int64_t> SdClipTokenizer::Encode(const std::string& text,
                                              int max_len) const {
    std::vector<int64_t> ids;
    ids.reserve(max_len);
    for (const auto& word : SplitWords(text)) {
        for (const auto& piece : BpeTokenize(word, merges_, merge_rank_)) {
            const auto it = token_to_id_.find(piece);
            if (it != token_to_id_.end()) ids.push_back(it->second);
        }
        if (static_cast<int>(ids.size()) >= max_len - 1) break;
    }
    ids.push_back(eot_id_);
    if (static_cast<int>(ids.size()) > max_len) ids.resize(max_len);
    while (static_cast<int>(ids.size()) < max_len) ids.push_back(eot_id_);
    return ids;
}

}  // namespace XER::AI::SDBase
