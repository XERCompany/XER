#include "compiler.h"
#include "expressions.h"
#include "path_utils.h"
#include "environment.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBufferRef.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <re2/re2.h>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/CodeGen/ModuleBuilder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendDiagnostic.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/FrontendTool/Utils.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Serialization/ASTWriter.h>
#include <clang/Serialization/ModuleFile.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>

#include <filesystem>
#include <thread>
#include <mutex>
#include <vector>
#include <memory>
#include <sstream>

namespace XBuild {
namespace fs = std::filesystem;

namespace {

std::mutex compile_mutex;

// Initialize LLVM targets (call once at startup)
void InitializeLLVMTargets() {
    static bool initialized = false;
    if (!initialized) {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();
        initialized = true;
    }
}

std::string VectorToString(const std::vector<std::string>& vec) {
    std::string result;
    for (const auto& item : vec) {
        if (!result.empty()) result += " ";
        result += item;
    }
    return result;
}

void ApplyCompileOptions(clang::CompilerInstance& compiler, const CompileOptions& opts) {
    // Language options
    auto& lang_opts = compiler.getLangOpts();
    lang_opts.RTTI = opts.rtti;
    lang_opts.Exceptions = opts.exceptions;
    lang_opts.ThreadsafeStatics = opts.threads;
    lang_opts.Coroutines = opts.coroutines; 
    lang_opts.Modules = opts.modules;
    
    // Code generation options
    auto& codegen_opts = compiler.getCodeGenOpts();
    codegen_opts.OptimizationLevel = opts.optimization_level;
    codegen_opts.RelocationModel = opts.pic ? llvm::Reloc::PIC_ : llvm::Reloc::Static;
    
    // Debug info
    if (opts.debug_info) { 
        if (opts.debug_info_kind == "limited") {
            codegen_opts.setDebugInfo(llvm::codegenoptions::DebugInfoKind::LimitedDebugInfo);
        } else if (opts.debug_info_kind == "full") {
            codegen_opts.setDebugInfo(llvm::codegenoptions::DebugInfoKind::FullDebugInfo);
        }
    }
    
    // Inline settings
    // codegen_opts.Inlining = opts.inline_functions;
    codegen_opts.DiscardValueNames = !opts.debug_info;
    
    // LTO
    if (opts.lto) {
        codegen_opts.PrepareForThinLTO = (opts.lto_mode == "thin");
        codegen_opts.PrepareForLTO = true;
    }
    
    // Vectorization
    codegen_opts.VectorizeLoop = opts.loop_vectorization;
    codegen_opts.VectorizeSLP = opts.slp_vectorization; 
     
    // Optimization
    if (opts.optimize_size) {
        codegen_opts.OptimizeSize = 2;
    }
    if (opts.aggressive_optimization) {
        codegen_opts.OptimizationLevel = 3;
    }
    
    // Fast math
    if (opts.fast_math) {
        codegen_opts.LessPreciseFPMAD = true; 
    }
     
    
    // PGO
    if (opts.pgo && !opts.pgo_profile.empty()) {
        codegen_opts.ProfileInstrumentUsePath = opts.pgo_profile;
    }
}

void ApplyTargetOptions(clang::CompilerInstance& compiler, const CompileOptions& opts) {
    auto& target_opts = compiler.getTargetOpts();
    
    if (!opts.target_triple.empty()) {
        target_opts.Triple = opts.target_triple;
    }
    
    if (!opts.target_cpu.empty()) {
        target_opts.CPU = opts.target_cpu;
    }
    
    if (!opts.target_features.empty()) {
        target_opts.Features.push_back(opts.target_features);
    }
}

bool CompileWithClang(const SourceUnit& source,
                      const Project& project,
                      const std::vector<std::string>& includes,
                      const std::vector<std::string>& defines,
                      const CompileOptions& opts,
                      const fs::path& output,
                      clang::FrontendAction& action,
                      std::string& error) {
    InitializeLLVMTargets();

    // Create compiler instance
    clang::CompilerInstance compiler;
    compiler.createDiagnostics();

    if (!compiler.hasDiagnostics()) {
        error = "Failed to create diagnostics";
        return false;
    }

    // Set target
    std::string target_triple = opts.target_triple.empty() ? 
        llvm::sys::getDefaultTargetTriple() : opts.target_triple;
    compiler.getTargetOpts().Triple = target_triple;
    compiler.setTarget(clang::TargetInfo::CreateTargetInfo(compiler.getDiagnostics(),
                                                           compiler.getTargetOpts()));
 

    // Apply compile options
    ApplyCompileOptions(compiler, opts);
    ApplyTargetOptions(compiler, opts);

    // Set language options for C++23
    auto& lang_opts = compiler.getLangOpts();
    lang_opts.CPlusPlus = true;
    lang_opts.CPlusPlus23 = true;
    lang_opts.Bool = true;

    // Set preprocessor options
    auto& preprocessor_opts = compiler.getPreprocessorOpts();

    // Add defines
    for (const auto& define : defines) {
        preprocessor_opts.addMacroDef(define);
    }

    // Add project-level defines
    for (const auto& define : Strings(project.document["defines"])) {
        preprocessor_opts.addMacroDef(define);
    }

    // Add target-specific defines
    if (project.debug) {
        preprocessor_opts.addMacroDef("DEBUG=1");
        preprocessor_opts.addMacroDef("_DEBUG");
    }

    // Add sanitizer defines
    if (opts.address_sanitizer) {
        preprocessor_opts.addMacroDef("ADDRESS_SANITIZER");
    }
    if (opts.memory_sanitizer) {
        preprocessor_opts.addMacroDef("MEMORY_SANITIZER");
    }
    if (opts.thread_sanitizer) {
        preprocessor_opts.addMacroDef("THREAD_SANITIZER");
    }

    // Set header search options
    auto& header_opts = compiler.getHeaderSearchOpts();

    // Add includes
    for (const auto& include : includes) {
        header_opts.AddPath(include, clang::frontend::Angled, false, false);
    }

    // Add project-level includes
    for (const auto& include : Strings(project.document["includes"])) {
        fs::path include_path = AbsolutePath(project.root, include);
        header_opts.AddPath(include_path.string(), clang::frontend::Angled, false, false);
    }

    // Add system includes from vcpkg
    for (const auto& dir : Strings(project.document.get("vcpkg_include_dirs", Json::Value(Json::arrayValue)))) {
        header_opts.AddPath(dir, clang::frontend::System, false, false);
    }

    // Set invocation
    compiler.createFileManager();
    compiler.createSourceManager();
 
    // Set up frontend options
    auto& frontend_opts = compiler.getFrontendOpts();
    frontend_opts.Inputs.clear();
    frontend_opts.Inputs.push_back(clang::FrontendInputFile(
        source.input.string(),
        clang::InputKind(
            clang::Language::CXX,
            clang::InputKind::Format::Source
        )
    ));

    // Set output file
    frontend_opts.OutputFile = output.string();

    // Add extra compile flags
    for (const auto& flag : opts.extra_compile_flags) {
        frontend_opts.LLVMArgs.push_back(flag);
    }

    // Compile
    bool success = compiler.ExecuteAction(action);

    if (!success) {
        error = "Compilation failed";
    }

    return success;
}

} // namespace

bool CompileSource(const SourceUnit& source,
                   const Project& project,
                   const std::vector<std::string>& includes,
                   const std::vector<std::string>& defines,
                   const CompileOptions& opts,
                   CompileResult& result) {
    // Generate object file path
    fs::path object_dir = project.build_dir / "obj";
    std::error_code ec;
    fs::create_directories(object_dir, ec);

    fs::path object_file;
    if (!source.output.empty()) {
        object_file = source.output;
    } else {
        object_file = object_dir / (source.input.filename().string() + ".o");
    }

    // Execute pre-build hook
    if (!source.before.empty()) {
        std::string hook_output, hook_error;
        FILE* hook_pipe = _popen(source.before.c_str(), "r");
        if (hook_pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), hook_pipe) != nullptr) {
                hook_output += buffer;
            }
            _pclose(hook_pipe);
        }
    }

    // Create frontend action for generating object file
    clang::EmitObjAction emit_obj_action;

    // Compile using Clang library
    bool success = CompileWithClang(source, project, includes, defines, opts, 
                                      object_file, emit_obj_action, result.error);

    // Execute post-build hook
    if (!source.after.empty()) {
        std::string hook_output, hook_error;
        FILE* hook_pipe = _popen(source.after.c_str(), "r");
        if (hook_pipe) {
            char buffer[4096];
            while (fgets(buffer, sizeof(buffer), hook_pipe) != nullptr) {
                hook_output += buffer;
            }
            _pclose(hook_pipe);
        }
    }

    result.success = success;
    result.object_file = object_file.string();

    return success;
}

bool CompileSources(const std::vector<SourceUnit>& sources,
                     const Project& project,
                     const std::vector<std::string>& includes,
                     const std::vector<std::string>& defines,
                     const CompileOptions& opts,
                     std::vector<CompileResult>& results) {
    results.resize(sources.size());
    std::vector<std::thread> threads;
    unsigned max_threads = std::min(project.jobs, static_cast<unsigned>(sources.size()));

    for (unsigned t = 0; t < max_threads; ++t) {
        threads.emplace_back([&](unsigned thread_id) {
            for (size_t i = thread_id; i < sources.size(); i += max_threads) {
                if (sources[i].header) {
                    results[i].success = true;
                    continue;
                }
                if (sources[i].delay) {
                    results[i].success = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(sources[i].delay_ms));
                    continue;
                }
                std::lock_guard<std::mutex> lock(compile_mutex);
                CompileSource(sources[i], project, includes, defines, opts, results[i]);
            }
        }, t);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    bool all_success = true;
    for (const auto& result : results) {
        if (!result.success) {
            all_success = false;
            break;
        }
    }

    return all_success;
}

bool GenerateLLVMIR(const SourceUnit& source,
                     const Project& project,
                     const std::vector<std::string>& includes,
                     const std::vector<std::string>& defines,
                     const CompileOptions& opts,
                     CompileResult& result) {
    fs::path ir_dir = project.build_dir / "ir";
    std::error_code ec;
    fs::create_directories(ir_dir, ec);

    fs::path ir_file = ir_dir / (source.input.filename().string() + ".ll");

    clang::EmitLLVMOnlyAction emit_llvm_action;
    bool success = CompileWithClang(source, project, includes, defines, opts,
                                      ir_file, emit_llvm_action, result.error);

    result.success = success;
    result.llvm_ir = ir_file.string();

    return success;
}

bool GenerateAssembly(const SourceUnit& source,
                      const Project& project,
                      const std::vector<std::string>& includes,
                      const std::vector<std::string>& defines,
                      const CompileOptions& opts,
                      CompileResult& result) {
    fs::path asm_dir = project.build_dir / "asm";
    std::error_code ec;
    fs::create_directories(asm_dir, ec);

    fs::path asm_file = asm_dir / (source.input.filename().string() + ".s");

    clang::EmitAssemblyAction emit_asm_action;
    bool success = CompileWithClang(source, project, includes, defines, opts,
                                      asm_file, emit_asm_action, result.error);

    result.success = success;
    result.asm_output = asm_file.string();

    return success;
}

} // namespace XBuild
