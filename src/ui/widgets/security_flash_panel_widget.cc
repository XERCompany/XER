#include "security_flash_panel_widget.h"

#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "rendering/renderer.h"
#include "security_flash/security_flash.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/textarea.h"

namespace XER::UI::Widgets {
namespace {

// XER::SecurityFlash is both a namespace and the class inside it.
using SFS = XER::SecurityFlash::SecurityFlash;

constexpr double kPollIntervalSeconds = 0.75;

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

// Polls until `wanted` state or timeout; returns true on success.
bool WaitForState(SC_HANDLE svc, DWORD wanted, DWORD timeout_ms) {
	const DWORD deadline = GetTickCount() + timeout_ms;
	for (;;) {
		SERVICE_STATUS_PROCESS ssp{};
		DWORD needed = 0;
		if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
		                          reinterpret_cast<BYTE*>(&ssp),
		                          sizeof(ssp), &needed)) {
			return false;
		}
		if (ssp.dwCurrentState == wanted) return true;
		if (GetTickCount() > deadline) return false;
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
		// Already in the desired state is not an error.
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
// The child performs its own SCM work; UAC consent happens here.
bool LaunchServiceTool(const wchar_t* arg, std::string* error) {
	const std::wstring exe = SiblingServiceExe();
	if (exe.empty() || GetFileAttributesW(exe.c_str()) ==
	                       INVALID_FILE_ATTRIBUTES) {
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

std::string BuildDrivesText() {
#ifdef _WIN32
	const auto drives = SFS::EnumerateDrives(/*removable_only=*/true);
	if (drives.empty()) {
		return "No removable drives detected.\n"
		       "Insert a flash drive and press Refresh.";
	}
	std::string out;
	for (const auto& d : drives) {
		char letter = d.letter ? d.letter : '?';
		const bool token = d.ready && SFS::TokenExists(d.root_path);
		char line[512];
		std::snprintf(line, sizeof(line),
		              "%c:\\  %-24s serial=0x%08X  token: %s\n",
		              letter, d.volume_label.c_str(), d.volume_serial,
		              !d.ready ? "no media" : (token ? "ARMED" : "none"));
		out += line;
	}
	return out;
#else
	return "Drive monitoring is not supported on this platform.";
#endif
}

}  // namespace

SecurityFlashPanelWidget::SecurityFlashPanelWidget() {
	service_label_ = std::make_shared<Label>("SecurityFlashService: …",
	                                         /*muted=*/false);
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

	drives_area_ = std::make_shared<Textarea>();
	drives_area_->SetReadOnly(true);

	install_button_->SetOnClick([this] { OnInstall(); });
	uninstall_button_->SetOnClick([this] { OnUninstall(); });
	start_button_->SetOnClick([this] { OnStart(); });
	stop_button_->SetOnClick([this] { OnStop(); });
	refresh_button_->SetOnClick([this] { OnRefresh(); });

	install_button_->SetTooltip("Register XERSecurityFlash (elevated)");
	uninstall_button_->SetTooltip("Remove XERSecurityFlash (elevated)");
	start_button_->SetTooltip("Start the service via SCM");
	stop_button_->SetTooltip("Stop the service via SCM");
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
	AddChild(action_label_);
	AddChild(drives_area_);
}

void SecurityFlashPanelWidget::Render(Rendering::Renderer& renderer,
                                      const Theme::Tokens& tokens) {
	EnsureChildrenAttached();
	const auto bounds = GetBounds();
	renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
	renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
}

void SecurityFlashPanelWidget::Layout(const Layout::Rect& bounds) {
	Layout(bounds, Theme::Tokens{});
}

void SecurityFlashPanelWidget::Layout(const Layout::Rect& bounds,
                                      const Theme::Tokens&) {
	SetBounds(bounds);
	constexpr float kPad = 16.0f;
	const float w = bounds.width - 2.0f * kPad;

	service_label_->Layout(
	    {bounds.x + kPad, bounds.y + kPad, w, 26.0f});

	constexpr int kButtons = 5;
	constexpr float kGap = 8.0f;
	const float bw = (w - kGap * (kButtons - 1)) / kButtons;
	const float by = bounds.y + kPad + 34.0f;
	const float bh = 34.0f;
	float bx = bounds.x + kPad;
	for (Button* b : {install_button_.get(), uninstall_button_.get(),
	                  start_button_.get(), stop_button_.get(),
	                  refresh_button_.get()}) {
		b->Layout({bx, by, bw, bh});
		bx += bw + kGap;
	}

	action_label_->Layout(
	    {bounds.x + kPad, by + bh + 8.0f, w, 20.0f});

	drives_area_->Layout({bounds.x + kPad, by + bh + 36.0f, w,
	                      bounds.height - (by + bh + 36.0f - bounds.y) -
	                          kPad});
}

bool SecurityFlashPanelWidget::HandleEvent(const Event&) {
	return false;
}

Layout::Size SecurityFlashPanelWidget::GetMinSize() const {
	return {480.0f, 240.0f};
}

Layout::Size SecurityFlashPanelWidget::GetPreferredSize() const {
	return {640.0f, 320.0f};
}

Layout::Size SecurityFlashPanelWidget::GetMaxSize() const {
	return {100000.0f, 100000.0f};
}

void SecurityFlashPanelWidget::Tick(double dt_seconds) {
	poll_accumulator_ += dt_seconds;
	if (poll_accumulator_ >= kPollIntervalSeconds) {
		poll_accumulator_ = 0.0;
		RefreshStatus();
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

	const std::string drives = BuildDrivesText();
	if (drives != last_drives_text_) {
		last_drives_text_ = drives;
		drives_area_->SetText(drives);
	}
}

void SecurityFlashPanelWidget::SetActionResult(const std::string& text) {
	action_label_->SetText(text);
}

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
	RefreshStatus();
}

}  // namespace XER::UI::Widgets
