#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace xer {
namespace async_io {

class AsyncTaskQueue {
public:
    void Post(std::function<void()> task) {
        tasks_.push_back(std::move(task));
    }

    void RunOne() {
        if (!tasks_.empty()) {
            tasks_.front()();
            tasks_.erase(tasks_.begin());
        }
    }

private:
    std::vector<std::function<void()>> tasks_;
};

class AsyncFileReader {
public:
    explicit AsyncFileReader(std::string path) : path_(std::move(path)) {}

    std::string ReadAll() const {
        std::ifstream input(path_, std::ios::binary);
        if (!input) {
            return {};
        }
        return std::string((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    }

private:
    std::string path_;
};

}  // namespace async_io
}  // namespace xer

int main() {
    const std::string path = "example_input.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "async I/O example\n";
    }

    xer::async_io::AsyncFileReader reader(path);
    auto data = reader.ReadAll();
    std::cout << "Loaded " << data.size() << " bytes\n";
    std::cout << data << "\n";

    xer::async_io::AsyncTaskQueue queue;
    queue.Post([] {
        std::cout << "Queued async task executed\n";
    });
    queue.RunOne();

    std::filesystem::remove(path);
    return 0;
}
