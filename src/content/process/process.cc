/**
 * This file is part of XER, Process subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#include "content/process/process.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#endif

namespace content::process {

Process::Process()
    : pid_(0),
      exit_callback_(nullptr),
      output_callback_(nullptr),
      thread_pool_(&AsyncIO::IO::IOThreadPool::GetSharedInstance()) {
#ifdef _WIN32
  process_handle_ = nullptr;
  stdin_write_ = nullptr;
  stdout_read_ = nullptr;
  stderr_read_ = nullptr;
#else
  stdin_fd_ = -1;
  stdout_fd_ = -1;
  stderr_fd_ = -1;
#endif
}

Process::~Process() {
  if (IsRunning()) {
    Terminate();
  }
  Cleanup();
}

bool Process::Start(const std::string& executable,
                    const std::vector<std::string>& arguments,
                    const std::string& working_directory) {
#ifdef _WIN32
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;
  
  // Create pipes for stdin, stdout, stderr
  HANDLE stdin_read, stdout_write, stderr_write;
  
  if (!CreatePipe(&stdin_read, &stdin_write_, &sa, 0) ||
      !CreatePipe(&stdout_read_, &stdout_write, &sa, 0) ||
      !CreatePipe(&stderr_read_, &stderr_write, &sa, 0)) {
    Cleanup();
    return false;
  }
  
  // Ensure parent doesn't inherit the wrong ends
  SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read_, HANDLE_FLAG_INHERIT, 0);
  
  // Build command line
  std::string cmd_line = executable;
  for (const auto& arg : arguments) {
    cmd_line += " ";
    if (arg.find(' ') != std::string::npos) {
      cmd_line += "\"" + arg + "\"";
    } else {
      cmd_line += arg;
    }
  }
  
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.hStdInput = stdin_read;
  si.hStdOutput = stdout_write;
  si.hStdError = stderr_write;
  si.dwFlags |= STARTF_USESTDHANDLES;
  
  ZeroMemory(&pi, sizeof(pi));
  
  LPSTR cmd_line_mut = _strdup(cmd_line.c_str());
  LPSTR working_dir_mut = working_directory.empty() ? nullptr : _strdup(working_directory.c_str());
  
  BOOL success = CreateProcessA(
      nullptr,
      cmd_line_mut,
      nullptr,
      nullptr,
      TRUE,
      CREATE_NO_WINDOW,
      nullptr,
      working_dir_mut,
      &si,
      &pi);
  
  free(cmd_line_mut);
  if (working_dir_mut) free(working_dir_mut);
  
  CloseHandle(stdin_read);
  CloseHandle(stdout_write);
  CloseHandle(stderr_write);
  
  if (!success) {
    Cleanup();
    return false;
  }
  
  CloseHandle(pi.hThread);
  process_handle_ = pi.hProcess;
  pid_ = pi.dwProcessId;
  
  return true;
  
#else
  // Unix/Linux/macOS implementation
  int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
  
  if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
    return false;
  }
  
  pid_t child_pid = fork();
  if (child_pid == -1) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return false;
  }
  
  if (child_pid == 0) {
    // Child process
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    if (!working_directory.empty()) {
      chdir(working_directory.c_str());
    }
    
    // Build argv
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : arguments) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    
    execvp(executable.c_str(), argv.data());
    _exit(127);
  }
  
  // Parent process
  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  
  stdin_fd_ = stdin_pipe[1];
  stdout_fd_ = stdout_pipe[0];
  stderr_fd_ = stderr_pipe[0];
  pid_ = child_pid;
  
  return true;
#endif
}

bool Process::Start(const std::string& command) {
  // Parse command into executable and arguments
  std::istringstream iss(command);
  std::string executable;
  std::vector<std::string> arguments;
  
  if (iss >> executable) {
    std::string arg;
    while (iss >> arg) {
      arguments.push_back(arg);
    }
  }
  
  return Start(executable, arguments);
}

bool Process::Wait(int timeout_ms) {
#ifdef _WIN32
  DWORD timeout = (timeout_ms == 0) ? INFINITE : timeout_ms;
  DWORD result = WaitForSingleObject(process_handle_, timeout);
  
  if (result == WAIT_OBJECT_0) {
    DWORD exit_code;
    if (GetExitCodeProcess(process_handle_, &exit_code)) {
      exit_status_.exit_code = exit_code;
      exit_status_.signaled = false;
    }
    NotifyExit();
    return true;
  }
  return false;
  
#else
  if (timeout_ms == 0) {
    int status;
    pid_t result = waitpid(pid_, &status, 0);
    
    if (result == pid_) {
      if (WIFEXITED(status)) {
        exit_status_.exit_code = WEXITSTATUS(status);
        exit_status_.signaled = false;
      } else if (WIFSIGNALED(status)) {
        exit_status_.exit_code = -1;
        exit_status_.signaled = true;
        exit_status_.signal = WTERMSIG(status);
      }
      NotifyExit();
      return true;
    }
  } else {
    // Non-blocking wait with timeout
    // Simplified: use sleep intervals
    int elapsed = 0;
    int interval = 10;  // Check every 10ms
    
    while (elapsed < timeout_ms) {
      int status;
      pid_t result = waitpid(pid_, &status, WNOHANG);
      
      if (result == pid_) {
        if (WIFEXITED(status)) {
          exit_status_.exit_code = WEXITSTATUS(status);
          exit_status_.signaled = false;
        } else if (WIFSIGNALED(status)) {
          exit_status_.exit_code = -1;
          exit_status_.signaled = true;
          exit_status_.signal = WTERMSIG(status);
        }
        NotifyExit();
        return true;
      } else if (result == -1) {
        return false;
      }
      
      usleep(interval * 1000);
      elapsed += interval;
    }
  }
  return false;
#endif
}

bool Process::Terminate() {
  if (!IsRunning()) {
    return false;
  }
  
#ifdef _WIN32
  if (TerminateProcess(process_handle_, 0)) {
    Wait(1000);  // Wait up to 1 second for cleanup
    return true;
  }
  return false;
  
#else
  if (kill(pid_, SIGTERM) == 0) {
    Wait(1000);
    return true;
  }
  return false;
#endif
}

bool Process::Kill() {
  if (!IsRunning()) {
    return false;
  }
  
#ifdef _WIN32
  if (TerminateProcess(process_handle_, 1)) {
    Wait(100);
    return true;
  }
  return false;
  
#else
  if (kill(pid_, SIGKILL) == 0) {
    Wait(100);
    return true;
  }
  return false;
#endif
}

bool Process::IsRunning() const {
#ifdef _WIN32
  if (process_handle_ == nullptr) {
    return false;
  }
  
  DWORD exit_code;
  if (GetExitCodeProcess(process_handle_, &exit_code)) {
    return exit_code == STILL_ACTIVE;
  }
  return false;
  
#else
  if (pid_ == 0) {
    return false;
  }
  
  // Send signal 0 to check if process exists
  return kill(pid_, 0) == 0;
#endif
}

int Process::WriteStdin(const std::string& data) {
  if (!IsRunning()) {
    return -1;
  }
  
#ifdef _WIN32
  DWORD bytes_written;
  if (WriteFile(stdin_write_, data.c_str(), data.size(), &bytes_written, nullptr)) {
    return bytes_written;
  }
  return -1;
  
#else
  if (stdin_fd_ == -1) {
    return -1;
  }
  
  ssize_t result = write(stdin_fd_, data.c_str(), data.size());
  return result;
#endif
}

void Process::CloseStdin() {
#ifdef _WIN32
  if (stdin_write_) {
    CloseHandle(stdin_write_);
    stdin_write_ = nullptr;
  }
#else
  if (stdin_fd_ != -1) {
    close(stdin_fd_);
    stdin_fd_ = -1;
  }
#endif
}

int Process::ReadStdout(std::string* data, size_t max_bytes) {
  if (!IsRunning() || data == nullptr) {
    return -1;
  }
  
  data->resize(max_bytes);
  
#ifdef _WIN32
  DWORD bytes_read;
  if (ReadFile(stdout_read_, &(*data)[0], max_bytes, &bytes_read, nullptr)) {
    data->resize(bytes_read);
    return bytes_read;
  }
  return -1;
  
#else
  if (stdout_fd_ == -1) {
    return -1;
  }
  
  ssize_t result = read(stdout_fd_, &(*data)[0], max_bytes);
  if (result > 0) {
    data->resize(result);
    return result;
  }
  data->clear();
  return result;
#endif
}

int Process::ReadStderr(std::string* data, size_t max_bytes) {
  if (!IsRunning() || data == nullptr) {
    return -1;
  }
  
  data->resize(max_bytes);
  
#ifdef _WIN32
  DWORD bytes_read;
  if (ReadFile(stderr_read_, &(*data)[0], max_bytes, &bytes_read, nullptr)) {
    data->resize(bytes_read);
    return bytes_read;
  }
  return -1;
  
#else
  if (stderr_fd_ == -1) {
    return -1;
  }
  
  ssize_t result = read(stderr_fd_, &(*data)[0], max_bytes);
  if (result > 0) {
    data->resize(result);
    return result;
  }
  data->clear();
  return result;
#endif
}

void Process::SetExitCallback(ProcessExitCallback callback) {
  exit_callback_ = callback;
}

void Process::SetOutputCallback(ProcessOutputCallback callback) {
  output_callback_ = callback;
}

ProcessInfo Process::GetInfo() const {
  ProcessInfo info;
  info.pid = pid_;
  info.is_running = IsRunning();
  
#ifdef _WIN32
  if (process_handle_) {
    char path[MAX_PATH];
    if (GetModuleFileNameExA(process_handle_, nullptr, path, MAX_PATH)) {
      info.executable_path = path;
    }
  }
#else
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/exe", pid_);
  ssize_t len = readlink(path, path, sizeof(path) - 1);
  if (len > 0) {
    path[len] = '\0';
    info.executable_path = path;
  }
#endif
  
  return info;
}

ProcessOutput Process::Execute(const std::string& command, int timeout_ms) {
  Process process;
  ProcessOutput output;
  
  if (!process.Start(command)) {
    output.exit_code = -1;
    return output;
  }
  
  process.CloseStdin();
  
  if (timeout_ms == 0) {
    process.Wait();
  } else {
    process.Wait(timeout_ms);
  }
  
  process.ReadStdout(&output.stdout_data, 1024 * 1024);  // Read up to 1MB
  process.ReadStderr(&output.stderr_data, 1024 * 1024);
  output.exit_code = process.GetExitStatus().exit_code;
  
  return output;
}

ProcessOutput Process::Execute(const std::string& executable,
                               const std::vector<std::string>& arguments,
                               int timeout_ms) {
  Process process;
  ProcessOutput output;
  
  if (!process.Start(executable, arguments)) {
    output.exit_code = -1;
    return output;
  }
  
  process.CloseStdin();
  
  if (timeout_ms == 0) {
    process.Wait();
  } else {
    process.Wait(timeout_ms);
  }
  
  process.ReadStdout(&output.stdout_data, 1024 * 1024);
  process.ReadStderr(&output.stderr_data, 1024 * 1024);
  output.exit_code = process.GetExitStatus().exit_code;
  
  return output;
}

bool Process::ProcessExists(pid_t pid) {
#ifdef _WIN32
  HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (handle) {
    DWORD exit_code;
    bool exists = GetExitCodeProcess(handle, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(handle);
    return exists;
  }
  return false;
  
#else
  return kill(pid, 0) == 0;
#endif
}

std::vector<ProcessInfo> Process::ListProcesses() {
  std::vector<ProcessInfo> processes;
  
#ifdef _WIN32
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return processes;
  }
  
  PROCESSENTRY32 pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32);
  
  if (Process32First(snapshot, &pe32)) {
    do {
      ProcessInfo info;
      info.pid = pe32.th32ProcessID;
      info.executable_path = pe32.szExeFile;
      info.is_running = true;
      processes.push_back(info);
    } while (Process32Next(snapshot, &pe32));
  }
  
  CloseHandle(snapshot);
  
#else
  DIR* proc_dir = opendir("/proc");
  if (!proc_dir) {
    return processes;
  }
  
  struct dirent* entry;
  while ((entry = readdir(proc_dir)) != nullptr) {
    if (isdigit(entry->d_name[0])) {
      pid_t pid = atoi(entry->d_name);
      
      ProcessInfo info;
      info.pid = pid;
      info.is_running = true;
      
      char path[PATH_MAX];
      snprintf(path, sizeof(path), "/proc/%d/comm", pid);
      
      FILE* comm_file = fopen(path, "r");
      if (comm_file) {
        char comm[256];
        if (fgets(comm, sizeof(comm), comm_file)) {
          comm[strcspn(comm, "\n")] = '\0';
          info.executable_path = comm;
        }
        fclose(comm_file);
      }
      
      processes.push_back(info);
    }
  }
  
  closedir(proc_dir);
#endif
  
  return processes;
}

std::vector<ProcessInfo> Process::FindProcessByName(const std::string& name) {
  auto all_processes = ListProcesses();
  std::vector<ProcessInfo> matching;
  
  for (const auto& proc : all_processes) {
    if (proc.executable_path.find(name) != std::string::npos) {
      matching.push_back(proc);
    }
  }
  
  return matching;
}

pid_t Process::GetCurrentPid() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

void Process::StartAsync(const std::string& executable,
                         const std::vector<std::string>& arguments,
                         std::function<void(bool)> callback) {
  if (thread_pool_) {
    thread_pool_->Enqueue([this, executable, arguments, callback]() {
      bool success = Start(executable, arguments);
      callback(success);
    });
  } else {
    callback(Start(executable, arguments));
  }
}

void Process::ExecuteAsync(const std::string& command,
                           std::function<void(const ProcessOutput&)> callback) {
  if (thread_pool_) {
    thread_pool_->Enqueue([command, callback]() {
      ProcessOutput output = Execute(command);
      callback(output);
    });
  } else {
    callback(Execute(command));
  }
}

void Process::Cleanup() {
#ifdef _WIN32
  if (process_handle_) {
    CloseHandle(process_handle_);
    process_handle_ = nullptr;
  }
  if (stdin_write_) {
    CloseHandle(stdin_write_);
    stdin_write_ = nullptr;
  }
  if (stdout_read_) {
    CloseHandle(stdout_read_);
    stdout_read_ = nullptr;
  }
  if (stderr_read_) {
    CloseHandle(stderr_read_);
    stderr_read_ = nullptr;
  }
#else
  if (stdin_fd_ != -1) {
    close(stdin_fd_);
    stdin_fd_ = -1;
  }
  if (stdout_fd_ != -1) {
    close(stdout_fd_);
    stdout_fd_ = -1;
  }
  if (stderr_fd_ != -1) {
    close(stderr_fd_);
    stderr_fd_ = -1;
  }
#endif
}

void Process::NotifyExit() {
  if (exit_callback_) {
    exit_callback_(exit_status_);
  }
}

void Process::NotifyOutput(const std::string& data, bool is_stderr) {
  if (output_callback_) {
    output_callback_(data, is_stderr);
  }
}

// ProcessManager implementation

ProcessManager& ProcessManager::Instance() {
  static ProcessManager instance;
  return instance;
}

std::shared_ptr<Process> ProcessManager::CreateProcess(
    const std::string& executable,
    const std::vector<std::string>& arguments,
    const std::string& working_directory) {
  auto process = std::make_shared<Process>();
  
  if (process->Start(executable, arguments, working_directory)) {
    processes_.push_back(process);
    return process;
  }
  
  return nullptr;
}

std::vector<std::shared_ptr<Process>> ProcessManager::GetAllProcesses() {
  return processes_;
}

std::shared_ptr<Process> ProcessManager::GetProcess(pid_t pid) {
  for (auto& proc : processes_) {
    if (proc->GetPid() == pid) {
      return proc;
    }
  }
  return nullptr;
}

void ProcessManager::TerminateAll() {
  for (auto& proc : processes_) {
    if (proc->IsRunning()) {
      proc->Terminate();
    }
  }
}

void ProcessManager::CleanupDeadProcesses() {
  auto it = std::remove_if(processes_.begin(), processes_.end(),
                          [](const std::shared_ptr<Process>& proc) {
                            return !proc->IsRunning();
                          });
  processes_.erase(it, processes_.end());
}

}  // namespace content::process
