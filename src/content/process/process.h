/**
 * This file is part of XER, Process subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "async_io/io_thread_pool.h"

namespace content::process {

/**
 * ProcessExitStatus represents the exit status of a process.
 */
struct ProcessExitStatus {
  int exit_code;
  bool signaled;  // True if process was terminated by signal
  int signal;     // Signal number if signaled
  
  ProcessExitStatus() : exit_code(0), signaled(false), signal(0) {}
};

/**
 * ProcessInfo contains information about a running process.
 */
struct ProcessInfo {
  pid_t pid;
  std::string executable_path;
  std::string working_directory;
  std::vector<std::string> arguments;
  std::vector<std::string> environment;
  bool is_running;
  
  ProcessInfo() : pid(0), is_running(false) {}
};

/**
 * ProcessOutput captures stdout/stderr from a process.
 */
struct ProcessOutput {
  std::string stdout_data;
  std::string stderr_data;
  int exit_code;
  
  ProcessOutput() : exit_code(0) {}
};

/**
 * ProcessObserver is a callback for process events.
 */
using ProcessObserver = std::function<void(const ProcessInfo&)>;

/**
 * ProcessExitCallback is called when a process exits.
 */
using ProcessExitCallback = std::function<void(const ProcessExitStatus&)>;

/**
 * ProcessOutputCallback is called when process output is available.
 */
using ProcessOutputCallback = std::function<void(const std::string& data, bool is_stderr)>;

/**
 * Process provides cross-platform process management.
 * Integrates with XER's async infrastructure for non-blocking operations.
 */
class Process {
public:
  Process();
  ~Process();
  
  // Prevent copying
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  
  /**
   * Start a process with the given command and arguments.
   * @param executable Path to the executable
   * @param arguments Command-line arguments
   * @param working_directory Working directory (empty for current)
   * @return true on success
   */
  bool Start(const std::string& executable,
             const std::vector<std::string>& arguments = {},
             const std::string& working_directory = "");
  
  /**
   * Start a process with a command string.
   * @param command Command string (e.g., "ls -la")
   * @return true on success
   */
  bool Start(const std::string& command);
  
  /**
   * Wait for the process to complete.
   * @param timeout_ms Timeout in milliseconds (0 = infinite)
   * @return true if process exited, false if timeout
   */
  bool Wait(int timeout_ms = 0);
  
  /**
   * Terminate the process gracefully.
   * @return true on success
   */
  bool Terminate();
  
  /**
   * Kill the process forcefully.
   * @return true on success
   */
  bool Kill();
  
  /**
   * Check if the process is still running.
   */
  bool IsRunning() const;
  
  /**
   * Get the process ID.
   */
  pid_t GetPid() const { return pid_; }
  
  /**
   * Get the exit status (only valid after process exits).
   */
  ProcessExitStatus GetExitStatus() const { return exit_status_; }
  
  /**
   * Write to process stdin.
   * @param data Data to write
   * @return Number of bytes written, or -1 on error
   */
  int WriteStdin(const std::string& data);
  
  /**
   * Close stdin (signals EOF to process).
   */
  void CloseStdin();
  
  /**
   * Read from process stdout (non-blocking).
   * @param data Output buffer
   * @param max_bytes Maximum bytes to read
   * @return Number of bytes read, or -1 on error
   */
  int ReadStdout(std::string* data, size_t max_bytes = 4096);
  
  /**
   * Read from process stderr (non-blocking).
   * @param data Output buffer
   * @param max_bytes Maximum bytes to read
   * @return Number of bytes read, or -1 on error
   */
  int ReadStderr(std::string* data, size_t max_bytes = 4096);
  
  /**
   * Set callback for process exit.
   */
  void SetExitCallback(ProcessExitCallback callback);
  
  /**
   * Set callback for process output.
   */
  void SetOutputCallback(ProcessOutputCallback callback);
  
  /**
   * Get process information.
   */
  ProcessInfo GetInfo() const;
  
  /**
   * Static: Execute a command synchronously and capture output.
   * @param command Command to execute
   * @param timeout_ms Timeout (0 = infinite)
   * @return Process output with exit code
   */
  static ProcessOutput Execute(const std::string& command, int timeout_ms = 0);
  
  /**
   * Static: Execute with arguments synchronously.
   */
  static ProcessOutput Execute(const std::string& executable,
                               const std::vector<std::string>& arguments,
                               int timeout_ms = 0);
  
  /**
   * Static: Check if a process with given PID exists.
   */
  static bool ProcessExists(pid_t pid);
  
  /**
   * Static: Get list of all running processes.
   */
  static std::vector<ProcessInfo> ListProcesses();
  
  /**
   * Static: Find process by name.
   */
  static std::vector<ProcessInfo> FindProcessByName(const std::string& name);
  
  /**
   * Static: Get current process ID.
   */
  static pid_t GetCurrentPid();
  
  /**
   * Async operations using thread pool.
   */
  void StartAsync(const std::string& executable,
                  const std::vector<std::string>& arguments,
                  std::function<void(bool)> callback);
  
  void ExecuteAsync(const std::string& command,
                    std::function<void(const ProcessOutput&)> callback);

private:
  void Cleanup();
  void NotifyExit();
  void NotifyOutput(const std::string& data, bool is_stderr);
  
  pid_t pid_;
  ProcessExitStatus exit_status_;
  ProcessExitCallback exit_callback_;
  ProcessOutputCallback output_callback_;
  
#ifdef _WIN32
  HANDLE process_handle_;
  HANDLE stdin_write_;
  HANDLE stdout_read_;
  HANDLE stderr_read_;
#else
  int stdin_fd_;
  int stdout_fd_;
  int stderr_fd_;
#endif
  
  AsyncIO::IO::IOThreadPool* thread_pool_;
};

/**
 * ProcessManager provides high-level process pool management.
 */
class ProcessManager {
public:
  static ProcessManager& Instance();
  
  // Prevent copying
  ProcessManager(const ProcessManager&) = delete;
  ProcessManager& operator=(const ProcessManager&) = delete;
  
  /**
   * Create and start a new process.
   * @return Process pointer (nullptr on failure)
   */
  std::shared_ptr<Process> CreateProcess(const std::string& executable,
                                         const std::vector<std::string>& arguments = {},
                                         const std::string& working_directory = "");
  
  /**
   * Get all managed processes.
   */
  std::vector<std::shared_ptr<Process>> GetAllProcesses();
  
  /**
   * Get process by PID.
   */
  std::shared_ptr<Process> GetProcess(pid_t pid);
  
  /**
   * Terminate all managed processes.
   */
  void TerminateAll();
  
  /**
   * Remove completed processes from the pool.
   */
  void CleanupDeadProcesses();
  
  /**
   * Get process count.
   */
  size_t GetProcessCount() const { return processes_.size(); }

private:
  ProcessManager() = default;
  ~ProcessManager() = default;
  
  std::vector<std::shared_ptr<Process>> processes_;
  AsyncIO::IO::IOThreadPool* thread_pool_;
};

}  // namespace content::process
