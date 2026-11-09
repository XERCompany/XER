#include "security_flash_panel_widget.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "content/passwordgen/password_generator.h"
#include "rendering/renderer.h"
#include "security_flash/security_flash.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/textarea.h"
#include "widgets/text_input.h"

namespace XER::UI::Widgets {
namespace {

// XER::SecurityFlash is both a namespace and the class inside it.
using SFS = XER::SecurityFlash::SecurityFlash;
namespace SF = XER::SecurityFlash;
using PasswordGen = XER::Content::PasswordGen::PasswordGenerator;
using PasswordPolicy = XER::Content::PasswordGen::Policy;

constexpr double kPollIntervalSeconds = 0.75;
constexpr double kAccountsIntervalSeconds = 4.0;

#ifdef _WIN32
constexpr wchar_t kServiceName[] = L"XERSecurityFlash";
constexpr wchar_t kServiceExeName[] = L"SecurityFlashService.exe";

struct ScmHandle {
	explicit ScmHandle(SC_HANDLE h) : value(h) {}
	SC_HANDLE value = nullptr;
	~ScmHandle() {
		if (value) CloseServiceHandle(value);
	}
	ScmHandle(const ScmHandle&) = delete;
	ScmHandle& operator=(const ScmHandle&) = delete;
};

// Returns the SCM state code, or 0 when the service is not installed.
DWORD QueryServiceState(std::string* detail) {
	ScmHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
	if (!scm.value) {
		if (detail) *detail = "SCM unavailable";
		return 0;
	}
	ScmHandle svc{OpenServiceW(scm.value, kServiceName,
	                           SERVICE_QUERY_STATUS)};
	if (!svc.value) {
		const DWORD err = GetLastError();
		if (detail) {
			*detail = err == ERROR_SERVICE_DOES_NOT_EXIST
			              ? "not installed"
			              : "query failed (error " +
			                    std::to_string(err) + ")";
		}
		return 0;
	}
	SERVICE_STATUS_PROCESS ssp{};
	DWORD needed = 0;
	if (!QueryServiceStatusEx(svc.value, SC_STATUS_PROCESS_INFO,
	                          reinterpret_cast<BYTE*>(&ssp), sizeof(ssp),
	                          &needed)) {
		if (detail) *detail = "status query failed";
		return 0;
	}
	return ssp.dwCurrentState;
}

const char* StateName(DWORD state) {
	switch (state) {
	case SERVICE_RUNNING: return "running";
	case SERVICE_STOPPED: return "stopped";
	case SERVICE_START_PENDING: return "starting";
	case SERVICE_STOP_PENDING: return "stopping";
	case SERVICE_PAUSED: return "paused";
	case SERVICE_CONTINUE_PENDING: return "resuming";
	default: return "unknown";
	}
}

bool WaitForState(SC_HANDLE svc, DWORD wanted, DWORD timeout_ms) {
	// GetTickCount64: 32-bit GetTickCount wraps after ~49.7 days uptime and
	// the deadline comparison below would misfire across the wrap.
	const ULONGLONG deadline = GetTickCount64() + timeout_ms;
	for (;;) {
		SERVICE_STATUS_PROCESS ssp{};
		DWORD needed = 0;
		if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
		                          reinterpret_cast<BYTE*>(&ssp),
		                          sizeof(ssp), &needed)) {
			return false;
		}
		if (ssp.dwCurrentState == wanted) return true;
		if (GetTickCount64() >= deadline) return false;
		Sleep(100);
	}
}

// Issues a start/stop control and waits for the resulting state.
bool ServiceControl(bool start, std::string* error) {
	const DWORD wanted = start ? SERVICE_RUNNING : SERVICE_STOPPED;
	ScmHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
	if (!scm.value) {
		if (error) *error = "OpenSCManager failed";
		return false;
	}
	ScmHandle svc{OpenServiceW(scm.value, kServiceName,
	                           SERVICE_START | SERVICE_STOP |
	                               SERVICE_QUERY_STATUS)};
	if (!svc.value) {
		const DWORD err = GetLastError();
		if (error) {
			*error = err == ERROR_SERVICE_DOES_NOT_EXIST
			             ? "service is not installed"
			             : "OpenService failed (error " +
			                   std::to_string(err) + ")";
		}
		return false;
	}

	const BOOL ok = start
	                    ? StartServiceW(svc.value, 0, nullptr)
	                    : ControlService(svc.value, SERVICE_CONTROL_STOP,
	                                     nullptr);
	if (!ok) {
		const DWORD err = GetLastError();
		if ((start && err == ERROR_SERVICE_ALREADY_RUNNING) ||
		    (!start && err == ERROR_SERVICE_NOT_ACTIVE)) {
			return true;
		}
		if (error) {
			*error = std::string(start ? "StartService"
			                           : "ControlService") +
			         " failed (error " + std::to_string(err) + ")";
		}
		return false;
	}
	if (!WaitForState(svc.value, wanted, 8000)) {
		if (error) *error = "service did not reach state in time";
		return false;
	}
	return true;
}

// Path of the sibling service executable (same bin directory).
std::wstring SiblingServiceExe() {
	wchar_t buf[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	if (!n) return {};
	wchar_t* slash = wcsrchr(buf, L'\\');
	if (!slash) return {};
	*(slash + 1) = L'\0';
	return std::wstring(buf) + kServiceExeName;
}

// Fire-and-forget elevated launch of SecurityFlashService.exe with `arg`.
bool LaunchServiceTool(const wchar_t* arg, std::string* error) {
	const std::wstring exe = SiblingServiceExe();
	if (exe.empty() ||
	    GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
		if (error) {
			*error = "SecurityFlashService.exe not found next to "
			         "this executable";
		}
		return false;
	}
	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_FLAG_NO_UI;
	sei.lpVerb = L"runas";
	sei.lpFile = exe.c_str();
	sei.lpParameters = arg;
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei)) {
		const DWORD err = GetLastError();
		if (error) {
			*error = err == ERROR_CANCELLED
			             ? "elevation cancelled"
			             : "launch failed (error " +
			                   std::to_string(err) + ")";
		}
		return false;
	}
	if (sei.hProcess) CloseHandle(sei.hProcess);
	return true;
}

#endif  // _WIN32

// ---- shared helpers -----------------------------------------------------

// True when `username` is the account this process runs under.
bool IsSignedInUser(const std::string& username) {
#ifdef _WIN32
	wchar_t current[257];
	DWORD len = 257;
	if (!GetUserNameW(current, &len)) return false;
	const int n = MultiByteToWideChar(CP_UTF8, 0, username.c_str(), -1,
	                                  nullptr, 0);
	if (n <= 0) return false;
	std::wstring wide(static_cast<std::size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, username.c_str(), -1, wide.data(), n);
	return _wcsicmp(wide.c_str(), current) == 0;
#else
	(void)username;
	return false;
#endif
}

std::string DriveStatusLine(const XER::SecurityFlash::DriveInfo& d) {
	char letter = d.letter ? d.letter : '?';
	const bool token = d.ready && SFS::TokenExists(d.root_path);
	char line[512];
	std::snprintf(line, sizeof(line),
	              "%c:\\  %-22s serial=0x%08X  token: %s",
	              letter, d.volume_label.c_str(), d.volume_serial,
	              !d.ready ? "no media" : (token ? "ARMED" : "none"));
	return line;
}

}  // namespace

SecurityFlashPanelWidget::SecurityFlashPanelWidget() {
	service_label_ = std::make_shared<Label>("SecurityFlashService: …",
	                                         /*muted=*/false);
	section_label_ =
	    std::make_shared<Label>("Accounts & removable drives",
	                            /*muted=*/true);
	action_label_ = std::make_shared<Label>("", /*muted=*/true);

	install_button_ = std::make_shared<Button>("Install",
	                                           ButtonStyle::Primary);
	uninstall_button_ =
	    std::make_shared<Button>("Uninstall", ButtonStyle::Destructive);
	start_button_ =
	    std::make_shared<Button>("Start", ButtonStyle::Secondary);
	stop_button_ =
	    std::make_shared<Button>("Stop", ButtonStyle::Outline);
	refresh_button_ =
	    std::make_shared<Button>("Refresh", ButtonStyle::Ghost);

	accounts_area_ = std::make_shared<Textarea>();
	accounts_area_->SetReadOnly(true);

	username_input_ = std::make_shared<TextInput>();
	username_input_->SetPlaceholder("Windows username, e.g. alice");
	password_input_ = std::make_shared<TextInput>();
	password_input_->SetPlaceholder("Windows password (or Generate)");
	password_input_->SetPasswordMode(true);
	passphrase_input_ = std::make_shared<TextInput>();
	passphrase_input_->SetPlaceholder("Flash passphrase (encrypts token)");
	passphrase_input_->SetPasswordMode(true);

	constexpr const char* kEditKeys =
	    "\nCtrl+A select all, Ctrl+C / Ctrl+X / Ctrl+V copy / cut / paste,"
	    " Ctrl+Z undo, Ctrl+Y redo";
	username_input_->SetTooltip(
	    std::string("Local Windows account name. Shown on the Windows "
	                "sign-in screen after creation.") + kEditKeys);
	password_input_->SetTooltip(
	    std::string("Password for the Windows account. Copy/cut are "
	                "disabled for hidden fields.") + kEditKeys);
	passphrase_input_->SetTooltip(
	    std::string("Passphrase that encrypts the token on the flash "
	                "drive (AES-256-GCM). Not the Windows password.") +
	    kEditKeys);

	generate_button_ =
	    std::make_shared<Button>("Generate", ButtonStyle::Ghost);
	drive_select_ = std::make_shared<Select>();

	user_select_ = std::make_shared<Select>();
	user_select_->SetPlaceholder("Select a user to edit...");
	user_select_->SetTooltip(
	    "Pick an existing local account to load it into the fields for "
	    "editing");
	user_select_->SetOnChanged([this](int index) { OnUserSelected(index); });
	fullname_input_ = std::make_shared<TextInput>();
	fullname_input_->SetPlaceholder("Display name (sign-in screen)");
	fullname_input_->SetTooltip(
	    std::string("Full name shown on the Windows sign-in screen.") +
	    kEditKeys);
	comment_input_ = std::make_shared<TextInput>();
	comment_input_->SetPlaceholder("Description (optional)");
	comment_input_->SetTooltip(
	    std::string("Account description (lusrmgr 'Description').") +
	    kEditKeys);
	save_user_button_ =
	    std::make_shared<Button>("Save Changes", ButtonStyle::Primary);
	toggle_user_button_ =
	    std::make_shared<Button>("Disable", ButtonStyle::Secondary);
	delete_user_button_ =
	    std::make_shared<Button>("Delete User", ButtonStyle::Destructive);
	save_user_button_->SetOnClick([this] { OnSaveUser(); });
	toggle_user_button_->SetOnClick([this] { OnToggleUser(); });
	delete_user_button_->SetOnClick([this] { OnDeleteUser(); });
	save_user_button_->SetTooltip(
	    "Apply username (rename), display name and description. A "
	    "non-empty password field also resets the Windows password "
	    "(token is not rewritten; use Change Password for that).");
	toggle_user_button_->SetTooltip(
	    "Enable or disable the selected account");
	delete_user_button_->SetTooltip(
	    "Delete the selected Windows account (click twice to confirm). "
	    "The profile folder in C:\\Users is kept.");

	create_button_ = std::make_shared<Button>("Create + Write Token",
	                                          ButtonStyle::Primary);
	change_button_ =
	    std::make_shared<Button>("Change Password", ButtonStyle::Secondary);
	verify_button_ =
	    std::make_shared<Button>("Verify Password", ButtonStyle::Secondary);
	write_button_ =
	    std::make_shared<Button>("Write Token", ButtonStyle::Secondary);
	erase_button_ =
	    std::make_shared<Button>("Erase Token", ButtonStyle::Destructive);

	install_button_->SetOnClick([this] { OnInstall(); });
	uninstall_button_->SetOnClick([this] { OnUninstall(); });
	start_button_->SetOnClick([this] { OnStart(); });
	stop_button_->SetOnClick([this] { OnStop(); });
	refresh_button_->SetOnClick([this] { OnRefresh(); });
	generate_button_->SetOnClick([this] { OnGeneratePassword(); });
	create_button_->SetOnClick([this] { OnCreateAccount(); });
	change_button_->SetOnClick([this] { OnChangePassword(); });
	verify_button_->SetOnClick([this] { OnVerifyPassword(); });
	write_button_->SetOnClick([this] { OnWriteToken(); });
	erase_button_->SetOnClick([this] { OnEraseToken(); });

	install_button_->SetTooltip("Register XERSecurityFlash (elevated)");
	uninstall_button_->SetTooltip("Remove XERSecurityFlash (elevated)");
	start_button_->SetTooltip("Start the service via SCM");
	stop_button_->SetTooltip("Stop the service via SCM");
	generate_button_->SetTooltip(
	    "Fill the password field with a generated password");
	create_button_->SetTooltip(
	    "Create the Windows user and write the encrypted token "
	    "to the selected drive");
	change_button_->SetTooltip(
	    "Change the Windows password and rewrite the token");
	verify_button_->SetTooltip(
	    "Check username+password via LogonUser (no session)");
	write_button_->SetTooltip(
	    "Write xer_flash.tok with the entered credentials");
	erase_button_->SetTooltip("Delete xer_flash.tok from the drive");
	drive_select_->SetTooltip(
	    "Removable drive that receives the token. Removing an armed "
	    "drive locks Windows (password required to unlock).");
}

void SecurityFlashPanelWidget::EnsureChildrenAttached() {
	if (children_attached_) return;
	children_attached_ = true;
	AddChild(service_label_);
	AddChild(install_button_);
	AddChild(uninstall_button_);
	AddChild(start_button_);
	AddChild(stop_button_);
	AddChild(refresh_button_);
	AddChild(section_label_);
	AddChild(accounts_area_);
	AddChild(fullname_input_);
	AddChild(comment_input_);
	AddChild(save_user_button_);
	AddChild(toggle_user_button_);
	AddChild(delete_user_button_);
	AddChild(user_select_);
	AddChild(username_input_);
	AddChild(password_input_);
	AddChild(passphrase_input_);
	AddChild(generate_button_);
	AddChild(drive_select_);
	AddChild(create_button_);
	AddChild(change_button_);
	AddChild(verify_button_);
	AddChild(write_button_);
	AddChild(erase_button_);
	AddChild(action_label_);
}

void SecurityFlashPanelWidget::Render(Rendering::Renderer& renderer,
                                      const Theme::Tokens& tokens) {
	EnsureChildrenAttached();
	const auto bounds = GetBounds();
	renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
	renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
}

void SecurityFlashPanelWidget::Layout(const Layout::Rect& bounds) {
	Layout(bounds, CachedLayoutTokens());
}

void SecurityFlashPanelWidget::Layout(const Layout::Rect& bounds,
                                      const Theme::Tokens& tokens) {
	CacheLayoutTokens(tokens);
	SetBounds(bounds);
	constexpr float kPad = 16.0f;
	const float w = bounds.width - 2.0f * kPad;
	float y = bounds.y + kPad;

	service_label_->Layout({bounds.x + kPad, y, w, 24.0f}, CachedLayoutTokens());
	y += 30.0f;

	// Service buttons row.
	{
		constexpr int kN = 5;
		constexpr float kGap = 8.0f;
		const float bw = (w - kGap * (kN - 1)) / kN;
		float bx = bounds.x + kPad;
		for (Button* b : {install_button_.get(), uninstall_button_.get(),
		                  start_button_.get(), stop_button_.get(),
		                  refresh_button_.get()}) {
			b->Layout({bx, y, bw, 32.0f}, CachedLayoutTokens());
			bx += bw + kGap;
		}
	}
	y += 40.0f;

	section_label_->Layout({bounds.x + kPad, y, w, 18.0f}, CachedLayoutTokens());
	y += 22.0f;

	accounts_area_->Layout({bounds.x + kPad, y, w, 104.0f}, CachedLayoutTokens());
	y += 112.0f;

	const float col = (w - 12.0f) / 2.0f;
	constexpr float kInputH = 34.0f;

	// Edit existing user: picker + display name, description + actions.
	user_select_->Layout({bounds.x + kPad, y, col, kInputH}, CachedLayoutTokens());
	fullname_input_->Layout({bounds.x + kPad + col + 12.0f, y, col, kInputH},
	                        CachedLayoutTokens());
	y += kInputH + 10.0f;
	comment_input_->Layout({bounds.x + kPad, y, col, kInputH}, CachedLayoutTokens());
	{
		constexpr float kGap = 8.0f;
		const float bw = (col - 2.0f * kGap) / 3.0f;
		float bx = bounds.x + kPad + col + 12.0f;
		for (Button* b : {save_user_button_.get(), toggle_user_button_.get(),
		                  delete_user_button_.get()}) {
			b->Layout({bx, y, bw, kInputH}, CachedLayoutTokens());
			bx += bw + kGap;
		}
	}
	y += kInputH + 18.0f;

	// Two-column input rows.
	constexpr float kGenerateW = 88.0f;
	username_input_->Layout({bounds.x + kPad, y, col, kInputH}, CachedLayoutTokens());
	password_input_->Layout({bounds.x + kPad + col + 12.0f, y,
	                         col - kGenerateW - 8.0f, kInputH},
	                        CachedLayoutTokens());
	generate_button_->Layout({bounds.x + kPad + w - kGenerateW, y,
	                          kGenerateW, kInputH}, CachedLayoutTokens());
	y += kInputH + 10.0f;

	passphrase_input_->Layout({bounds.x + kPad, y, col, kInputH}, CachedLayoutTokens());
	drive_select_->Layout({bounds.x + kPad + col + 12.0f, y, col,
	                       kInputH}, CachedLayoutTokens());
	y += kInputH + 14.0f;

	// Account action buttons.
	{
		constexpr int kN = 5;
		constexpr float kGap = 8.0f;
		const float bw = (w - kGap * (kN - 1)) / kN;
		float bx = bounds.x + kPad;
		for (Button* b : {create_button_.get(), change_button_.get(),
		                  verify_button_.get(), write_button_.get(),
		                  erase_button_.get()}) {
			b->Layout({bx, y, bw, 34.0f}, CachedLayoutTokens());
			bx += bw + kGap;
		}
	}
	y += 42.0f;

	action_label_->Layout({bounds.x + kPad, y, w, 20.0f}, CachedLayoutTokens());
}

bool SecurityFlashPanelWidget::HandleEvent(const Event&) {
	return false;
}

Layout::Size SecurityFlashPanelWidget::GetMinSize() const {
	return {640.0f, 510.0f};
}

Layout::Size SecurityFlashPanelWidget::GetPreferredSize() const {
	return {760.0f, 566.0f};
}

Layout::Size SecurityFlashPanelWidget::GetMaxSize() const {
	return {100000.0f, 100000.0f};
}

void SecurityFlashPanelWidget::Tick(double dt_seconds) {
	poll_accumulator_ += dt_seconds;
	accounts_accumulator_ += dt_seconds;
	if (poll_accumulator_ >= kPollIntervalSeconds) {
		poll_accumulator_ = 0.0;
		RefreshStatus();
		RefreshDriveOptions();
	}
	if (accounts_accumulator_ >= kAccountsIntervalSeconds) {
		accounts_accumulator_ = 0.0;
		RefreshAccountsText();
	}
}

void SecurityFlashPanelWidget::RefreshStatus() {
#ifdef _WIN32
	std::string detail;
	const DWORD state = QueryServiceState(&detail);
	std::string text;
	if (state == 0) {
		text = "SecurityFlashService: " + detail;
	} else {
		text = "SecurityFlashService: " + std::string(StateName(state));
	}
	if (text != last_service_state_) {
		last_service_state_ = text;
		service_label_->SetText(text);
	}
#else
	if (last_service_state_.empty()) {
		last_service_state_ = "service control: Windows only";
		service_label_->SetText(last_service_state_);
	}
#endif
}

void SecurityFlashPanelWidget::RefreshAccountsText() {
	std::string out;

	// Local accounts.
	std::vector<std::string> users;
	std::string err;
	if (SFS::EnumerateLocalUsers(&users, &err)) {
		out += "Local accounts (" + std::to_string(users.size()) + "):\n";
		std::vector<std::string> labels;
		for (const auto& u : users) {
			SF::WindowsUserDetails d;
			std::string label = u;
			if (SFS::GetWindowsUserDetails(u, &d, nullptr)) {
				if (!d.full_name.empty()) label += "  (" + d.full_name + ")";
				if (d.disabled) label += "  [disabled]";
			}
			out += "  " + label + "\n";
			labels.push_back(std::move(label));
		}
		RefreshUserOptions(users, labels);
	} else {
		out += "Local accounts: " + err + "\n";
	}
	out += "\nRemovable drives:\n";

	const auto drives = SFS::EnumerateDrives(/*removable_only=*/true);
	if (drives.empty()) {
		out += "  (none detected)\n";
	} else {
		for (const auto& d : drives) out += "  " + DriveStatusLine(d) + "\n";
	}

	if (out != last_accounts_text_) {
		last_accounts_text_ = out;
		accounts_area_->SetText(out);
	}
}

void SecurityFlashPanelWidget::RefreshUserOptions(
    const std::vector<std::string>& users,
    const std::vector<std::string>& labels) {
	std::string key;
	for (const auto& l : labels) key += l + "\n";
	if (key == last_user_options_key_) return;
	last_user_options_key_ = key;
	user_names_ = users;

	// Re-select the user being edited without reloading the fields.
	int index = -1;
	for (int i = 0; i < static_cast<int>(users.size()); ++i) {
		if (users[i] == editing_user_) {
			index = i;
			break;
		}
	}
	suppress_user_select_ = true;
	user_select_->SetOptions(labels);
	user_select_->SetSelectedIndex(index);
	suppress_user_select_ = false;
}

void SecurityFlashPanelWidget::LoadUserForEdit(const std::string& username) {
	SF::WindowsUserDetails d;
	std::string err;
	if (!SFS::GetWindowsUserDetails(username, &d, &err)) {
		SetActionResult("cannot load '" + username + "': " + err);
		return;
	}
	editing_user_ = username;
	editing_disabled_ = d.disabled;
	pending_delete_user_.clear();
	username_input_->SetValue(username);
	password_input_->SetValue("");
	fullname_input_->SetValue(d.full_name);
	comment_input_->SetValue(d.comment);
	toggle_user_button_->SetText(d.disabled ? "Enable" : "Disable");
	SetActionResult("editing '" + username + "'" +
	                (d.disabled ? " (disabled)" : "") +
	                " — change fields and press Save Changes");
}

void SecurityFlashPanelWidget::ClearUserEdit() {
	editing_user_.clear();
	editing_disabled_ = false;
	pending_delete_user_.clear();
	fullname_input_->SetValue("");
	comment_input_->SetValue("");
	toggle_user_button_->SetText("Disable");
	suppress_user_select_ = true;
	user_select_->SetSelectedIndex(-1);
	suppress_user_select_ = false;
}

void SecurityFlashPanelWidget::RefreshDriveOptions() {
	const auto drives = SFS::EnumerateDrives(/*removable_only=*/true);
	std::vector<std::string> options;
	for (const auto& d : drives) {
		if (!d.letter) continue;
		std::string opt = DriveStatusLine(d);
		// Select shows the whole status line; letter is the first char.
		options.push_back(opt);
	}
	if (options.empty()) {
		options.push_back("(no removable drives)");
	}

	std::string key;
	for (const auto& o : options) key += o + "\n";
	if (key == last_drive_options_key_) return;
	last_drive_options_key_ = key;

	// Preserve the current selection by drive letter when possible.
	const std::string prev = drive_select_->GetSelectedText();
	char prev_letter = '\0';
	if (!prev.empty()) {
		const char c = prev[0];
		if (c >= 'a' && c <= 'z') {
			prev_letter = static_cast<char>(c - 'a' + 'A');
		} else if (c >= 'A' && c <= 'Z') {
			prev_letter = c;
		}
	}

	drive_select_->SetOptions(options);
	if (prev_letter) {
		for (int i = 0; i < static_cast<int>(options.size()); ++i) {
			if (!options[i].empty() &&
			    std::toupper(options[i][0]) == prev_letter) {
				drive_select_->SetSelectedIndex(i);
				break;
			}
		}
	}
}

char SecurityFlashPanelWidget::SelectedDriveLetter() const {
	const std::string& sel = drive_select_->GetSelectedText();
	if (sel.empty()) return '\0';
	char c = sel[0];
	if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
	return (c >= 'A' && c <= 'Z') ? c : '\0';
}

bool SecurityFlashPanelWidget::SelectedDriveReady() const {
	const char letter = SelectedDriveLetter();
	if (!letter) return false;
	for (const auto& d : SFS::EnumerateDrives(true)) {
		if (d.letter == letter) return d.ready;
	}
	return false;
}

void SecurityFlashPanelWidget::SetActionResult(const std::string& text) {
	action_label_->SetText(text);
}

// ---- service actions ----------------------------------------------------

void SecurityFlashPanelWidget::OnInstall() {
#ifdef _WIN32
	std::string err;
	if (LaunchServiceTool(L"--install", &err)) {
		SetActionResult("install requested (UAC) — status refreshes "
		                "automatically");
	} else {
		SetActionResult("install failed: " + err);
	}
#else
	SetActionResult("not supported on this platform");
#endif
}

void SecurityFlashPanelWidget::OnUninstall() {
#ifdef _WIN32
	std::string err;
	if (LaunchServiceTool(L"--uninstall", &err)) {
		SetActionResult("uninstall requested (UAC)");
	} else {
		SetActionResult("uninstall failed: " + err);
	}
#else
	SetActionResult("not supported on this platform");
#endif
}

void SecurityFlashPanelWidget::OnStart() {
#ifdef _WIN32
	std::string err;
	if (ServiceControl(/*start=*/true, &err)) {
		SetActionResult("service started");
	} else {
		SetActionResult("start failed: " + err);
	}
#else
	SetActionResult("not supported on this platform");
#endif
}

void SecurityFlashPanelWidget::OnStop() {
#ifdef _WIN32
	std::string err;
	if (ServiceControl(/*start=*/false, &err)) {
		SetActionResult("service stopped");
	} else {
		SetActionResult("stop failed: " + err);
	}
#else
	SetActionResult("not supported on this platform");
#endif
}

void SecurityFlashPanelWidget::OnRefresh() {
	poll_accumulator_ = kPollIntervalSeconds;
	accounts_accumulator_ = kAccountsIntervalSeconds;
	RefreshStatus();
	RefreshDriveOptions();
	RefreshAccountsText();
}

// ---- account actions ----------------------------------------------------

void SecurityFlashPanelWidget::OnGeneratePassword() {
	const auto res = PasswordGen::Generate(PasswordPolicy{});
	if (res.ok) {
		password_input_->SetValue(res.password);
		SetActionResult("password generated (20 chars)");
	} else {
		SetActionResult("generate failed: " + res.error);
	}
}

void SecurityFlashPanelWidget::OnCreateAccount() {
	const std::string user = username_input_->GetValue();
	const std::string pass = password_input_->GetValue();
	const std::string phrase = passphrase_input_->GetValue();
	const char drive = SelectedDriveLetter();

	if (user.empty()) {
		SetActionResult("username is required");
		return;
	}
	if (phrase.empty()) {
		SetActionResult("flash passphrase is required");
		return;
	}
	if (!drive) {
		SetActionResult("select a removable drive");
		return;
	}
	if (!SelectedDriveReady()) {
		SetActionResult("selected drive has no media");
		return;
	}

	SF::ProvisionOptions opts;
	opts.username = user;
	opts.password = pass;  // empty -> generated by Provision
	opts.passphrase = phrase;
	opts.drive_letter = std::string(1, drive);
	opts.create_windows_user = true;

	const auto res = SFS::Provision(opts);
	if (!res.success) {
		SetActionResult("provision failed: " + res.error);
		return;
	}
	// Surface a generated password so the user can save it once.
	if (res.password_was_generated) {
		password_input_->SetValue(res.password);
		SetActionResult("created '" + res.username +
		                "' — generated password shown in the field");
	} else {
		SetActionResult("created '" + res.username +
		                "' and wrote token to " + res.drive_letter);
	}
	RefreshAccountsText();
}

void SecurityFlashPanelWidget::OnChangePassword() {
	const std::string user = username_input_->GetValue();
	const std::string pass = password_input_->GetValue();
	const std::string phrase = passphrase_input_->GetValue();
	const char drive = SelectedDriveLetter();

	if (user.empty() || pass.empty() || phrase.empty() || !drive ||
	    !SelectedDriveReady()) {
		SetActionResult(
		    "need username, password, passphrase and a ready drive");
		return;
	}
	const std::string root =
	    std::string(1, drive) + ":\\";

	// Windows side first: NetUserSetInfo level 1003.
	std::string err;
	if (!SFS::ChangeWindowsPassword(user, pass, &err)) {
		SetActionResult("password change failed: " + err);
		return;
	}

	// Token side: keep payload metadata, swap the password.
	SF::TokenPayload payload;
	if (!SFS::ReadToken(root, phrase, &payload, &err)) {
		// No readable token — write a fresh minimal payload.
		payload = {};
		payload.username = user;
	}
	payload.password = pass;
	if (!SFS::WriteToken(root, payload, phrase, &err)) {
		SetActionResult("Windows password changed but token write "
		                "failed: " + err);
		return;
	}
	SFS::SecureClear(payload.password);
	SetActionResult("password changed and token rewritten on " +
	                std::string(1, drive));
}

void SecurityFlashPanelWidget::OnVerifyPassword() {
	const std::string user = username_input_->GetValue();
	const std::string pass = password_input_->GetValue();
	if (user.empty()) {
		SetActionResult("username is required");
		return;
	}
	std::string err;
	if (SFS::VerifyWindowsPassword(user, pass, &err)) {
		SetActionResult("credentials OK for '" + user + "'");
	} else {
		SetActionResult("verify failed: " + err);
	}
}

void SecurityFlashPanelWidget::OnWriteToken() {
	const std::string user = username_input_->GetValue();
	const std::string pass = password_input_->GetValue();
	const std::string phrase = passphrase_input_->GetValue();
	const char drive = SelectedDriveLetter();

	if (user.empty() || pass.empty() || phrase.empty()) {
		SetActionResult(
		    "need username, password and passphrase");
		return;
	}
	if (!drive || !SelectedDriveReady()) {
		SetActionResult("select a ready removable drive");
		return;
	}
	const std::string root = std::string(1, drive) + ":\\";

	SF::TokenPayload payload;
	payload.username = user;
	payload.password = pass;
	// Preserve volume binding metadata when a token already exists.
	SF::TokenPayload existing;
	std::string err;
	if (SFS::ReadToken(root, phrase, &existing, nullptr)) {
		payload.volume_serial = existing.volume_serial;
		payload.created_utc = existing.created_utc;
		payload.host_name = existing.host_name;
	}
	if (!SFS::WriteToken(root, payload, phrase, &err)) {
		SetActionResult("token write failed: " + err);
		return;
	}
	SetActionResult("token written to " + root);
}

void SecurityFlashPanelWidget::OnEraseToken() {
	const char drive = SelectedDriveLetter();
	if (!drive || !SelectedDriveReady()) {
		SetActionResult("select a ready removable drive");
		return;
	}
	const std::string root = std::string(1, drive) + ":\\";
	std::string err;
	if (SFS::EraseToken(root, &err)) {
		SetActionResult("token erased from " + root);
	} else {
		SetActionResult("erase failed: " + err);
	}
	RefreshAccountsText();
}

// ---- user editing -------------------------------------------------------

void SecurityFlashPanelWidget::OnUserSelected(int index) {
	if (suppress_user_select_) return;
	if (index < 0 || index >= static_cast<int>(user_names_.size())) return;
	LoadUserForEdit(user_names_[static_cast<std::size_t>(index)]);
}

void SecurityFlashPanelWidget::OnSaveUser() {
	if (editing_user_.empty()) {
		SetActionResult("select a user to edit first");
		return;
	}
	std::string err;
	std::string target = editing_user_;
	const std::string new_name = username_input_->GetValue();
	if (!new_name.empty() && new_name != editing_user_) {
		if (!SFS::RenameWindowsUser(editing_user_, new_name, &err)) {
			SetActionResult("rename failed: " + err);
			return;
		}
		target = new_name;
		editing_user_ = new_name;
	}

	SF::WindowsUserDetails d;
	d.full_name = fullname_input_->GetValue();
	d.comment = comment_input_->GetValue();
	d.disabled = editing_disabled_;
	if (!SFS::UpdateWindowsUserDetails(target, d, &err)) {
		SetActionResult("save failed: " + err);
		OnRefresh();
		return;
	}

	std::string extra;
	const std::string pass = password_input_->GetValue();
	if (!pass.empty()) {
		if (!SFS::ChangeWindowsPassword(target, pass, &err)) {
			SetActionResult("saved '" + target +
			                "', but password change failed: " + err);
			OnRefresh();
			return;
		}
		extra = ", password changed (token not rewritten)";
	}
	SetActionResult("saved '" + target + "'" + extra);
	OnRefresh();
}

void SecurityFlashPanelWidget::OnToggleUser() {
	if (editing_user_.empty()) {
		SetActionResult("select a user to edit first");
		return;
	}
	if (!editing_disabled_ && IsSignedInUser(editing_user_)) {
		SetActionResult("cannot disable the account you are signed in with");
		return;
	}
	// Start from the stored values so unsaved field edits are not applied.
	SF::WindowsUserDetails d;
	std::string err;
	if (!SFS::GetWindowsUserDetails(editing_user_, &d, &err)) {
		SetActionResult("cannot load '" + editing_user_ + "': " + err);
		return;
	}
	d.disabled = !d.disabled;
	if (!SFS::UpdateWindowsUserDetails(editing_user_, d, &err)) {
		SetActionResult(std::string(d.disabled ? "disable" : "enable") +
		                " failed: " + err);
		return;
	}
	editing_disabled_ = d.disabled;
	toggle_user_button_->SetText(d.disabled ? "Enable" : "Disable");
	SetActionResult("'" + editing_user_ + "' " +
	                (d.disabled ? "disabled" : "enabled"));
	OnRefresh();
}

void SecurityFlashPanelWidget::OnDeleteUser() {
	if (editing_user_.empty()) {
		SetActionResult("select a user to edit first");
		return;
	}
	if (IsSignedInUser(editing_user_)) {
		SetActionResult("cannot delete the account you are signed in with");
		return;
	}
	if (pending_delete_user_ != editing_user_) {
		pending_delete_user_ = editing_user_;
		SetActionResult("click Delete User again to permanently delete '" +
		                editing_user_ + "'");
		return;
	}
	std::string err;
	const std::string user = editing_user_;
	if (!SFS::DeleteWindowsUser(user, &err)) {
		pending_delete_user_.clear();
		SetActionResult("delete failed: " + err);
		return;
	}
	ClearUserEdit();
	username_input_->SetValue("");
	SetActionResult("deleted '" + user +
	                "' (profile folder in C:\\Users is kept)");
	OnRefresh();
}

}  // namespace XER::UI::Widgets
