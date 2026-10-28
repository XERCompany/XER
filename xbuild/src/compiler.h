#pragma once
#include "model.h"
#include <string>
#include <vector>

namespace XBuild {

struct CompileResult {
    bool success = false;
    std::string output;
    std::string error;
    std::string object_file;
    std::string llvm_ir;
    std::string asm_output;
};

struct CompileOptions {
    // Optimization levels
    int optimization_level = 2;  // 0-3, -O0 to -O3
    bool debug_info = false;
    
    // C++ standard
    std::string cpp_standard = "c++23";
    
    // Code generation
    bool pic = true;  // Position Independent Code
    bool pie = false;  // Position Independent Executable
    std::string debug_info_kind = "line-tables-only";  // none, line-tables-only, limited, full
    
    // Warnings
    bool warnings_as_errors = false;
    bool all_warnings = true;
    bool extra_warnings = true;
    bool pedantic = false;
    std::vector<std::string> specific_warnings;
    std::vector<std::string> no_warnings;
    
    // Language features
    bool rtti = true;
    bool exceptions = true;
    bool threads = true;
    bool coroutines = true;
    bool concepts = true;
    bool modules = false;
    
    // Sanitizers
    bool address_sanitizer = false;
    bool memory_sanitizer = false;
    bool thread_sanitizer = false;
    bool undefined_sanitizer = false;
    
    // LTO (Link Time Optimization)
    bool lto = false;
    std::string lto_mode = "thin";  // thin, full
    
    // Vectorization 
    bool slp_vectorization = true;
    bool loop_vectorization = true;
    
    // Target specific
    std::string target_triple;
    std::string target_cpu;
    std::string target_features;
    
    // Code size optimization
    bool optimize_size = false;
    bool aggressive_optimization = false;
    
    // Fast math
    bool fast_math = false;
    bool finite_math_only = false;
    
    // Inline
    bool inline_functions = true;
    int inline_limit = 500;
    
    // Link time options
    bool dead_strip = true;
    bool devirtualize = true;
    
    // PGO (Profile Guided Optimization)
    bool pgo = false;
    std::string pgo_profile;
    
    // Custom flags
    std::vector<std::string> extra_compile_flags;
    std::vector<std::string> extra_link_flags;
};

// Compile a single source file to an object file
bool CompileSource(const SourceUnit& source,
                   const Project& project,
                   const std::vector<std::string>& includes,
                   const std::vector<std::string>& defines,
                   const CompileOptions& options,
                   CompileResult& result);

// Compile multiple sources in parallel
bool CompileSources(const std::vector<SourceUnit>& sources,
                     const Project& project,
                     const std::vector<std::string>& includes,
                     const std::vector<std::string>& defines,
                     const CompileOptions& options,
                     std::vector<CompileResult>& results);

// Generate LLVM IR from source
bool GenerateLLVMIR(const SourceUnit& source,
                     const Project& project,
                     const std::vector<std::string>& includes,
                     const std::vector<std::string>& defines,
                     const CompileOptions& options,
                     CompileResult& result);

// Generate assembly from source
bool GenerateAssembly(const SourceUnit& source,
                      const Project& project,
                      const std::vector<std::string>& includes,
                      const std::vector<std::string>& defines,
                      const CompileOptions& options,
                      CompileResult& result);

} // namespace XBuild
