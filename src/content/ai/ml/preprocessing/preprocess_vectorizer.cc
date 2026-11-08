#include "preprocess_vectorizer.h"

namespace XER::ML::Preprocessing {

void PreprocessVectorizer::Fit(
        const std::vector<std::vector<std::string>>& token_seqs) {
    vocab_.clear();
    for (const auto& seq : token_seqs)
        for (const auto& tok : seq)
            if (!vocab_.count(tok)) vocab_[tok] = vocab_.size();
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessVectorizer::Transform(
        const std::vector<std::vector<std::string>>& token_seqs) const {
    std::vector<std::vector<float>> out;
    out.reserve(token_seqs.size());
    for (const auto& seq : token_seqs) {
        std::vector<float> vec(vocab_.size(), 0.0f);
        for (const auto& tok : seq) {
            auto it = vocab_.find(tok);
            if (it != vocab_.end()) vec[it->second] += 1.0f;
        }
        out.push_back(std::move(vec));
    }
    return out;
}

}  // namespace XER::ML::Preprocessing
