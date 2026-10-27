/**
 * This file is part of XER, Process subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#pragma once

#include <string>
#include <vector>

namespace content::process {

/**
 * ParsedCommand represents a parsed command with executable and arguments.
 */
struct ParsedCommand {
  std::string executable;
  std::vector<std::string> arguments;
  std::string working_directory;
  bool background;  // Run in background
  
  ParsedCommand() : background(false) {}
};

/**
 * ProcessCommandParser parses command strings into structured components.
 * Handles shell-like quoting, escaping, and environment variables.
 */
class ProcessCommandParser {
public:
  /**
   * Parse a command string into executable and arguments.
   * @param command Command string (e.g., "ls -la /tmp")
   * @return Parsed command structure
   */
  static ParsedCommand Parse(const std::string& command);
  
  /**
   * Parse with environment variable expansion.
   * @param command Command string
   * @param env Environment variables (key=value pairs)
   * @return Parsed command with expanded variables
   */
  static ParsedCommand ParseWithEnv(const std::string& command,
                                    const std::vector<std::string>& env);
  
  /**
   * Parse command with custom working directory.
   * @param command Command string
   * @param working_directory Working directory
   * @return Parsed command
   */
  static ParsedCommand ParseWithDirectory(const std::string& command,
                                           const std::string& working_directory);
  
  /**
   * Join arguments back into a command string.
   * @param executable Executable path
   * @param arguments Arguments list
   * @return Command string
   */
  static std::string Join(const std::string& executable,
                          const std::vector<std::string>& arguments);
  
  /**
   * Escape an argument for shell-safe quoting.
   * @param argument Argument to escape
   * @return Escaped argument
   */
  static std::string EscapeArgument(const std::string& argument);
  
  /**
   * Expand environment variables in a string.
   * @param str String with variables (e.g., "$HOME/file")
   * @return String with expanded variables
   */
  static std::string ExpandEnvironmentVariables(const std::string& str);
  
  /**
   * Split a command into tokens, respecting quotes.
   * @param command Command string
   * @return Vector of tokens
   */
  static std::vector<std::string> Tokenize(const std::string& command);
  
  /**
   * Validate a command string for safety.
   * @param command Command to validate
   * @return true if command is safe to execute
   */
  static bool ValidateCommand(const std::string& command);
  
  /**
   * Check if command contains shell metacharacters.
   * @param command Command string
   * @return true if contains metacharacters
   */
  static bool HasShellMetacharacters(const std::string& command);
  
  /**
   * Extract executable path from PATH environment variable.
   * @param command Command name (e.g., "ls")
   * @return Full path to executable, or empty if not found
   */
  static std::string FindExecutableInPath(const std::string& command);

private:
  static std::string ExpandVariable(const std::string& name);
  static bool IsQuote(char c);
  static bool IsEscape(char c);
  static bool IsWhitespace(char c);
};

}  // namespace content::process
