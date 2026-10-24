#include "sandbox.h"
#include "../../../async_io/io_thread_pool.h"
#include <regex>
#include <algorithm>

namespace Content::OpenAI::Sandbox {

class PythonSandbox::Impl {
public:
    Impl(const ResourceLimits& limits)
        : limits_(limits),
          policy_(SandboxPolicy::kModerate),
          thread_pool_(IO::AsyncIO::IOThreadPool::GetSharedInstance()),
          is_running_(false),
          active_processes_(0),
          current_memory_mb_(0.0) {
    }

    ResourceLimits limits_;
    SandboxPolicy policy_;
    IO::AsyncIO::IOThreadPool& thread_pool_;
    std::atomic<bool> is_running_;
    std::atomic<int> active_processes_;
    std::atomic<double> current_memory_mb_;
};

PythonSandbox::PythonSandbox(const ResourceLimits& limits)
    : impl_(std::make_unique<Impl>(limits)) {
}

PythonSandbox::~PythonSandbox() = default;

std::future<ExecutionResult> PythonSandbox::ExecuteCode(
    const ExecutionContext& context) {

    std::promise<ExecutionResult> promise;
    auto future = promise.get_future();

    ExecuteCodeAsync(
        context,
        [&promise](const ExecutionResult& result) {
            promise.set_value(result);
        }
    );

    return future;
}

void PythonSandbox::ExecuteCodeAsync(
    const ExecutionContext& context,
    std::function<void(const ExecutionResult&)> on_complete) {

    impl_->thread_pool_.Enqueue([this, context, on_complete]() {
        auto start = std::chrono::system_clock::now();

        ExecutionResult result;
        result.success = true;
        result.exit_code = 0;
        result.start_time = start;

        // Security check
        if (!SecurityPolicy::ValidateCode(context.code, context.language)) {
            result.success = false;
            result.exit_code = 1;
            result.error_message = "Code validation failed: potentially dangerous patterns detected";
            result.end_time = std::chrono::system_clock::now();
            result.execution_time_ms = std::chrono::duration<double, std::milli>(
                result.end_time - result.start_time).count();
            if (on_complete) {
                on_complete(result);
            }
            return;
        }

        // TODO: Actual Python execution via subprocess or embedded interpreter
        // For now, simulate execution
        impl_->is_running_ = true;
        impl_->active_processes_++;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        result.stdout_output = "Execution simulated for Python code";
        result.stderr_output = "";
        result.memory_used_mb = 50;
        impl_->current_memory_mb_ = result.memory_used_mb;

        impl_->active_processes_--;
        impl_->is_running_ = false;

        result.end_time = std::chrono::system_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(
            result.end_time - result.start_time).count();

        if (on_complete) {
            on_complete(result);
        }
    });
}

void PythonSandbox::SetResourceLimits(const ResourceLimits& limits) {
    impl_->limits_ = limits;
}

ResourceLimits PythonSandbox::GetResourceLimits() const {
    return impl_->limits_;
}

void PythonSandbox::SetPolicy(SandboxPolicy policy) {
    impl_->policy_ = policy;
}

SandboxPolicy PythonSandbox::GetPolicy() const {
    return impl_->policy_;
}

void PythonSandbox::Reset() {
    impl_->active_processes_ = 0;
    impl_->current_memory_mb_ = 0.0;
}

void PythonSandbox::Cleanup() {
    Reset();
}

bool PythonSandbox::IsRunning() const {
    return impl_->is_running_;
}

int PythonSandbox::GetActiveProcesses() const {
    return impl_->active_processes_;
}

double PythonSandbox::GetCurrentMemoryUsage() const {
    return impl_->current_memory_mb_;
}

// JavaScriptSandbox implementation
class JavaScriptSandbox::Impl {
public:
    Impl(const ResourceLimits& limits)
        : limits_(limits),
          policy_(SandboxPolicy::kModerate),
          thread_pool_(IO::AsyncIO::IOThreadPool::GetSharedInstance()),
          is_running_(false),
          active_processes_(0),
          current_memory_mb_(0.0) {
    }

    ResourceLimits limits_;
    SandboxPolicy policy_;
    IO::AsyncIO::IOThreadPool& thread_pool_;
    std::atomic<bool> is_running_;
    std::atomic<int> active_processes_;
    std::atomic<double> current_memory_mb_;
};

JavaScriptSandbox::JavaScriptSandbox(const ResourceLimits& limits)
    : impl_(std::make_unique<Impl>(limits)) {
}

JavaScriptSandbox::~JavaScriptSandbox() = default;

std::future<ExecutionResult> JavaScriptSandbox::ExecuteCode(
    const ExecutionContext& context) {

    std::promise<ExecutionResult> promise;
    auto future = promise.get_future();

    ExecuteCodeAsync(
        context,
        [&promise](const ExecutionResult& result) {
            promise.set_value(result);
        }
    );

    return future;
}

void JavaScriptSandbox::ExecuteCodeAsync(
    const ExecutionContext& context,
    std::function<void(const ExecutionResult&)> on_complete) {

    impl_->thread_pool_.Enqueue([this, context, on_complete]() {
        auto start = std::chrono::system_clock::now();

        ExecutionResult result;
        result.success = true;
        result.exit_code = 0;
        result.start_time = start;

        // Security check
        if (!SecurityPolicy::ValidateCode(context.code, context.language)) {
            result.success = false;
            result.exit_code = 1;
            result.error_message = "Code validation failed: potentially dangerous patterns detected";
            result.end_time = std::chrono::system_clock::now();
            result.execution_time_ms = std::chrono::duration<double, std::milli>(
                result.end_time - result.start_time).count();
            if (on_complete) {
                on_complete(result);
            }
            return;
        }

        // TODO: Actual JavaScript execution via V8 or Node.js
        // For now, simulate execution
        impl_->is_running_ = true;
        impl_->active_processes_++;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        result.stdout_output = "Execution simulated for JavaScript code";
        result.stderr_output = "";
        result.memory_used_mb = 50;
        impl_->current_memory_mb_ = result.memory_used_mb;

        impl_->active_processes_--;
        impl_->is_running_ = false;

        result.end_time = std::chrono::system_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(
            result.end_time - result.start_time).count();

        if (on_complete) {
            on_complete(result);
        }
    });
}

void JavaScriptSandbox::SetResourceLimits(const ResourceLimits& limits) {
    impl_->limits_ = limits;
}

ResourceLimits JavaScriptSandbox::GetResourceLimits() const {
    return impl_->limits_;
}

void JavaScriptSandbox::SetPolicy(SandboxPolicy policy) {
    impl_->policy_ = policy;
}

SandboxPolicy JavaScriptSandbox::GetPolicy() const {
    return impl_->policy_;
}

void JavaScriptSandbox::Reset() {
    impl_->active_processes_ = 0;
    impl_->current_memory_mb_ = 0.0;
}

void JavaScriptSandbox::Cleanup() {
    Reset();
}

bool JavaScriptSandbox::IsRunning() const {
    return impl_->is_running_;
}

int JavaScriptSandbox::GetActiveProcesses() const {
    return impl_->active_processes_;
}

double JavaScriptSandbox::GetCurrentMemoryUsage() const {
    return impl_->current_memory_mb_;
}

// SandboxFactory implementation
std::unique_ptr<ISandbox> SandboxFactory::CreatePythonSandbox(
    const ResourceLimits& limits) {

    return std::make_unique<PythonSandbox>(limits);
}

std::unique_ptr<ISandbox> SandboxFactory::CreateJavaScriptSandbox(
    const ResourceLimits& limits) {

    return std::make_unique<JavaScriptSandbox>(limits);
}

std::unique_ptr<ISandbox> SandboxFactory::CreateSandbox(
    const std::string& language,
    const ResourceLimits& limits) {

    if (language == "python" || language == "py") {
        return CreatePythonSandbox(limits);
    } else if (language == "javascript" || language == "js") {
        return CreateJavaScriptSandbox(limits);
    }

    return nullptr;
}

// SecurityPolicy implementation
const std::vector<std::string> SecurityPolicy::kDangerousPatterns = {
    "__import__",
    "eval\\(",
    "exec\\(",
    "compile\\(",
    "open\\(",
    "subprocess",
    "os\\.system",
    "os\\.popen",
    "input\\(",
    "require\\(",
    "process\\.exec",
    "child_process",
    "fs\\.readFileSync",
    "fs\\.writeFileSync"
};

bool SecurityPolicy::ValidateCode(const std::string& code, const std::string& language) {
    // Convert to lowercase for case-insensitive matching
    std::string lower_code = code;
    std::transform(lower_code.begin(), lower_code.end(), lower_code.begin(), ::tolower);

    for (const auto& pattern : kDangerousPatterns) {
        try {
            std::regex re(pattern, std::regex_constants::icase);
            if (std::regex_search(lower_code, re)) {
                return false;
            }
        } catch (const std::regex_error&) {
            // If regex fails, do simple string search
            if (lower_code.find(pattern) != std::string::npos) {
                return false;
            }
        }
    }

    return true;
}

bool SecurityPolicy::SanitizeInput(const std::string& input) {
    // Check for shell injection patterns
    const std::vector<std::string> injection_patterns = {
        ";", "&&", "||", "|", "`", "$(", "\\$\\{"
    };

    for (const auto& pattern : injection_patterns) {
        if (input.find(pattern) != std::string::npos) {
            return false;
        }
    }

    return true;
}

std::string SecurityPolicy::EscapeOutput(const std::string& output) {
    std::string escaped = output;

    // Escape HTML entities
    std::map<std::string, std::string> html_entities = {
        {"&", "&amp;"},
        {"<", "&lt;"},
        {">", "&gt;"},
        {"\"", "&quot;"},
        {"'", "&#39;"}
    };

    for (const auto& entity : html_entities) {
        size_t pos = 0;
        while ((pos = escaped.find(entity.first, pos)) != std::string::npos) {
            escaped.replace(pos, entity.first.length(), entity.second);
            pos += entity.second.length();
        }
    }

    return escaped;
}

bool SecurityPolicy::CheckForMaliciousPatterns(const std::string& code) {
    return !ValidateCode(code, "");
}

}  // namespace Content::OpenAI::Sandbox
