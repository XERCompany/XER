// SecurityFlashService — Windows service monitoring removable media for
// XER SecurityFlash tokens (xer_flash.tok).
//
// The service polls mounted removable drives and logs transitions:
// drive inserted/ejected and credential token armed/disarmed.
//
// It also serves the SecurityFlashHello credential provider over a
// SYSTEM-only named pipe (\\.\pipe\XERSecurityFlashHello): LIST reports
// token drives and their login mode, LOGON decrypts a token (sealed key
// for flash-only drives, PIN otherwise) and returns the credentials.
//
// Usage:
//   SecurityFlashService.exe              run under the Service Control Manager
//   SecurityFlashService.exe --console    run the monitor in the foreground
//   SecurityFlashService.exe --install    register the service (elevated)
//   SecurityFlashService.exe --uninstall  remove the service (elevated)
//   SecurityFlashService.exe --help

#include <windows.h>
#include <wtsapi32.h>
#include <sddl.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "helper/string_utils.h"
#include "security_flash/security_flash.h"

namespace fs = std::filesystem;

// XER::SecurityFlash is both a namespace and the class inside it.
using SFS = XER::SecurityFlash::SecurityFlash;

namespace {

constexpr wchar_t kServiceName[] = L"XERSecurityFlash";
constexpr wchar_t kServiceDisplayName[] = L"XER Security Flash Monitor";
constexpr DWORD kPollIntervalMs = 2000;

SERVICE_STATUS_HANDLE g_status_handle = nullptr;
SERVICE_STATUS g_status{};
HANDLE g_stop_event = nullptr;
std::atomic<bool> g_running{false};
std::mutex g_log_mutex;

std::wstring ExePath() {
	wchar_t buf[MAX_PATH];
	const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	return n ? std::wstring(buf, n) : std::wstring();
}

std::wstring LogPath() {
	wchar_t buf[MAX_PATH];
	const DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
	fs::path dir = n ? fs::path(buf) / L"XER" / L"SecurityFlash"
	                 : fs::path(L"C:\\ProgramData\\XER\\SecurityFlash");
	std::error_code ec;
	fs::create_directories(dir, ec);
	return (dir / L"service.log").wstring();
}

void Log(const std::string& message) {
	const auto now = std::chrono::system_clock::now();
	const auto t = std::chrono::system_clock::to_time_t(now);
	tm tmv{};
	localtime_s(&tmv, &t);
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);

	std::lock_guard lock(g_log_mutex);
	std::ofstream f(LogPath(), std::ios::app);
	if (f) f << "[" << stamp << "] " << message << "\n";
	OutputDebugStringA((std::string("[SecurityFlashService] ") + message +
	                    "\n")
	                       .c_str());
}

void SetServiceState(DWORD state, DWORD exit_code = NO_ERROR,
                     DWORD wait_hint = 0) {
	if (!g_status_handle) return;
	g_status.dwCurrentState = state;
	g_status.dwWin32ExitCode = exit_code;
	g_status.dwWaitHint = wait_hint;
	SetServiceStatus(g_status_handle, &g_status);
}

VOID WINAPI ServiceCtrlHandler(DWORD control) {
	switch (control) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		Log("stop requested");
		g_running = false;
		SetServiceState(SERVICE_STOP_PENDING, NO_ERROR, 5000);
		if (g_stop_event) SetEvent(g_stop_event);
		break;
	default:
		break;
	}
}

// Physical-key policy: when the flash drive carrying an armed
// SecurityFlash token is removed, every active interactive session is
// locked (not logged off): programs keep running, but the user has to
// re-enter the Windows password. LockWorkStation only works from the
// interactive desktop, so the service (session 0) disconnects the
// sessions instead, which drops them to the Windows lock/sign-in screen.
void TriggerSessionLock() {
	WTS_SESSION_INFOW* sessions = nullptr;
	DWORD count = 0;
	if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1,
	                           &sessions, &count)) {
		Log("lock: WTSEnumerateSessions failed: " +
		    std::to_string(GetLastError()));
		return;
	}
	int locked = 0;
	for (DWORD i = 0; i < count; ++i) {
		if (sessions[i].State != WTSActive || sessions[i].SessionId == 0) {
			continue;
		}
		if (WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE,
		                         sessions[i].SessionId, FALSE)) {
			++locked;
		} else {
			Log("lock: WTSDisconnectSession(" +
			    std::to_string(sessions[i].SessionId) + ") failed: " +
			    std::to_string(GetLastError()));
		}
	}
	WTSFreeMemory(sessions);
	Log("lock: armed token drive removed — locked " +
	    std::to_string(locked) + " session(s)");
}

// One monitoring pass: snapshot removable drives + token presence,
// log transitions against the previous snapshot.
void MonitorPass(
    std::map<char, bool>& armed,
    std::map<char, XER::SecurityFlash::DriveInfo>& drives) {
	std::map<char, XER::SecurityFlash::DriveInfo> current;
	for (const auto& d :
	     SFS::EnumerateDrives(/*removable_only=*/true)) {
		if (d.letter && d.ready) current[d.letter] = d;
	}

	// Drives that disappeared.
	bool armed_ejected = false;
	for (const auto& [letter, info] : drives) {
		if (!current.count(letter)) {
			const bool was_armed = armed.count(letter) && armed[letter];
			Log(std::string("drive ejected: ") + letter +
			    (was_armed ? " (token was armed)" : ""));
			armed_ejected |= was_armed;
		}
	}
	// Physical-key policy: losing the armed token drive locks sessions.
	if (armed_ejected) TriggerSessionLock();
	// Drives that appeared or changed readiness.
	for (const auto& [letter, info] : current) {
		const auto it = drives.find(letter);
		if (it == drives.end()) {
			Log(std::string("drive inserted: ") + letter + " '" +
			    info.volume_label + "'");
		}
	}

	// Token armed/disarmed transitions.
	for (const auto& [letter, info] : current) {
		const bool has = SFS::TokenExists(info.root_path);
		const bool was = armed[letter];
		if (has && !was) {
			Log(std::string("token armed on ") + letter + ":" +
			    info.root_path);
		} else if (!has && was) {
			Log(std::string("token removed from ") + letter + ":" +
			    info.root_path);
		}
		armed[letter] = has;
	}
	// Drop state for ejected drives.
	for (auto it = armed.begin(); it != armed.end();) {
		if (!current.count(it->first)) it = armed.erase(it);
		else ++it;
	}

	drives = std::move(current);
}

void MonitorLoop() {
	std::map<char, bool> armed;
	std::map<char, XER::SecurityFlash::DriveInfo> drives;

	Log("monitor started");
	while (g_running) {
		MonitorPass(armed, drives);
		if (WaitForSingleObject(g_stop_event, kPollIntervalMs) ==
		    WAIT_OBJECT_0) {
			break;
		}
	}
	Log("monitor stopped");
}

// --- SecurityFlashHello credential pipe -----------------------------------

constexpr wchar_t kHelloPipeName[] = L"\\\\.\\pipe\\XERSecurityFlashHello";
constexpr DWORD kHelloPipeBufferBytes = 4096;
constexpr DWORD kHelloIoTimeoutMs = 5000;

bool HelloPipeIo(HANDLE pipe, bool write, void* data, DWORD size,
                 DWORD* done) {
	OVERLAPPED ov{};
	ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!ov.hEvent) return false;
	const BOOL started = write ? WriteFile(pipe, data, size, nullptr, &ov)
	                           : ReadFile(pipe, data, size, nullptr, &ov);
	if (!started && GetLastError() != ERROR_IO_PENDING) {
		CloseHandle(ov.hEvent);
		return false;
	}
	if (WaitForSingleObject(ov.hEvent, kHelloIoTimeoutMs) != WAIT_OBJECT_0) {
		CancelIo(pipe);
		GetOverlappedResult(pipe, &ov, done, TRUE);
		CloseHandle(ov.hEvent);
		return false;
	}
	const BOOL ok = GetOverlappedResult(pipe, &ov, done, FALSE);
	CloseHandle(ov.hEvent);
	return ok != FALSE;
}

// Usernames of flash-only tokens by volume serial, so LIST does not
// re-run the token KDF every poll.
std::map<std::uint32_t, std::string> g_flash_user_cache;

namespace Str = XER::Helper::String;

std::string HandleHelloRequest(const std::string& request) {
	if (request == "LIST") {
		std::string out = "LIST\n";
		for (const auto& d : SFS::EnumerateDrives(/*removable_only=*/true)) {
			if (!d.letter || !d.ready || !SFS::TokenExists(d.root_path)) {
				continue;
			}
			const bool flash_only = SFS::IsFlashOnlyLogin(d.root_path);
			std::string user;
			if (flash_only) {
				auto it = g_flash_user_cache.find(d.volume_serial);
				if (it != g_flash_user_cache.end()) {
					user = it->second;
				} else {
					XER::SecurityFlash::TokenPayload payload;
					if (SFS::ReadLoginToken(d.root_path, "", &payload,
					                        nullptr)) {
						user = payload.username;
						SFS::SecureClear(payload.password);
						g_flash_user_cache[d.volume_serial] = user;
					}
				}
			}
			out += std::string(1, d.letter) + "\t" +
			       (flash_only ? "flash" : "pin") + "\t" + user + "\n";
		}
		static std::string last_list;
		if (out != last_list) {
			last_list = out;
			const std::string shown = Str::ReplaceAll(
			    Str::ReplaceAll(out.substr(5), "\t", " "), "\n", ";");
			Log("hello: LIST -> " + (shown.empty() ? "(no token drives)" : shown));
		}
		return out;
	}

	constexpr char kLogon[] = "LOGON\t";
	if (Str::StartsWith(request, kLogon)) {
		constexpr std::size_t kLetterPos = sizeof(kLogon) - 1;
		if (request.size() < kLetterPos + 2 ||
		    request[kLetterPos + 1] != '\t' ||
		    !std::isalpha(static_cast<unsigned char>(request[kLetterPos]))) {
			return "ERR\tbad request";
		}
		const std::string root = std::string(1, request[kLetterPos]) + ":\\";
		std::string pin = request.substr(kLetterPos + 2);
		XER::SecurityFlash::TokenPayload payload;
		std::string err;
		const bool ok = SFS::ReadLoginToken(root, pin, &payload, &err);
		SFS::SecureClear(pin);
		if (!ok) {
			Log("hello: sign-in rejected for " + root + ": " + err);
			// Slow down PIN guessing.
			Sleep(1500);
			return "ERR\t" + (err.empty() ? std::string("wrong PIN") : err);
		}
		Log("hello: credentials released for '" + payload.username +
		    "' from " + root);
		std::string reply = "OK\t" + payload.username + "\t" +
		                    payload.password;
		SFS::SecureClear(payload.password);
		return reply;
	}
	return "ERR\tunknown request";
}

void HelloPipeLoop() {
	// SYSTEM only: LogonUI, which hosts the credential provider, runs as
	// SYSTEM, so no user process can request credentials.
	PSECURITY_DESCRIPTOR sd = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
	        L"D:P(A;;GA;;;SY)", SDDL_REVISION_1, &sd, nullptr)) {
		Log("hello: security descriptor failed: " +
		    std::to_string(GetLastError()));
		return;
	}
	SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
	Log("hello: credential pipe started");

	while (g_running) {
		HANDLE pipe = CreateNamedPipeW(
		    kHelloPipeName,
		    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
		        FILE_FLAG_FIRST_PIPE_INSTANCE,
		    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
		        PIPE_REJECT_REMOTE_CLIENTS,
		    1, kHelloPipeBufferBytes, kHelloPipeBufferBytes, 0, &sa);
		if (pipe == INVALID_HANDLE_VALUE) {
			Log("hello: CreateNamedPipe failed: " +
			    std::to_string(GetLastError()));
			if (WaitForSingleObject(g_stop_event, 5000) == WAIT_OBJECT_0) {
				break;
			}
			continue;
		}

		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
		if (!connected) {
			const DWORD err = GetLastError();
			if (err == ERROR_PIPE_CONNECTED) {
				connected = true;
			} else if (err == ERROR_IO_PENDING) {
				HANDLE waits[2] = {ov.hEvent, g_stop_event};
				DWORD unused = 0;
				if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) ==
				    WAIT_OBJECT_0) {
					connected =
					    GetOverlappedResult(pipe, &ov, &unused, FALSE) != FALSE;
				} else {
					CancelIo(pipe);
					GetOverlappedResult(pipe, &ov, &unused, TRUE);
				}
			}
		}
		if (ov.hEvent) CloseHandle(ov.hEvent);

		if (connected) {
			char buf[kHelloPipeBufferBytes];
			DWORD read = 0;
			if (HelloPipeIo(pipe, false, buf, sizeof(buf), &read) &&
			    read > 0) {
				std::string request(buf, read);
				std::string reply = HandleHelloRequest(request);
				SFS::SecureClear(request);
				DWORD written = 0;
				if (HelloPipeIo(pipe, true, reply.data(),
				                static_cast<DWORD>(reply.size()), &written)) {
					FlushFileBuffers(pipe);
				}
				SFS::SecureClear(reply);
			}
			SecureZeroMemory(buf, sizeof(buf));
			DisconnectNamedPipe(pipe);
		}
		CloseHandle(pipe);
	}

	LocalFree(sd);
	Log("hello: credential pipe stopped");
}

VOID WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
	g_status_handle = RegisterServiceCtrlHandlerW(kServiceName,
	                                             ServiceCtrlHandler);
	if (!g_status_handle) return;

	g_status = {};
	g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_status.dwControlsAccepted =
	    SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
	g_status.dwCurrentState = SERVICE_START_PENDING;
	g_status.dwWin32ExitCode = NO_ERROR;
	g_status.dwCheckPoint = 1;
	g_status.dwWaitHint = 3000;
	SetServiceStatus(g_status_handle, &g_status);

	g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!g_stop_event) {
		g_status.dwCurrentState = SERVICE_STOPPED;
		g_status.dwWin32ExitCode = GetLastError();
		SetServiceStatus(g_status_handle, &g_status);
		return;
	}

	g_running = true;
	SetServiceState(SERVICE_RUNNING);

	std::thread worker(MonitorLoop);
	std::thread hello(HelloPipeLoop);
	worker.join();
	hello.join();

	CloseHandle(g_stop_event);
	g_stop_event = nullptr;
	SetServiceState(SERVICE_STOPPED);
}

// --- install / uninstall -------------------------------------------------

// True when the current process token is elevated (UAC admin).
bool IsElevated() {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}
	TOKEN_ELEVATION elev{};
	DWORD returned = 0;
	const BOOL ok = GetTokenInformation(token, TokenElevation, &elev,
	                                    sizeof(elev), &returned);
	CloseHandle(token);
	return ok && elev.TokenIsElevated != 0;
}

// Relaunch this exe elevated via UAC and wait for it to finish.
// Returns the child's exit code, or 1 on failure/cancellation.
int RunElevated(const wchar_t* arguments) {
	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = L"runas";
	const std::wstring exe = ExePath();  // keep alive for lpFile
	sei.lpFile = exe.c_str();
	sei.lpParameters = arguments;
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei)) {
		const DWORD err = GetLastError();
		if (err == ERROR_CANCELLED) {
			std::printf("elevation cancelled\n");
		} else {
			std::fprintf(stderr, "elevation failed: %lu\n", err);
		}
		return 1;
	}
	WaitForSingleObject(sei.hProcess, INFINITE);
	DWORD code = 1;
	GetExitCodeProcess(sei.hProcess, &code);
	CloseHandle(sei.hProcess);
	return static_cast<int>(code);
}

// Opens the SCM; on access-denied from a non-elevated token, relaunches
// this exe elevated with `elevated_arg` and returns its exit code.
// Returns the SCM handle on success, nullptr after reporting.
SC_HANDLE OpenSCManagerElevated(const wchar_t* elevated_arg,
                                int* exit_code) {
	SC_HANDLE scm =
	    OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (scm) return scm;

	const DWORD err = GetLastError();
	if (err == ERROR_ACCESS_DENIED && !IsElevated()) {
		std::printf("administrator rights required — "
		            "requesting elevation...\n");
		*exit_code = RunElevated(elevated_arg);
		return nullptr;
	}
	std::fprintf(stderr, "OpenSCManager failed: %lu\n", err);
	*exit_code = 1;
	return nullptr;
}

int InstallService() {
	int exit_code = 0;
	SC_HANDLE scm = OpenSCManagerElevated(L"--install", &exit_code);
	if (!scm) return exit_code;

	const std::wstring path = ExePath();
	SC_HANDLE svc = CreateServiceW(
	    scm, kServiceName, kServiceDisplayName,
	    SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
	    SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
	    path.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
	if (!svc) {
		const DWORD err = GetLastError();
		if (err == ERROR_SERVICE_EXISTS) {
			std::printf("service already exists\n");
		} else {
			std::fprintf(stderr, "CreateService failed: %lu\n", err);
		}
		CloseServiceHandle(scm);
		return err == ERROR_SERVICE_EXISTS ? 0 : 1;
	}
	const std::wstring desc =
	    L"Monitors removable drives for XER SecurityFlash credential "
	    L"tokens and logs armed/disarmed transitions.";
	SERVICE_DESCRIPTIONW sd{};
	sd.lpDescription = const_cast<LPWSTR>(desc.c_str());
	ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &sd);
	CloseServiceHandle(svc);
	CloseServiceHandle(scm);
	std::printf("installed service '%ls' -> %ls\n", kServiceName,
	            path.c_str());
	return 0;
}

int UninstallService() {
	int exit_code = 0;
	SC_HANDLE scm = OpenSCManagerElevated(L"--uninstall", &exit_code);
	if (!scm) return exit_code;
	SC_HANDLE svc = OpenServiceW(scm, kServiceName, DELETE);
	if (!svc) {
		std::fprintf(stderr, "OpenService failed: %lu\n", GetLastError());
		CloseServiceHandle(scm);
		return 1;
	}
	const BOOL ok = DeleteService(svc);
	const DWORD err = GetLastError();
	CloseServiceHandle(svc);
	CloseServiceHandle(scm);
	if (!ok) {
		std::fprintf(stderr, "DeleteService failed: %lu\n", err);
		return 1;
	}
	std::printf("service '%ls' uninstalled\n", kServiceName);
	return 0;
}

int ConsoleMode() {
	g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!g_stop_event) return 1;
	SetConsoleCtrlHandler(
	    [](DWORD ctrl) -> BOOL {
		    if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT ||
		        ctrl == CTRL_CLOSE_EVENT) {
			    g_running = false;
			    SetEvent(g_stop_event);
			    return TRUE;
		    }
		    return FALSE;
	    },
	    TRUE);
	g_running = true;
	std::printf("monitoring removable drives (Ctrl+C to stop); "
	            "log: %ls\n",
	            LogPath().c_str());
	MonitorLoop();
	CloseHandle(g_stop_event);
	return 0;
}

void Usage() {
	std::printf(
	    "SecurityFlashService — monitors removable media for XER "
	    "SecurityFlash tokens.\n"
	    "Usage:\n"
	    "  SecurityFlashService.exe              console monitor "
	    "(or SCM start)\n"
	    "  SecurityFlashService.exe --console    foreground monitor\n"
	    "  SecurityFlashService.exe --install    register service "
	    "(auto-elevates)\n"
	    "  SecurityFlashService.exe --uninstall  remove service "
	    "(auto-elevates)\n"
	    "  SecurityFlashService.exe --help\n"
	    "Log: %%ProgramData%%\\XER\\SecurityFlash\\service.log\n");
}

}  // namespace

int main(int argc, char** argv) {
	std::string mode = argc > 1 ? argv[1] : "";
	if (mode == "--help" || mode == "-h") {
		Usage();
		return 0;
	}
	if (mode == "--install") return InstallService();
	if (mode == "--uninstall") return UninstallService();
	if (mode == "--console") return ConsoleMode();

	// Default: started by the Service Control Manager.
	SERVICE_TABLE_ENTRYW table[] = {
	    {const_cast<LPWSTR>(kServiceName), ServiceMain},
	    {nullptr, nullptr}};
	if (!StartServiceCtrlDispatcherW(table)) {
		const DWORD err = GetLastError();
		// 1063 = not launched by the SCM: an interactive run. Fall back
		// to the foreground monitor instead of failing.
		if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
			std::printf("not running under the Service Control "
			            "Manager — starting console monitor\n");
			return ConsoleMode();
		}
		std::fprintf(stderr,
		             "StartServiceCtrlDispatcher failed: %lu\n",
		             err);
		return 1;
	}
	return 0;
}
