#ifndef XER_UI_WIDGETS_OPENAI_SKILLS_WIDGET_H
#define XER_UI_WIDGETS_OPENAI_SKILLS_WIDGET_H

#include "../../widgets/widget.h"
#include "../../../content/openai/openai.h"
#include <string>
#include <vector>
#include <functional>

namespace XER::UI::Widgets::OpenAI {

/**
 * SkillDefinitionUI - UI representation of a skill
 */
struct SkillDefinitionUI {
    std::string name;
    std::string description;
    std::string category;
    std::vector<std::string> parameters;
    bool is_registered;
    bool is_active;
};

/**
 * SkillsWidget - UI widget for skill management
 *
 * Provides:
 * - Skill list with categories
 * - Skill registration/unregistration
 * - Skill execution UI
 * - Parameter input
 * - Result display
 * - Skill creation wizard
 */
class SkillsWidget : public Widget {
public:
    SkillsWidget();
    ~SkillsWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetOpenAISubsystem(std::shared_ptr<Content::OpenAI::OpenAISubsystem> subsystem);
    
    void RegisterSkill(const std::string& skill_name);
    void UnregisterSkill(const std::string& skill_name);
    void ExecuteSkill(const std::string& skill_name, const std::map<std::string, std::string>& params);
    
    void RefreshSkillList();
    void FilterByCategory(const std::string& category);
    
    // Callbacks
    using SkillExecutedCallback = std::function<void(const std::string& skill_name, const std::string& result)>;
    void OnSkillExecuted(SkillExecutedCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * SkillExecutionWidget - Widget for executing individual skills
 */
class SkillExecutionWidget : public Widget {
public:
    SkillExecutionWidget();
    ~SkillExecutionWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void SetSkill(const std::string& skill_name);
    void SetParameters(const std::map<std::string, std::string>& params);
    void Execute();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * SkillLibraryWidget - Widget for browsing available skills
 */
class SkillLibraryWidget : public Widget {
public:
    SkillLibraryWidget();
    ~SkillLibraryWidget() override = default;

    void Render(Rendering::Renderer& renderer) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void AddSkill(const SkillDefinitionUI& skill);
    void ClearSkills();
    std::string GetSelectedSkill() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Widgets::OpenAI

#endif // XER_UI_WIDGETS_OPENAI_SKILLS_WIDGET_H
