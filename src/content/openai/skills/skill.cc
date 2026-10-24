#include "skill.h"
#include "../../../async_io/io_thread_pool.h"
#include <algorithm>
#include <chrono>

namespace Content::OpenAI::Skills {

BaseSkill::BaseSkill(const SkillDefinition& definition)
    : definition_(definition) {
}

std::string BaseSkill::GetName() const {
    return definition_.name;
}

std::string BaseSkill::GetDescription() const {
    return definition_.description;
}

SkillDefinition BaseSkill::GetDefinition() const {
    return definition_;
}

bool BaseSkill::ValidateParameters(
    const std::map<std::string, std::string>& parameters) const {

    for (const auto& param : definition_.parameters) {
        if (parameters.find(param) == parameters.end()) {
            return false;
        }
    }
    return true;
}

// WebSearchSkill implementation
WebSearchSkill::WebSearchSkill()
    : BaseSkill(SkillDefinition{
        .name = "web_search",
        .description = "Search the web for information",
        .category = "information",
        .parameters = {"query", "num_results"},
        .return_type = "search_results",
        .example_usage = "web_search(query=\"AI technology\", num_results=5)",
        .requires_auth = true,
        .requires_network = true
    }) {
}

std::future<SkillResult> WebSearchSkill::Execute(
    const std::map<std::string, std::string>& parameters) {

    std::promise<SkillResult> promise;
    auto future = promise.get_future();

    ExecuteAsync(
        parameters,
        [&promise](const SkillResult& result) {
            promise.set_value(result);
        }
    );

    return future;
}

void WebSearchSkill::ExecuteAsync(
    const std::map<std::string, std::string>& parameters,
    std::function<void(const SkillResult&)> on_complete) {

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, parameters, on_complete]() {
        auto start = std::chrono::system_clock::now();

        SkillResult result;
        result.success = ValidateParameters(parameters);

        if (result.success) {
            auto query_it = parameters.find("query");
            auto num_it = parameters.find("num_results");

            if (query_it != parameters.end()) {
                // TODO: Implement actual web search API call
                result.output = "Search results for: " + query_it->second + "\n";
                result.output += "[Simulated results from web search API]";
            } else {
                result.success = false;
                result.error_message = "Missing required parameter: query";
            }
        } else {
            result.error_message = "Invalid parameters provided";
        }

        auto end = std::chrono::system_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(
            end - start).count();

        if (on_complete) {
            on_complete(result);
        }
    });
}

// CodeExecutionSkill implementation
CodeExecutionSkill::CodeExecutionSkill()
    : BaseSkill(SkillDefinition{
        .name = "code_execution",
        .description = "Execute code in a sandboxed environment",
        .category = "computation",
        .parameters = {"code", "language"},
        .return_type = "execution_output",
        .example_usage = "code_execution(code=\"print('hello')\", language=\"python\")",
        .requires_auth = false,
        .requires_network = false
    }) {
}

std::future<SkillResult> CodeExecutionSkill::Execute(
    const std::map<std::string, std::string>& parameters) {

    std::promise<SkillResult> promise;
    auto future = promise.get_future();

    ExecuteAsync(
        parameters,
        [&promise](const SkillResult& result) {
            promise.set_value(result);
        }
    );

    return future;
}

void CodeExecutionSkill::ExecuteAsync(
    const std::map<std::string, std::string>& parameters,
    std::function<void(const SkillResult&)> on_complete) {

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, parameters, on_complete]() {
        auto start = std::chrono::system_clock::now();

        SkillResult result;
        result.success = ValidateParameters(parameters);

        if (result.success) {
            auto code_it = parameters.find("code");
            auto lang_it = parameters.find("language");

            if (code_it != parameters.end() && lang_it != parameters.end()) {
                // Use sandbox for execution
                auto sandbox = Sandbox::SandboxFactory::CreateSandbox(lang_it->second);

                if (sandbox) {
                    Sandbox::ExecutionContext context;
                    context.language = lang_it->second;
                    context.code = code_it->second;

                    auto exec_result = sandbox->ExecuteCode(context).get();

                    result.success = exec_result.success;
                    result.output = exec_result.stdout_output;
                    if (!exec_result.stderr_output.empty()) {
                        result.output += "\nStderr: " + exec_result.stderr_output;
                    }
                    result.execution_time_ms = exec_result.execution_time_ms;
                    result.metadata["exit_code"] = std::to_string(exec_result.exit_code);
                    result.metadata["memory_mb"] = std::to_string(exec_result.memory_used_mb);
                } else {
                    result.success = false;
                    result.error_message = "Unsupported language: " + lang_it->second;
                }
            } else {
                result.success = false;
                result.error_message = "Missing required parameters: code, language";
            }
        } else {
            result.error_message = "Invalid parameters provided";
        }

        auto end = std::chrono::system_clock::now();
        if (result.execution_time_ms == 0.0) {
            result.execution_time_ms = std::chrono::duration<double, std::milli>(
                end - start).count();
        }

        if (on_complete) {
            on_complete(result);
        }
    });
}

// FileIOSkill implementation
FileIOSkill::FileIOSkill()
    : BaseSkill(SkillDefinition{
        .name = "file_io",
        .description = "Read and write files with security controls",
        .category = "io",
        .parameters = {"operation", "path", "content"},
        .return_type = "file_content",
        .example_usage = "file_io(operation=\"read\", path=\"/path/to/file.txt\")",
        .requires_auth = false,
        .requires_network = false
    }) {
}

std::future<SkillResult> FileIOSkill::Execute(
    const std::map<std::string, std::string>& parameters) {

    std::promise<SkillResult> promise;
    auto future = promise.get_future();

    ExecuteAsync(
        parameters,
        [&promise](const SkillResult& result) {
            promise.set_value(result);
        }
    );

    return future;
}

void FileIOSkill::ExecuteAsync(
    const std::map<std::string, std::string>& parameters,
    std::function<void(const SkillResult&)> on_complete) {

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, parameters, on_complete]() {
        auto start = std::chrono::system_clock::now();

        SkillResult result;
        result.success = ValidateParameters(parameters);

        if (result.success) {
            auto op_it = parameters.find("operation");
            auto path_it = parameters.find("path");

            if (op_it != parameters.end() && path_it != parameters.end()) {
                // TODO: Implement actual file I/O with security checks
                result.output = "File I/O operation: " + op_it->second + " on " + path_it->second;
                result.metadata["operation"] = op_it->second;
                result.metadata["path"] = path_it->second;
            } else {
                result.success = false;
                result.error_message = "Missing required parameters: operation, path";
            }
        } else {
            result.error_message = "Invalid parameters provided";
        }

        auto end = std::chrono::system_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(
            end - start).count();

        if (on_complete) {
            on_complete(result);
        }
    });
}

// DataAnalysisSkill implementation
DataAnalysisSkill::DataAnalysisSkill()
    : BaseSkill(SkillDefinition{
        .name = "data_analysis",
        .description = "Analyze data and generate insights",
        .category = "analysis",
        .parameters = {"data", "analysis_type"},
        .return_type = "analysis_result",
        .example_usage = "data_analysis(data=\"[1,2,3,4,5]\", analysis_type=\"statistics\")",
        .requires_auth = false,
        .requires_network = false
    }) {
}

std::future<SkillResult> DataAnalysisSkill::Execute(
    const std::map<std::string, std::string>& parameters) {

    std::promise<SkillResult> promise;
    auto future = promise.get_future();

    ExecuteAsync(
        parameters,
        [&promise](const SkillResult& result) {
            promise.set_value(result);
        }
    );

    return future;
}

void DataAnalysisSkill::ExecuteAsync(
    const std::map<std::string, std::string>& parameters,
    std::function<void(const SkillResult&)> on_complete) {

    auto& thread_pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();

    thread_pool.Enqueue([this, parameters, on_complete]() {
        auto start = std::chrono::system_clock::now();

        SkillResult result;
        result.success = ValidateParameters(parameters);

        if (result.success) {
            auto data_it = parameters.find("data");
            auto type_it = parameters.find("analysis_type");

            if (data_it != parameters.end() && type_it != parameters.end()) {
                // TODO: Implement actual data analysis
                result.output = "Analysis type: " + type_it->second + "\n";
                result.output += "Data: " + data_it->second + "\n";
                result.output += "[Simulated analysis results]";
                result.metadata["analysis_type"] = type_it->second;
            } else {
                result.success = false;
                result.error_message = "Missing required parameters: data, analysis_type";
            }
        } else {
            result.error_message = "Invalid parameters provided";
        }

        auto end = std::chrono::system_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(
            end - start).count();

        if (on_complete) {
            on_complete(result);
        }
    });
}

// SkillRegistry implementation
SkillRegistry& SkillRegistry::GetInstance() {
    static SkillRegistry instance;
    return instance;
}

void SkillRegistry::RegisterSkill(std::shared_ptr<ISkill> skill) {
    std::lock_guard<std::mutex> lock(mutex_);
    skills_[skill->GetName()] = skill;
}

void SkillRegistry::UnregisterSkill(const std::string& skill_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    skills_.erase(skill_name);
}

std::shared_ptr<ISkill> SkillRegistry::GetSkill(const std::string& skill_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(skill_name);
    if (it != skills_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> SkillRegistry::ListSkills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& pair : skills_) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<SkillDefinition> SkillRegistry::ListSkillDefinitions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SkillDefinition> definitions;
    for (const auto& pair : skills_) {
        definitions.push_back(pair.second->GetDefinition());
    }
    return definitions;
}

void SkillRegistry::RegisterDefaultSkills() {
    RegisterSkill(std::make_shared<WebSearchSkill>());
    RegisterSkill(std::make_shared<CodeExecutionSkill>());
    RegisterSkill(std::make_shared<FileIOSkill>());
    RegisterSkill(std::make_shared<DataAnalysisSkill>());
}

void SkillRegistry::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    skills_.clear();
}

// SkillManager implementation
SkillManager::SkillManager(std::shared_ptr<Agent::IAgent> agent)
    : agent_(agent) {
}

void SkillManager::AttachSkill(const std::string& skill_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(attached_skills_.begin(), attached_skills_.end(), skill_name) == attached_skills_.end()) {
        attached_skills_.push_back(skill_name);

        // Register as tool with agent
        auto skill = SkillRegistry::GetInstance().GetSkill(skill_name);
        if (skill) {
            Tool tool;
            tool.type = "function";
            tool.function.name = skill->GetName();
            tool.function.description = skill->GetDescription();
            tool.function.parameters_json = "{}";  // Would be actual JSON schema
            agent_->RegisterTool(tool);
        }
    }
}

void SkillManager::DetachSkill(const std::string& skill_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    attached_skills_.erase(
        std::remove(attached_skills_.begin(), attached_skills_.end(), skill_name),
        attached_skills_.end()
    );

    agent_->UnregisterTool(skill_name);
}

std::vector<std::string> SkillManager::GetAttachedSkills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return attached_skills_;
}

std::future<SkillResult> SkillManager::ExecuteSkill(
    const std::string& skill_name,
    const std::map<std::string, std::string>& parameters) {

    auto skill = SkillRegistry::GetInstance().GetSkill(skill_name);
    if (skill) {
        return skill->Execute(parameters);
    }

    std::promise<SkillResult> promise;
    SkillResult result;
    result.success = false;
    result.error_message = "Skill not found: " + skill_name;
    promise.set_value(result);
    return promise.get_future();
}

}  // namespace Content::OpenAI::Skills
