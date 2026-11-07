#include "condition_variable_wrapper.h"

namespace XER::AsyncIO::IO::Sync {

ConditionVariableWrapper::ConditionVariableWrapper(const std::string& name)
    : name_(name), notify_count_(0), wait_count_(0) {}

ConditionVariableWrapper::~ConditionVariableWrapper() {}

void ConditionVariableWrapper::Notify() {
    cv_.notify_one();
    notify_count_++;
}

void ConditionVariableWrapper::NotifyAll() {
    cv_.notify_all();
    notify_count_++;
}

void ConditionVariableWrapper::Wait(std::unique_lock<std::mutex>& lock) {
    cv_.wait(lock);
    wait_count_++;
}

bool ConditionVariableWrapper::WaitFor(std::unique_lock<std::mutex>& lock, const std::chrono::milliseconds& timeout) {
    bool result = cv_.wait_for(lock, timeout) == std::cv_status::no_timeout;
    wait_count_++;
    return result;
}

}  // namespace XER::AsyncIO::IO::Sync
