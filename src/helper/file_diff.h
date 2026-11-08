#include <string>
#include <vector>

namespace XER::Helper {

enum class FileDiffOperationType {
    Added,
    Removed
};

struct FileDiffOperation {
    FileDiffOperationType type;
    std::string line;
};

struct FileDiffReturn
{
    std::string before;
    std::string after;
    bool equal = false;
    std::size_t added_count = 0;
    std::size_t removed_count = 0;
    std::vector<std::string> added_lines;
    std::vector<std::string> removed_lines;
    std::vector<FileDiffOperation> operations;
};


class FileDiff {
public:
    FileDiff(std::string before, std::string after);

    const FileDiffReturn& returnDiffFile() const;

private:
    FileDiffReturn diff_;
};

}

using FileDiffOperationType = XER::Helper::FileDiffOperationType;
using FileDiffOperation = XER::Helper::FileDiffOperation;
using FileDiffReturn = XER::Helper::FileDiffReturn;
using FileDiff = XER::Helper::FileDiff;