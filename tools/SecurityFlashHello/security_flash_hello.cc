// SecurityFlashHello — Windows Credential Provider for XER SecurityFlash.
//
// Adds a "SecurityFlashHello" tile to the sign-in and unlock screens. With
// a SecurityFlash drive inserted, the tile signs in the account stored in
// the drive's token:
//   flash-only drives  — sign in automatically (key sealed to this PC),
//   flash + PIN drives — ask for the token passphrase (PIN).
// Token decryption happens in SecurityFlashService (SYSTEM) over a
// SYSTEM-only named pipe; drive/token checks and string helpers come from
// XER::SecurityFlash and XER::Helper::String.
// The standard Windows password tile is left untouched.
//
// Register:   regsvr32 SecurityFlashHello.dll      (elevated)
// Unregister: regsvr32 /u SecurityFlashHello.dll

#include <windows.h>
#include <ntsecapi.h>
#define SECURITY_WIN32
#include <security.h>
#include <credentialprovider.h>
#include <olectl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "helper/string_utils.h"
#include "security_flash/security_flash.h"

namespace {

using SFS = XER::SecurityFlash::SecurityFlash;
using XER::Helper::String::ReplaceAll;
using XER::Helper::String::Split;
using XER::Helper::String::SplitLines;
using XER::Helper::String::StartsWith;
using XER::Helper::String::Utf8ToWide;
using XER::Helper::String::WideToUtf8;

// {7E3F1B52-9C4A-4D1E-B8A6-2F5C0D93E471}
constexpr CLSID kClsid = {0x7e3f1b52, 0x9c4a, 0x4d1e,
                          {0xb8, 0xa6, 0x2f, 0x5c, 0x0d, 0x93, 0xe4, 0x71}};
constexpr wchar_t kClsidString[] = L"{7E3F1B52-9C4A-4D1E-B8A6-2F5C0D93E471}";
constexpr wchar_t kProviderName[] = L"SecurityFlashHello";
constexpr wchar_t kProvidersKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Authentication\\"
    L"Credential Providers\\";
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\XERSecurityFlashHello";

HMODULE g_module = nullptr;
std::atomic<long> g_dll_refs{0};

enum FieldId : DWORD {
	kFieldLabel = 0,
	kFieldStatus,
	kFieldPin,
	kFieldSubmit,
	kFieldCount
};

HRESULT DupString(const wchar_t* text, PWSTR* out) {
	if (out == nullptr) return E_INVALIDARG;
	const size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
	*out = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
	if (*out == nullptr) return E_OUTOFMEMORY;
	memcpy(*out, text, bytes);
	return S_OK;
}

// UTF-16 counterpart of SecurityFlash::SecureClear (credential UI fields).
void Wipe(std::wstring& s) {
	if (!s.empty()) SecureZeroMemory(s.data(), s.size() * sizeof(wchar_t));
	s.clear();
}

// Diagnostics (no secrets): %ProgramData%\XER\SecurityFlash\hello.log
void HelloLog(const std::string& text) {
	wchar_t dir[MAX_PATH];
	const DWORD n = ExpandEnvironmentStringsW(
	    L"%ProgramData%\\XER\\SecurityFlash", dir, MAX_PATH);
	if (n == 0 || n > MAX_PATH) return;
	const std::wstring path = std::wstring(dir) + L"\\hello.log";
	HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
	                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	char prefix[64];
	std::snprintf(prefix, sizeof(prefix), "[%04u-%02u-%02u %02u:%02u:%02u] ",
	              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
	              st.wSecond);
	const std::string line = prefix + text + "\r\n";
	DWORD written = 0;
	WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
	          nullptr);
	CloseHandle(file);
}

// One request/response round trip with SecurityFlashService.
bool PipeRequest(const std::string& request, std::string* response,
                 DWORD* error_out = nullptr) {
	HANDLE pipe = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 2; ++attempt) {
		pipe = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
		                   OPEN_EXISTING, 0, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) break;
		const DWORD err = GetLastError();
		if (err != ERROR_PIPE_BUSY || !WaitNamedPipeW(kPipeName, 1000)) {
			if (error_out != nullptr) *error_out = err;
			return false;
		}
	}
	if (pipe == INVALID_HANDLE_VALUE) {
		if (error_out != nullptr) *error_out = ERROR_PIPE_BUSY;
		return false;
	}

	DWORD mode = PIPE_READMODE_MESSAGE;
	SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

	DWORD written = 0;
	bool ok = WriteFile(pipe, request.data(),
	                    static_cast<DWORD>(request.size()), &written,
	                    nullptr) &&
	          written == request.size();
	response->clear();
	while (ok) {
		char buf[1024];
		DWORD read = 0;
		const BOOL done = ReadFile(pipe, buf, sizeof(buf), &read, nullptr);
		if (read > 0) response->append(buf, read);
		SecureZeroMemory(buf, sizeof(buf));
		if (done) break;
		if (GetLastError() != ERROR_MORE_DATA) ok = false;
	}
	if (!ok && error_out != nullptr) *error_out = GetLastError();
	CloseHandle(pipe);
	return ok;
}

// Fallback when the service is unreachable: report removable drives that
// carry a token so the tile can say what is wrong.
std::string LocalDriveScan(DWORD pipe_error) {
	std::string out = "LIST\n";
	for (const auto& d : SFS::EnumerateDrives(/*removable_only=*/true)) {
		if (!d.letter || !d.ready || !SFS::TokenExists(d.root_path)) continue;
		out += std::string(1, d.letter) + "\tnoservice\t" +
		       std::to_string(pipe_error) + "\n";
	}
	return out;
}

// Token drives from the service, or the local fallback when it is down.
std::string FetchDriveList(DWORD* pipe_error) {
	std::string list;
	*pipe_error = 0;
	if (!PipeRequest("LIST", &list, pipe_error)) {
		list = LocalDriveScan(*pipe_error);
	}
	return list;
}

// First token drive reported by the service ("LIST\n" + "E\tflash\talice").
struct DriveState {
	char letter = 0;
	bool flash_only = false;
	bool service_down = false;
	std::wstring user;  // or the pipe error code when service_down
};

DriveState ParseFirstDrive(const std::string& list) {
	DriveState state;
	const std::vector<std::string> lines = SplitLines(list);
	if (lines.size() < 2 || lines[0] != "LIST") return state;
	const std::vector<std::string> fields = Split(lines[1], '\t');
	if (fields.size() < 3 || fields[0].size() != 1) return state;
	state.letter = fields[0][0];
	state.flash_only = fields[1] == "flash";
	state.service_down = fields[1] == "noservice";
	state.user = Utf8ToWide(fields[2]);
	return state;
}

HRESULT GetNegotiatePackage(ULONG* package) {
	HANDLE lsa = nullptr;
	NTSTATUS status = LsaConnectUntrusted(&lsa);
	if (status != 0) return HRESULT_FROM_NT(status);
	LSA_STRING name;
	name.Buffer = const_cast<PCHAR>(NEGOSSP_NAME_A);
	name.Length = static_cast<USHORT>(strlen(NEGOSSP_NAME_A));
	name.MaximumLength = static_cast<USHORT>(name.Length + 1);
	status = LsaLookupAuthenticationPackage(lsa, &name, package);
	LsaDeregisterLogonProcess(lsa);
	return status == 0 ? S_OK : HRESULT_FROM_NT(status);
}

// Packs KERB_INTERACTIVE_UNLOCK_LOGON with string buffers stored as
// offsets — the layout LogonUI expects from GetSerialization.
HRESULT PackKerbLogon(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
                      const std::wstring& domain, const std::wstring& user,
                      const std::wstring& password, BYTE** out,
                      DWORD* out_size) {
	const DWORD cb_domain = static_cast<DWORD>(domain.size() * sizeof(wchar_t));
	const DWORD cb_user = static_cast<DWORD>(user.size() * sizeof(wchar_t));
	const DWORD cb_pass = static_cast<DWORD>(password.size() * sizeof(wchar_t));
	const DWORD total = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON) + cb_domain +
	                    cb_user + cb_pass;
	BYTE* buf = static_cast<BYTE*>(CoTaskMemAlloc(total));
	if (buf == nullptr) return E_OUTOFMEMORY;
	ZeroMemory(buf, total);

	auto* kiul = reinterpret_cast<KERB_INTERACTIVE_UNLOCK_LOGON*>(buf);
	KERB_INTERACTIVE_LOGON* kil = &kiul->Logon;
	kil->MessageType = cpus == CPUS_UNLOCK_WORKSTATION
	                       ? KerbWorkstationUnlockLogon
	                       : KerbInteractiveLogon;
	BYTE* cursor = buf + sizeof(KERB_INTERACTIVE_UNLOCK_LOGON);
	auto put = [&](UNICODE_STRING& target, const std::wstring& value,
	               DWORD bytes) {
		target.Length = static_cast<USHORT>(bytes);
		target.MaximumLength = static_cast<USHORT>(bytes);
		target.Buffer = reinterpret_cast<PWSTR>(
		    static_cast<UINT_PTR>(cursor - buf));
		memcpy(cursor, value.data(), bytes);
		cursor += bytes;
	};
	put(kil->LogonDomainName, domain, cb_domain);
	put(kil->UserName, user, cb_user);
	put(kil->Password, password, cb_pass);

	*out = buf;
	*out_size = total;
	return S_OK;
}

class HelloCredential final : public ICredentialProviderCredential {
public:
	explicit HelloCredential(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus)
	    : cpus_(cpus) {
		++g_dll_refs;
	}

	void Update(const DriveState& state) {
		drive_ = state.letter;
		flash_only_ = state.flash_only;
		service_down_ = state.service_down;
		const std::wstring drive_text =
		    drive_ ? std::wstring(1, static_cast<wchar_t>(drive_)) + L": "
		           : std::wstring();
		if (!drive_) {
			status_ = L"Insert your SecurityFlash drive";
		} else if (service_down_) {
			status_ = L"Drive " + drive_text +
			          L"- SecurityFlashService is not reachable (error " +
			          state.user + L")";
		} else if (flash_only_) {
			status_ = L"Drive " + drive_text + L"- flash sign-in";
			if (!state.user.empty()) status_ += L" as " + state.user;
		} else {
			status_ = L"Drive " + drive_text + L"- enter your PIN";
		}
		if (events_ != nullptr) {
			events_->SetFieldString(this, kFieldStatus, status_.c_str());
			events_->SetFieldState(this, kFieldPin, PinState());
		}
	}

	// IUnknown
	IFACEMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
	IFACEMETHODIMP_(ULONG) Release() override {
		const long refs = --ref_;
		if (refs == 0) delete this;
		return refs;
	}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) return E_INVALIDARG;
		if (riid == __uuidof(IUnknown) ||
		    riid == __uuidof(ICredentialProviderCredential)) {
			*ppv = static_cast<ICredentialProviderCredential*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	// ICredentialProviderCredential
	IFACEMETHODIMP Advise(ICredentialProviderCredentialEvents* events) override {
		if (events_ != nullptr) events_->Release();
		events_ = events;
		if (events_ != nullptr) events_->AddRef();
		return S_OK;
	}
	IFACEMETHODIMP UnAdvise() override {
		if (events_ != nullptr) events_->Release();
		events_ = nullptr;
		return S_OK;
	}
	IFACEMETHODIMP SetSelected(BOOL* auto_logon) override {
		*auto_logon = FALSE;
		return S_OK;
	}
	IFACEMETHODIMP SetDeselected() override {
		Wipe(pin_);
		if (events_ != nullptr) events_->SetFieldString(this, kFieldPin, L"");
		return S_OK;
	}
	IFACEMETHODIMP GetFieldState(
	    DWORD field, CREDENTIAL_PROVIDER_FIELD_STATE* state,
	    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* interactive) override {
		if (field >= kFieldCount || state == nullptr || interactive == nullptr) {
			return E_INVALIDARG;
		}
		*interactive = CPFIS_NONE;
		switch (field) {
		case kFieldLabel:
		case kFieldStatus:
			*state = CPFS_DISPLAY_IN_BOTH;
			break;
		case kFieldPin:
			*state = PinState();
			*interactive = CPFIS_FOCUSED;
			break;
		default:
			*state = CPFS_DISPLAY_IN_SELECTED_TILE;
			break;
		}
		return S_OK;
	}
	IFACEMETHODIMP GetStringValue(DWORD field, PWSTR* value) override {
		switch (field) {
		case kFieldLabel: return DupString(kProviderName, value);
		case kFieldStatus: return DupString(status_.c_str(), value);
		case kFieldPin: return DupString(L"", value);
		default: return E_INVALIDARG;
		}
	}
	IFACEMETHODIMP GetBitmapValue(DWORD, HBITMAP*) override { return E_NOTIMPL; }
	IFACEMETHODIMP GetCheckboxValue(DWORD, BOOL*, PWSTR*) override {
		return E_NOTIMPL;
	}
	IFACEMETHODIMP GetSubmitButtonValue(DWORD field, DWORD* adjacent) override {
		if (field != kFieldSubmit || adjacent == nullptr) return E_INVALIDARG;
		*adjacent = kFieldPin;
		return S_OK;
	}
	IFACEMETHODIMP GetComboBoxValueCount(DWORD, DWORD*, DWORD*) override {
		return E_NOTIMPL;
	}
	IFACEMETHODIMP GetComboBoxValueAt(DWORD, DWORD, PWSTR*) override {
		return E_NOTIMPL;
	}
	IFACEMETHODIMP SetStringValue(DWORD field, PCWSTR value) override {
		if (field != kFieldPin) return E_INVALIDARG;
		Wipe(pin_);
		pin_ = value != nullptr ? value : L"";
		return S_OK;
	}
	IFACEMETHODIMP SetCheckboxValue(DWORD, BOOL) override { return E_NOTIMPL; }
	IFACEMETHODIMP SetComboBoxSelectedValue(DWORD, DWORD) override {
		return E_NOTIMPL;
	}
	IFACEMETHODIMP CommandLinkClicked(DWORD) override { return E_NOTIMPL; }

	IFACEMETHODIMP GetSerialization(
	    CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* response,
	    CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* serialization,
	    PWSTR* status_text, CREDENTIAL_PROVIDER_STATUS_ICON* status_icon) override {
		*response = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
		*status_text = nullptr;
		*status_icon = CPSI_NONE;
		ZeroMemory(serialization, sizeof(*serialization));

		if (!drive_) {
			// The tile may be stale (LogonUI stops events while idle).
			DWORD pipe_error = 0;
			Update(ParseFirstDrive(FetchDriveList(&pipe_error)));
		}
		if (!drive_) {
			*status_icon = CPSI_WARNING;
			return DupString(L"Insert your SecurityFlash drive", status_text);
		}
		if (service_down_) {
			*status_icon = CPSI_ERROR;
			return DupString(status_.c_str(), status_text);
		}
		if (!flash_only_ && pin_.empty()) {
			*status_icon = CPSI_WARNING;
			return DupString(L"Enter your PIN", status_text);
		}

		std::string pin_utf8 = flash_only_ ? std::string() : WideToUtf8(pin_);
		std::string request = std::string("LOGON\t") + drive_ + "\t" + pin_utf8;
		SFS::SecureClear(pin_utf8);
		std::string reply;
		const bool sent = PipeRequest(request, &reply);
		SFS::SecureClear(request);
		if (!sent) {
			HelloLog("LOGON: service pipe unreachable");
			*status_icon = CPSI_ERROR;
			return DupString(L"SecurityFlashService is not running",
			                 status_text);
		}
		if (!StartsWith(reply, "OK\t")) {
			const std::wstring message =
			    StartsWith(reply, "ERR\t") ? Utf8ToWide(reply.substr(4))
			                               : L"unexpected service reply";
			SFS::SecureClear(reply);
			HelloLog("LOGON: rejected by service: " + WideToUtf8(message));
			*status_icon = CPSI_ERROR;
			return DupString(message.c_str(), status_text);
		}
		const size_t tab = reply.find('\t', 3);
		if (tab == std::string::npos) {
			SFS::SecureClear(reply);
			*status_icon = CPSI_ERROR;
			return DupString(L"malformed service reply", status_text);
		}
		const std::wstring user = Utf8ToWide(reply.substr(3, tab - 3));
		std::string password_utf8 = reply.substr(tab + 1);
		SFS::SecureClear(reply);
		std::wstring password = Utf8ToWide(password_utf8);
		SFS::SecureClear(password_utf8);

		wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1];
		DWORD computer_len = MAX_COMPUTERNAME_LENGTH + 1;
		const std::wstring domain = GetComputerNameW(computer, &computer_len)
		                                ? std::wstring(computer, computer_len)
		                                : std::wstring(L".");

		ULONG package = 0;
		HRESULT hr = GetNegotiatePackage(&package);
		BYTE* packed = nullptr;
		DWORD packed_size = 0;
		if (SUCCEEDED(hr)) {
			hr = PackKerbLogon(cpus_, domain, user, password, &packed,
			                   &packed_size);
		}
		Wipe(password);
		if (FAILED(hr)) return hr;

		serialization->ulAuthenticationPackage = package;
		serialization->clsidCredentialProvider = kClsid;
		serialization->rgbSerialization = packed;
		serialization->cbSerialization = packed_size;
		*response = CPGSR_RETURN_CREDENTIAL_FINISHED;
		return S_OK;
	}

	IFACEMETHODIMP ReportResult(NTSTATUS status, NTSTATUS, PWSTR* status_text,
	                            CREDENTIAL_PROVIDER_STATUS_ICON* status_icon) override {
		*status_text = nullptr;
		*status_icon = CPSI_NONE;
		Wipe(pin_);
		if (events_ != nullptr) events_->SetFieldString(this, kFieldPin, L"");
		if (status != 0) {
			char code[16];
			std::snprintf(code, sizeof(code), "0x%08lX",
			              static_cast<unsigned long>(status));
			HelloLog(std::string("logon result ") + code);
			*status_icon = CPSI_ERROR;
			return DupString(L"SecurityFlashHello sign-in failed", status_text);
		}
		return S_OK;
	}

private:
	~HelloCredential() {
		Wipe(pin_);
		if (events_ != nullptr) events_->Release();
		--g_dll_refs;
	}

	CREDENTIAL_PROVIDER_FIELD_STATE PinState() const {
		return drive_ && !flash_only_ ? CPFS_DISPLAY_IN_SELECTED_TILE
		                              : CPFS_HIDDEN;
	}

	std::atomic<long> ref_{1};
	CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus_;
	ICredentialProviderCredentialEvents* events_ = nullptr;
	std::wstring status_ = L"Insert your SecurityFlash drive";
	std::wstring pin_;
	char drive_ = 0;
	bool flash_only_ = false;
	bool service_down_ = false;
};

class HelloProvider final : public ICredentialProvider {
public:
	HelloProvider() { ++g_dll_refs; }

	// IUnknown
	IFACEMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
	IFACEMETHODIMP_(ULONG) Release() override {
		const long refs = --ref_;
		if (refs == 0) delete this;
		return refs;
	}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) return E_INVALIDARG;
		if (riid == __uuidof(IUnknown) ||
		    riid == __uuidof(ICredentialProvider)) {
			*ppv = static_cast<ICredentialProvider*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	// ICredentialProvider
	IFACEMETHODIMP SetUsageScenario(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
	                                DWORD) override {
		if (cpus != CPUS_LOGON && cpus != CPUS_UNLOCK_WORKSTATION) {
			return E_NOTIMPL;
		}
		HelloLog(cpus == CPUS_LOGON ? "provider loaded (logon)"
		                            : "provider loaded (unlock)");
		if (credential_ == nullptr) {
			credential_ = new (std::nothrow) HelloCredential(cpus);
		}
		return credential_ != nullptr ? S_OK : E_OUTOFMEMORY;
	}
	IFACEMETHODIMP SetSerialization(
	    const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION*) override {
		return E_NOTIMPL;
	}
	IFACEMETHODIMP Advise(ICredentialProviderEvents* events,
	                      UINT_PTR context) override {
		{
			std::lock_guard lock(mutex_);
			if (events_ != nullptr) events_->Release();
			events_ = events;
			if (events_ != nullptr) events_->AddRef();
			advise_context_ = context;
			stop_ = false;
		}
		HelloLog("Advise (tid " + std::to_string(GetCurrentThreadId()) + ")");
		if (!poller_.joinable()) {
			poller_ = std::thread([this] { PollLoop(); });
		}
		return S_OK;
	}
	IFACEMETHODIMP UnAdvise() override {
		HelloLog("UnAdvise (tid " + std::to_string(GetCurrentThreadId()) +
		         ")");
		// The poller keeps running so the drive list stays current while
		// LogonUI is idle; it only notifies while advised.
		std::lock_guard lock(mutex_);
		if (events_ != nullptr) events_->Release();
		events_ = nullptr;
		return S_OK;
	}
	IFACEMETHODIMP GetFieldDescriptorCount(DWORD* count) override {
		*count = kFieldCount;
		return S_OK;
	}
	IFACEMETHODIMP GetFieldDescriptorAt(
	    DWORD index, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** out) override {
		struct FieldDef {
			CREDENTIAL_PROVIDER_FIELD_TYPE type;
			const wchar_t* label;
		};
		static const FieldDef kFields[kFieldCount] = {
		    {CPFT_LARGE_TEXT, kProviderName},
		    {CPFT_SMALL_TEXT, L"Status"},
		    {CPFT_PASSWORD_TEXT, L"PIN"},
		    {CPFT_SUBMIT_BUTTON, L"Submit"},
		};
		if (out == nullptr || index >= kFieldCount) return E_INVALIDARG;
		auto* desc = static_cast<CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*>(
		    CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR)));
		if (desc == nullptr) return E_OUTOFMEMORY;
		ZeroMemory(desc, sizeof(*desc));
		desc->dwFieldID = index;
		desc->cpft = kFields[index].type;
		const HRESULT hr = DupString(kFields[index].label, &desc->pszLabel);
		if (FAILED(hr)) {
			CoTaskMemFree(desc);
			return hr;
		}
		*out = desc;
		return S_OK;
	}
	IFACEMETHODIMP GetCredentialCount(DWORD* count, DWORD* default_index,
	                                  BOOL* auto_logon) override {
		DWORD pipe_error = 0;
		const std::string list = FetchDriveList(&pipe_error);
		{
			std::lock_guard lock(mutex_);
			drive_list_ = list;
			reported_list_ = list;
		}
		const DriveState state = ParseFirstDrive(list);
		if (credential_ != nullptr) credential_->Update(state);
		HelloLog(std::string("GetCredentialCount: drive ") +
		         (state.letter ? std::string(1, state.letter) : "-"));

		*count = credential_ != nullptr ? 1 : 0;
		*default_index = CREDENTIAL_PROVIDER_NO_DEFAULT;
		*auto_logon = FALSE;
		// Flash-only signs in once per insertion, so a failed attempt
		// does not loop until the drive is re-inserted.
		if (!state.letter) {
			autologon_drive_ = 0;
		} else if (state.flash_only && autologon_drive_ != state.letter) {
			autologon_drive_ = state.letter;
			*default_index = 0;
			*auto_logon = TRUE;
		}
		return S_OK;
	}
	IFACEMETHODIMP GetCredentialAt(DWORD index,
	                               ICredentialProviderCredential** out) override {
		if (index != 0 || out == nullptr || credential_ == nullptr) {
			return E_INVALIDARG;
		}
		return credential_->QueryInterface(
		    __uuidof(ICredentialProviderCredential),
		    reinterpret_cast<void**>(out));
	}

private:
	~HelloProvider() {
		StopPoller();
		if (events_ != nullptr) events_->Release();
		if (credential_ != nullptr) credential_->Release();
		--g_dll_refs;
	}

	void StopPoller() {
		{
			std::lock_guard lock(mutex_);
			stop_ = true;
		}
		cv_.notify_all();
		if (!poller_.joinable()) return;
		if (poller_.get_id() == std::this_thread::get_id()) {
			poller_.detach();  // UnAdvise re-entered from CredentialsChanged
		} else {
			poller_.join();
		}
	}

	// Polls the service for token drives; a change re-enumerates the tile.
	void PollLoop() {
		HelloLog("poller started (tid " +
		         std::to_string(GetCurrentThreadId()) + ")");
		struct ExitLog {
			~ExitLog() { HelloLog("poller stopped"); }
		} exit_log;
		std::unique_lock lock(mutex_);
		while (!stop_) {
			lock.unlock();
			std::string list;
			DWORD pipe_error = 0;
			if (!PipeRequest("LIST", &list, &pipe_error)) {
				list = LocalDriveScan(pipe_error);
				if (pipe_error != last_pipe_error_) {
					HelloLog("service pipe unreachable, error " +
					         std::to_string(pipe_error));
				}
			}
			last_pipe_error_ = pipe_error;
			lock.lock();
			if (stop_) break;
			if (list != drive_list_) {
				const std::string shown =
				    ReplaceAll(ReplaceAll(list, "\t", " "), "\n", ";");
				HelloLog("drives: " + (shown.empty() ? "(none)" : shown));
				drive_list_ = list;
			}
			if (drive_list_ != reported_list_) {
				ICredentialProviderEvents* events = events_;
				const UINT_PTR context = advise_context_;
				if (events != nullptr) {
					reported_list_ = drive_list_;
					events->AddRef();
					lock.unlock();
					const HRESULT hr = events->CredentialsChanged(context);
					char code[16];
					std::snprintf(code, sizeof(code), "0x%08lX",
					              static_cast<unsigned long>(hr));
					HelloLog(std::string("CredentialsChanged -> ") + code);
					events->Release();
					lock.lock();
				}
			}
			cv_.wait_for(lock, std::chrono::seconds(2), [this] { return stop_; });
		}
	}

	std::atomic<long> ref_{1};
	HelloCredential* credential_ = nullptr;
	ICredentialProviderEvents* events_ = nullptr;
	UINT_PTR advise_context_ = 0;
	std::thread poller_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool stop_ = false;
	std::string drive_list_;
	std::string reported_list_;  // list LogonUI last enumerated
	char autologon_drive_ = 0;
	DWORD last_pipe_error_ = 0;
};

class HelloClassFactory final : public IClassFactory {
public:
	HelloClassFactory() { ++g_dll_refs; }

	IFACEMETHODIMP_(ULONG) AddRef() override { return ++ref_; }
	IFACEMETHODIMP_(ULONG) Release() override {
		const long refs = --ref_;
		if (refs == 0) delete this;
		return refs;
	}
	IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (ppv == nullptr) return E_INVALIDARG;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
			*ppv = static_cast<IClassFactory*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
	                              void** ppv) override {
		if (ppv == nullptr) return E_INVALIDARG;
		*ppv = nullptr;
		if (outer != nullptr) return CLASS_E_NOAGGREGATION;
		auto* provider = new (std::nothrow) HelloProvider();
		if (provider == nullptr) return E_OUTOFMEMORY;
		const HRESULT hr = provider->QueryInterface(riid, ppv);
		provider->Release();
		return hr;
	}
	IFACEMETHODIMP LockServer(BOOL lock) override {
		if (lock) {
			++g_dll_refs;
		} else {
			--g_dll_refs;
		}
		return S_OK;
	}

private:
	~HelloClassFactory() { --g_dll_refs; }

	std::atomic<long> ref_{1};
};

HRESULT SetRegString(HKEY root, const std::wstring& key,
                     const wchar_t* value_name, const std::wstring& data) {
	const LSTATUS status = RegSetKeyValueW(
	    root, key.c_str(), value_name, REG_SZ, data.c_str(),
	    static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
	return HRESULT_FROM_WIN32(status);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

STDAPI DllCanUnloadNow() {
	return g_dll_refs.load() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv) {
	if (ppv == nullptr) return E_INVALIDARG;
	*ppv = nullptr;
	if (!IsEqualCLSID(clsid, kClsid)) return CLASS_E_CLASSNOTAVAILABLE;
	auto* factory = new (std::nothrow) HelloClassFactory();
	if (factory == nullptr) return E_OUTOFMEMORY;
	const HRESULT hr = factory->QueryInterface(riid, ppv);
	factory->Release();
	return hr;
}

STDAPI DllRegisterServer() {
	wchar_t path[MAX_PATH];
	const DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return SELFREG_E_CLASS;
	const std::wstring clsid_key = std::wstring(L"CLSID\\") + kClsidString;
	HRESULT hr = SetRegString(HKEY_CLASSES_ROOT, clsid_key, nullptr,
	                          kProviderName);
	if (SUCCEEDED(hr)) {
		hr = SetRegString(HKEY_CLASSES_ROOT, clsid_key + L"\\InprocServer32",
		                  nullptr, std::wstring(path, n));
	}
	if (SUCCEEDED(hr)) {
		hr = SetRegString(HKEY_CLASSES_ROOT, clsid_key + L"\\InprocServer32",
		                  L"ThreadingModel", L"Apartment");
	}
	if (SUCCEEDED(hr)) {
		hr = SetRegString(HKEY_LOCAL_MACHINE,
		                  std::wstring(kProvidersKey) + kClsidString, nullptr,
		                  kProviderName);
	}
	return hr;
}

STDAPI DllUnregisterServer() {
	RegDeleteTreeW(HKEY_LOCAL_MACHINE,
	               (std::wstring(kProvidersKey) + kClsidString).c_str());
	RegDeleteTreeW(HKEY_CLASSES_ROOT,
	               (std::wstring(L"CLSID\\") + kClsidString).c_str());
	return S_OK;
}
