/**
 * This file is part of XER, Process subsystem.
 * Copyright (C) 2026 XER Runtime Team
 */
#include "content/process/process_command_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace content::process {

ParsedCommand ProcessCommandParser::Parse(const std::string& command) {
  ParsedCommand parsed;
  auto tokens = Tokenize(command);
  
  if (tokens.empty()) {
    return parsed;
  }
  
  parsed.executable = tokens[0];
  parsed.arguments.assign(tokens.begin() + 1, tokens.end());
  
  return parsed;
}

ParsedCommand ProcessCommandParser::ParseWithEnv(
    const std::string& command,
    const std::vector<std::string>& env) {
  // Set environment variables temporarily
  std::vector<std::string> old_env;
  
  for (const auto& e : env) {
    size_t eq_pos = e.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = e.substr(0, eq_pos);
      std::string value = e.substr(eq_pos + 1);
      
#ifdef _WIN32
      char* old_value = nullptr;
      size_t size = 0;
      _dupenv_s(&old_value, &size, key.c_str());
      if (old_value) {
        old_env.push_back(key + "=" + old_value);
        free(old_value);
      }
      SetEnvironmentVariableA(key.c_str(), value.c_str());
#else
      char* old_value = getenv(key.c_str());
      if (old_value) {
        old_env.push_back(key + "=" + old_value);
      }
      setenv(key.c_str(), value.c_str(), 1);
#endif
    }
  }
  
  ParsedCommand parsed = Parse(ExpandEnvironmentVariables(command));
  
  // Restore old environment
  for (const auto& e : old_env) {
    size_t eq_pos = e.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = e.substr(0, eq_pos);
      std::string value = e.substr(eq_pos + 1);
      
#ifdef _WIN32
      SetEnvironmentVariableA(key.c_str(), value.c_str());
#else
      setenv(key.c_str(), value.c_str(), 1);
#endif
    }
  }
  
  return parsed;
}

ParsedCommand ProcessCommandParser::ParseWithDirectory(
    const std::string& command,
    const std::string& working_directory) {
  ParsedCommand parsed = Parse(command);
  parsed.working_directory = working_directory;
  return parsed;
}

std::string ProcessCommandParser::Join(const std::string& executable,
                                        const std::vector<std::string>& arguments) {
  std::string result = EscapeArgument(executable);
  
  for (const auto& arg : arguments) {
    result += " " + EscapeArgument(arg);
  }
  
  return result;
}

std::string ProcessCommandParser::EscapeArgument(const std::string& argument) {
  // If argument contains spaces or special characters, quote it
  if (argument.find(' ') != std::string::npos ||
      argument.find('\t') != std::string::npos ||
      argument.find('"') != std::string::npos ||
      argument.find('\'') != std::string::npos) {
    
    std::string escaped = "\"";
    for (char c : argument) {
      if (c == '"' || c == '\\') {
        escaped += '\\';
      }
      escaped += c;
    }
    escaped += "\"";
    return escaped;
  }
  
  return argument;
}

std::string ProcessCommandParser::ExpandEnvironmentVariables(const std::string& str) {
  std::string result;
  std::string var_name;
  bool in_var = false;
  bool in_brace = false;
  
  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    
    if (c == '$' && !in_var) {
      in_var = true;
      if (i + 1 < str.size() && str[i + 1] == '{') {
        in_brace = true;
        ++i;  // Skip '{'
      }
    } else if (in_var) {
      if (in_brace) {
        if (c == '}') {
          in_var = false;
          in_brace = false;
          result += ExpandVariable(var_name);
          var_name.clear();
        } else {
          var_name += c;
        }
      } else {
        if (isalnum(c) || c == '_') {
          var_name += c;
        } else {
          in_var = false;
          result += ExpandVariable(var_name);
          var_name.clear();
          result += c;
        }
      }
    } else {
      result += c;
    }
  }
  
  if (in_var) {
    result += ExpandVariable(var_name);
  }
  
  return result;
}

std::vector<std::string> ProcessCommandParser::Tokenize(const std::string& command) {
  std::vector<std::string> tokens;
  std::string current_token;
  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escape_next = false;
  
  for (char c : command) {
    if (escape_next) {
      current_token += c;
      escape_next = false;
      continue;
    }
    
    if (c == '\\' && !in_single_quote) {
      escape_next = true;
      continue;
    }
    
    if (c == '\'' && !in_double_quote) {
      in_single_quote = !in_single_quote;
      continue;
    }
    
    if (c == '"' && !in_single_quote) {
      in_double_quote = !in_double_quote;
      continue;
    }
    
    if (IsWhitespace(c) && !in_single_quote && !in_double_quote) {
      if (!current_token.empty()) {
        tokens.push_back(current_token);
        current_token.clear();
      }
      continue;
    }
    
    current_token += c;
  }
  
  if (!current_token.empty()) {
    tokens.push_back(current_token);
  }
  
  return tokens;
}

bool ProcessCommandParser::ValidateCommand(const std::string& command) {
  // Check for obviously dangerous commands
  std::string lower = command;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  
  // Block certain dangerous patterns
  if (lower.find("rm -rf /") != std::string::npos ||
      lower.find("dd if=") != std::string::npos ||
      lower.find(":(){:|:&};:") != std::string::npos) {  // Fork bomb
    return false;
  }
  
  // On Windows, check for dangerous Windows commands
#ifdef _WIN32
  if (lower.find("format c:") != std::string::npos ||
      lower.find("del /f /q") != std::string::npos) {
    return false;
  }
#endif
  
  return true;
}

bool ProcessCommandParser::HasShellMetacharacters(const std::string& command) {
  const char* metachars = "|&;<>()$`\\'\" \t\n";
  
  for (char c : command) {
    if (strchr(metachars, c) != nullptr) {
      return true;
    }
  }
  
  return false;
}

std::string ProcessCommandParser::FindExecutableInPath(const std::string& command) {
  // If command contains path separator, return as-is
  if (command.find('/') != std::string::npos ||
      command.find('\\') != std::string::npos) {
    return command;
  }
  
  // Get PATH environment variable
  const char* path_env = getenv("PATH");
  if (!path_env) {
    return "";
  }
  
  std::string path = path_env;
  
#ifdef _WIN32
  char separator = ';';
#else
  char separator = ':';
#endif
  
  size_t start = 0;
  size_t end = path.find(separator);
  
  while (true) {
    std::string dir = path.substr(start, end - start);
    
    if (!dir.empty()) {
      std::string full_path = dir;
      
#ifdef _WIN32
      if (full_path.back() != '\\' && full_path.back() != '/') {
        full_path += '\\';
      }
      full_path += command + ".exe";
#else
      if (full_path.back() != '/') {
        full_path += '/';
      }
      full_path += command;
#endif
      
      // Check if file exists and is executable
#ifdef _WIN32
      DWORD attrs = GetFileAttributesA(full_path.c_str());
      if (attrs != INVALID_FILE_ATTRIBUTES &&
          !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return full_path;
      }
#else
      struct stat st;
      if (stat(full_path.c_str(), &st) == 0 &&
          (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return full_path;
      }
#endif
    }
    
    if (end == std::string::npos) {
      break;
    }
    
    start = end + 1;
    end = path.find(separator, start);
  }
  
  return "";
}

std::string ProcessCommandParser::ExpandVariable(const std::string& name) {
  const char* value = getenv(name.c_str());
  if (value) {
    return value;
  }
  return "";
}

bool ProcessCommandParser::IsQuote(char c) {
  return c == '"' || c == '\'';
}

bool ProcessCommandParser::IsEscape(char c) {
  return c == '\\';
}

bool ProcessCommandParser::IsWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}  // namespace content::process
