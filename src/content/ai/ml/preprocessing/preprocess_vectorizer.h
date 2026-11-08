#pragma once
#include <vector>
#include <string>
#include <map>

namespace XER::ML::Preprocessing {

/// Bag-of-words float vectorizer: Fit builds vocabulary, Transform counts
class PreprocessVectorizer {
public:
    void Fit(const std::vector<std::vector<std::string>>& token_seqs);
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<std::string>>& token_seqs) const;

    bool IsFitted() const { return fitted_; }
    size_t VocabSize() const { return vocab_.size(); }

private:
    std::map<std::string, size_t> vocab_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
