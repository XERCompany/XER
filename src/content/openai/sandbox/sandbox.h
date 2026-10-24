#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <chrono>

namespace Content::OpenAI::Sandbox {

// Sandbox policy levels
enum class SandboxPolicy {
    kStrict,      // No external access, minimal resources
    kModerate,    // Limited external access, resource limits
    kPermissive,  // Full access with monitoring
    kUnrestricted // No restrictions
};

// Resource limits
struct ResourceLimits {
    int max_memory_mb = 512;
    int max_cpu_time_seconds = 30;
    int max_execution_time_seconds = 60;
    int max_network_requests = 10;
    int max_file_descriptors = 100;
    int max_processes = 1;
    bool allow_network = false;
    bool allow_filesystem = false;
    bool allow_subprocess = false;
    bool allow_environment = false;
};

// Execution result
struct ExecutionResult {
    bool success;
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
    std::string error_message;
    double execution_time_ms;
    int memory_used_mb;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
};

// Code execution context
struct ExecutionContext {
    std::string language;  // python, javascript, etc.
    std::string code;
    std::map<std::string, std::string> environment;
    std::string working_directory;
    std::vector<std::string> input_files;
    std::vector<std::string> output_files;
};

// Sandbox interface
class ISandbox {
public:
    virtual ~ISandbox() = default;

    // Code execution
    virtual std::future<ExecutionResult> ExecuteCode(
        const ExecutionContext& context
    ) = 0;

    virtual void ExecuteCodeAsync(
        const ExecutionContext& context,
        std::function<void(const ExecutionResult&)> on_complete
    ) = 0;

    // Resource management
    virtual void SetResourceLimits(const ResourceLimits& limits) = 0;
    virtual ResourceLimits GetResourceLimits() const = 0;

    // Policy management
    virtual void SetPolicy(SandboxPolicy policy) = 0;
    virtual SandboxPolicy GetPolicy() const = 0;

    // State management
    virtual void Reset() = 0;
    virtual void Cleanup() = 0;

    // Monitoring
    virtual bool IsRunning() const = 0;
    virtual int GetActiveProcesses() const = 0;
    virtual double GetCurrentMemoryUsage() const = 0;
};

// Python sandbox implementation
class PythonSandbox : public ISandbox {
public:
    explicit PythonSandbox(const ResourceLimits& limits = ResourceLimits());
    ~PythonSandbox() override;

    std::future<ExecutionResult> ExecuteCode(
        const ExecutionContext& context
    ) override;

    void ExecuteCodeAsync(
        const ExecutionContext& context,
        std::function<void(const ExecutionResult&)> on_complete
    ) override;

    void SetResourceLimits(const ResourceLimits& limits) override;
    ResourceLimits GetResourceLimits() const override;

    void SetPolicy(SandboxPolicy policy) override;
    SandboxPolicy GetPolicy() const override;

    void Reset() override;
    void Cleanup() override;

    bool IsRunning() const override;
    int GetActiveProcesses() const override;
    double GetCurrentMemoryUsage() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// JavaScript sandbox implementation
class JavaScriptSandbox : public ISandbox {
public:
    explicit JavaScriptSandbox(const ResourceLimits& limits = ResourceLimits());
    ~JavaScriptSandbox() override;

    std::future<ExecutionResult> ExecuteCode(
        const ExecutionContext& context
    ) override;

    void ExecuteCodeAsync(
        const ExecutionContext& context,
        std::function<void(const ExecutionResult&)> on_complete
    ) override;

    void SetResourceLimits(const ResourceLimits& limits) override;
    ResourceLimits GetResourceLimits() const override;

    void SetPolicy(SandboxPolicy policy) override;
    SandboxPolicy GetPolicy() const override;

    void Reset() override;
    void Cleanup() override;

    bool IsRunning() const override;
    int GetActiveProcesses() const override;
    double GetCurrentMemoryUsage() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Sandbox factory
class SandboxFactory {
public:
    static std::unique_ptr<ISandbox> CreatePythonSandbox(
        const ResourceLimits& limits = ResourceLimits()
    );

    static std::unique_ptr<ISandbox> CreateJavaScriptSandbox(
        const ResourceLimits& limits = ResourceLimits()
    );

    static std::unique_ptr<ISandbox> CreateSandbox(
        const std::string& language,
        const ResourceLimits& limits = ResourceLimits()
    );
};

// Security policies
class SecurityPolicy {
public:
    static bool ValidateCode(const std::string& code, const std::string& language);
    static bool SanitizeInput(const std::string& input);
    static std::string EscapeOutput(const std::string& output);
    static bool CheckForMaliciousPatterns(const std::string& code);

private:
    static const std::vector<std::string> kDangerousPatterns;
};

}  // namespace Content::OpenAI::Sandbox
