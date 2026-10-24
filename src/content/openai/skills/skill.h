#pragma once

#include "../openai_types.h"
#include "../agent/agent.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Content::OpenAI::Skills {

// Skill execution result
struct SkillResult {
    bool success;
    std::string output;
    std::string error_message;
    std::map<std::string, std::string> metadata;
    double execution_time_ms;
};

// Skill definition
struct SkillDefinition {
    std::string name;
    std::string description;
    std::string category;
    std::vector<std::string> parameters;
    std::string return_type;
    std::string example_usage;
    bool requires_auth = false;
    bool requires_network = false;
};

// Skill interface
class ISkill {
public:
    virtual ~ISkill() = default;

    virtual std::string GetName() const = 0;
    virtual std::string GetDescription() const = 0;
    virtual SkillDefinition GetDefinition() const = 0;

    virtual std::future<SkillResult> Execute(
        const std::map<std::string, std::string>& parameters
    ) = 0;

    virtual void ExecuteAsync(
        const std::map<std::string, std::string>& parameters,
        std::function<void(const SkillResult&)> on_complete
    ) = 0;

    virtual bool ValidateParameters(
        const std::map<std::string, std::string>& parameters
    ) const = 0;
};

// Base skill implementation
class BaseSkill : public ISkill {
public:
    BaseSkill(const SkillDefinition& definition);
    ~BaseSkill() override = default;

    std::string GetName() const override;
    std::string GetDescription() const override;
    SkillDefinition GetDefinition() const override;

    bool ValidateParameters(
        const std::map<std::string, std::string>& parameters
    ) const override;

protected:
    SkillDefinition definition_;
};

// Web search skill
class WebSearchSkill : public BaseSkill {
public:
    WebSearchSkill();
    ~WebSearchSkill() override = default;

    std::future<SkillResult> Execute(
        const std::map<std::string, std::string>& parameters
    ) override;

    void ExecuteAsync(
        const std::map<std::string, std::string>& parameters,
        std::function<void(const SkillResult&)> on_complete
    ) override;
};

// Code execution skill
class CodeExecutionSkill : public BaseSkill {
public:
    CodeExecutionSkill();
    ~CodeExecutionSkill() override = default;

    std::future<SkillResult> Execute(
        const std::map<std::string, std::string>& parameters
    ) override;

    void ExecuteAsync(
        const std::map<std::string, std::string>& parameters,
        std::function<void(const SkillResult&)> on_complete
    ) override;
};

// File I/O skill
class FileIOSkill : public BaseSkill {
public:
    FileIOSkill();
    ~FileIOSkill() override = default;

    std::future<SkillResult> Execute(
        const std::map<std::string, std::string>& parameters
    ) override;

    void ExecuteAsync(
        const std::map<std::string, std::string>& parameters,
        std::function<void(const SkillResult&)> on_complete
    ) override;
};

// Data analysis skill
class DataAnalysisSkill : public BaseSkill {
public:
    DataAnalysisSkill();
    ~DataAnalysisSkill() override = default;

    std::future<SkillResult> Execute(
        const std::map<std::string, std::string>& parameters
    ) override;

    void ExecuteAsync(
        const std::map<std::string, std::string>& parameters,
        std::function<void(const SkillResult&)> on_complete
    ) override;
};

// Skill registry
class SkillRegistry {
public:
    static SkillRegistry& GetInstance();

    void RegisterSkill(std::shared_ptr<ISkill> skill);
    void UnregisterSkill(const std::string& skill_name);
    std::shared_ptr<ISkill> GetSkill(const std::string& skill_name);
    std::vector<std::string> ListSkills() const;
    std::vector<SkillDefinition> ListSkillDefinitions() const;

    // Bulk operations
    void RegisterDefaultSkills();
    void ClearAll();

private:
    SkillRegistry() = default;
    std::map<std::string, std::shared_ptr<ISkill>> skills_;
    mutable std::mutex mutex_;
};

// Skill manager for agents
class SkillManager {
public:
    explicit SkillManager(std::shared_ptr<Agent::IAgent> agent);
    ~SkillManager() = default;

    void AttachSkill(const std::string& skill_name);
    void DetachSkill(const std::string& skill_name);
    std::vector<std::string> GetAttachedSkills() const;

    std::future<SkillResult> ExecuteSkill(
        const std::string& skill_name,
        const std::map<std::string, std::string>& parameters
    );

private:
    std::shared_ptr<Agent::IAgent> agent_;
    std::vector<std::string> attached_skills_;
    mutable std::mutex mutex_;
};

}  // namespace Content::OpenAI::Skills
