#ifndef XER_UI_WIDGETS_SECURITY_FLASH_PANEL_WIDGET_H
#define XER_UI_WIDGETS_SECURITY_FLASH_PANEL_WIDGET_H

#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Button;
class Label;
class Select;
class Textarea;
class TextInput;

// Account + service management panel for XER SecurityFlash.
//
// Top section: SecurityFlashService state (not installed / stopped /
// running) with Install / Uninstall / Start / Stop / Refresh actions.
// Install/Uninstall are delegated to the sibling
// SecurityFlashService.exe, which performs its own UAC elevation;
// Start/Stop go through the SCM directly.
//
// Bottom section: local account management — the account list
// (NetUserEnum), per-drive SecurityFlash token status, and actions:
//   Create + Write Token — provision a Windows user and write the
//       AES-256-GCM credential token to the selected flash drive.
//   Change Password      — NetUserSetInfo + token rewrite.
//   Verify Password      — LogonUser credential check.
//   Write Token          — (re)write the token file on the drive.
//   Erase Token          — delete the token file.
//   Generate             — fill the password field via the
//       content/passwordgen generator.
//
// User editing: pick an account in the user selector to load it, then
//   Save Changes — rename / display name / description / password,
//   Enable|Disable — toggle UF_ACCOUNTDISABLE,
//   Delete User  — NetUserDel (second click confirms).
//
// Status is polled from Tick(); labels invalidate only on change.
class SecurityFlashPanelWidget final : public Widget {
public:
    SecurityFlashPanelWidget();
    ~SecurityFlashPanelWidget() override = default;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds,
                const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void Tick(double dt_seconds) override;

private:
    void EnsureChildrenAttached();
    void RefreshStatus();
    void RefreshAccountsText();
    void RefreshDriveOptions();
    void RefreshUserOptions(const std::vector<std::string>& users,
                            const std::vector<std::string>& labels);
    void LoadUserForEdit(const std::string& username);
    void ClearUserEdit();
    void SetActionResult(const std::string& text);

    char SelectedDriveLetter() const;
    bool SelectedDriveReady() const;

    void OnInstall();
    void OnUninstall();
    void OnStart();
    void OnStop();
    void OnRefresh();
    void OnGeneratePassword();
    void OnCreateAccount();
    void OnChangePassword();
    void OnVerifyPassword();
    void OnWriteToken();
    void OnEraseToken();
    void OnUserSelected(int index);
    void OnSaveUser();
    void OnToggleUser();
    void OnDeleteUser();

    std::shared_ptr<Label> service_label_;
    std::shared_ptr<Label> section_label_;
    std::shared_ptr<Label> action_label_;
    std::shared_ptr<Button> install_button_;
    std::shared_ptr<Button> uninstall_button_;
    std::shared_ptr<Button> start_button_;
    std::shared_ptr<Button> stop_button_;
    std::shared_ptr<Button> refresh_button_;
    std::shared_ptr<Textarea> accounts_area_;
    std::shared_ptr<TextInput> username_input_;
    std::shared_ptr<TextInput> password_input_;
    std::shared_ptr<TextInput> passphrase_input_;
    std::shared_ptr<Button> generate_button_;
    std::shared_ptr<Select> drive_select_;
    std::shared_ptr<Button> create_button_;
    std::shared_ptr<Button> change_button_;
    std::shared_ptr<Button> verify_button_;
    std::shared_ptr<Button> write_button_;
    std::shared_ptr<Button> erase_button_;
    std::shared_ptr<Select> user_select_;
    std::shared_ptr<TextInput> fullname_input_;
    std::shared_ptr<TextInput> comment_input_;
    std::shared_ptr<Button> save_user_button_;
    std::shared_ptr<Button> toggle_user_button_;
    std::shared_ptr<Button> delete_user_button_;

    bool children_attached_ = false;
    double poll_accumulator_ = 0.0;
    double accounts_accumulator_ = 0.0;
    std::string last_service_state_;
    std::string last_accounts_text_;
    std::string last_drive_options_key_;
    std::string last_user_options_key_;
    std::vector<std::string> user_names_;
    std::string editing_user_;
    bool editing_disabled_ = false;
    std::string pending_delete_user_;
    bool suppress_user_select_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SECURITY_FLASH_PANEL_WIDGET_H
