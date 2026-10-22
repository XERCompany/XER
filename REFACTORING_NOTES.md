# XER Refactoring Notes

## Completed Improvements

### 1. Documentation
- **README.md**: Restructured with clear sections, table of contents, and comprehensive documentation
- **AGENTS.md**: Added project-specific guidelines for AI agents working on the codebase
- **.gitignore**: Expanded with comprehensive exclusions for build artifacts, IDE files, and temporary files

### 2. Code Organization
- **JSON Config Helper**: Extracted JSON configuration parsing into reusable helper classes
  - `json_config_helper.h/cc`: Generic JSON configuration utilities
  - `audio_config_helper.h/cc`: Audio-specific JSON configuration
- **main.cc**: Removed ~250 lines of duplicated JSON parsing code

## Identified Improvements

### main.cc Refactoring Opportunities

#### 1. Extract Command Handlers
**Current Issue**: The main function is over 500 lines with audio command handling mixed with core logic.

**Suggested Refactor**:
```cpp
// Create command handler classes/functions
class AudioCommandHandler {
public:
    static int HandleAudioDemo(const AppCommand::Parsed& parsed);
    static int HandleAudioFxCustom(const AppCommand::Parsed& parsed);
    static int HandleAudioFxBatch(const AppCommand::Parsed& parsed);
    // ... other audio commands
};

class ScriptCommandHandler {
public:
    static int HandleRun(const AppCommand::Parsed& parsed);
    static int HandleWatch(const AppCommand::Parsed& parsed);
    static int HandleCompile(const AppCommand::Parsed& parsed);
    // ... other script commands
};
```

#### 2. Command Pattern for CLI
**Current Issue**: Long chain of if statements for command dispatch.

**Suggested Refactor**:
```cpp
using CommandHandler = std::function<int(const AppCommand::Parsed&)>;

std::unordered_map<AppCommand::Type, CommandHandler> command_handlers = {
    {AppCommand::Type::kHelp, HandleHelp},
    {AppCommand::Type::kVersion, HandleVersion},
    {AppCommand::Type::kDoctor, HandleDoctor},
    {AppCommand::Type::kAudioDemo, AudioCommandHandler::HandleAudioDemo},
    // ... etc
};

auto handler = command_handlers.find(parsed.type);
if (handler != command_handlers.end()) {
    return handler->second(parsed);
}
```

#### 3. Environment Variable Helper
**Current Issue**: `ExportParsedToEnv` function is verbose and has many repeated patterns.

**Suggested Refactor**:
```cpp
class EnvironmentExporter {
public:
    static void Export(const AppCommand::Parsed& parsed);

private:
    static void SetValue(const char* key, const std::string& value);
    static void SetFlag(const char* key, bool enabled);
    static void ExportScriptConfig(const AppCommand::Parsed& parsed);
    static void ExportSecurityConfig(const AppCommand::Parsed& parsed);
    static void ExportCompilerConfig(const AppCommand::Parsed& parsed);
    // ... grouped by category
};
```

#### 4. Output Helper Consolidation
**Current Issue**: Multiple similar output functions (`WriteOutput`, `WriteOutputLine`, `WriteStdout`, etc.).

**Suggested Refactor**:
```cpp
class ConsoleOutput {
public:
    enum class Stream { Stdout, Stderr };

    static void Write(Stream stream, const std::string& text);
    static void WriteLine(Stream stream, const std::string& text);
    static void WriteBytes(const std::vector<std::uint8_t>& bytes);
};
```

#### 5. Path Helper
**Current Issue**: Repeated patterns for creating output directories and handling paths.

**Suggested Refactor**:
```cpp
class PathHelper {
public:
    static bool EnsureDirectory(const std::filesystem::path& path, std::string* error_out);
    static std::filesystem::path GetDefaultOutputDir(const std::string& subpath);
    static std::filesystem::path ResolveInputPath(const std::string& input);
};
```

### Code Smells Found

#### 1. Duplicated Code
- Audio command handlers share similar patterns for path resolution and option building
- Output directory creation is repeated multiple times
- Error reporting patterns are duplicated

#### 2. Long Method
- `main()` function is over 500 lines
- Should be broken into smaller, focused functions

#### 3. Feature Envy
- Many functions reach into `AppCommand::Parsed` structure repeatedly
- Could benefit from builder pattern for option structures

#### 4. Primitive Obsession
- String flags for configuration (e.g., "audiofxcustom", "batch")
- Could use enums or type-safe configuration objects

### Priority Recommendations

#### High Priority
1. Extract audio command handlers into separate class
2. Implement command pattern for dispatch
3. Consolidate output helpers

#### Medium Priority
4. Refactor environment variable exporter
5. Add path helper utilities
6. Extract audio option builders

#### Low Priority
7. Consider configuration object instead of parsed struct
8. Add validation layer for command options
9. Improve error handling with custom exception hierarchy

## Testing Improvements

### Missing Test Coverage
- Unit tests for JSON config helpers
- Integration tests for command handlers
- Mock tests for file I/O operations

### Suggested Test Structure
```
tests/
├── unit/
│   ├── json_config_helper_test.cc
│   ├── audio_config_helper_test.cc
│   └── path_helper_test.cc
├── integration/
│   ├── audio_command_test.cc
│   └── script_command_test.cc
└── mocks/
    ├── file_io_mock.h
    └── environment_mock.h
```

## Performance Considerations

### Current State
- Good: SIMD acceleration for audio processing
- Good: Async I/O with thread pools
- Concern: Repeated string allocations in command parsing
- Concern: Multiple filesystem checks in hot paths

### Optimization Opportunities
1. String interning for command names
2. Cache filesystem checks
3. Reuse buffers in audio processing
4. Lazy initialization for optional features

## Security Considerations

### Current State
- Good: Sandbox support
- Good: Runtime safety checks
- Concern: Path traversal vulnerabilities in file operations
- Concern: Unvalidated user input in config parsing

### Recommendations
1. Add path validation utilities
2. Sanitize all user inputs
3. Implement proper error handling for malformed configs
4. Add rate limiting for file operations

## Next Steps

1. Create AudioCommandHandler class
2. Implement command pattern
3. Add unit tests for new helpers
4. Refactor environment exporter
5. Add path validation
6. Update documentation
