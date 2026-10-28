#include "project_config.h"
#include "model.h"
#include "source_resolver.h"
#include "compiler.h"
#include "linker.h"
#include "code_generator.h"
#include "environment.h"

#include <clang/Basic/Version.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>

namespace {

void PrintUsage(std::ostream& out) {
    out << "Usage: xbuild build [project.json]\n"
           "       xbuild validate [project.json]\n"
           "       xbuild --help\n"
           "       xbuild --version\n\n"
           "Commands:\n"
           "  build    Build the project (default: ./project.json)\n"
           "  validate Validate project configuration (default: ./project.json)\n";
}

} // namespace

int main(int argc, char* argv[]) {
    llvm::cl::OptionCategory category("xbuild options");
    llvm::cl::list<std::string> arguments(
        llvm::cl::Positional, llvm::cl::desc("<command> [project.json]"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category));
    llvm::cl::HideUnrelatedOptions(category);
    llvm::cl::SetVersionPrinter([](llvm::raw_ostream& out) {
        out << "xbuild 0.1.0\n" << '\n';
    }); 

    if (!llvm::cl::ParseCommandLineOptions(
            argc, argv,
            "xbuild validate [project.json]\nValidate configuration only; builds, hooks and downloads are not executed.\n")) {
        PrintUsage(std::cerr);
        return 2;
    }
    if (arguments.empty() || arguments.size() > 2) {
        PrintUsage(std::cerr);
        return 2;
    }
    if (arguments[0] != "validate" && arguments[0] != "build") {
        std::cerr << "xbuild: Unknown command: " << arguments[0] << '\n';
        PrintUsage(std::cerr);
        return 2;
    }

    const std::filesystem::path path = arguments.size() == 2 ? arguments[1] : "project.json";
    Json::Value project_doc;
    std::string error;
    if (!XBuild::LoadProjectConfig(path, project_doc, error)) {
        std::cerr << "xbuild: " << error << '\n';
        return 1;
    }

    if (arguments[0] == "validate") {
        std::cout << "Configuration valid: " << path.string() << '\n'
                  << "Targets: " << project_doc["targets"].size() << '\n'
                  << "Validation only; no build actions were executed.\n";
        return 0;
    }

    // Build command
    XBuild::Project project;
    if (!XBuild::LoadProject(path, project, error)) {
        std::cerr << "xbuild: " << error << '\n';
        return 1;
    }

    XBuild::Environment environment = XBuild::Environment::Capture();

    // Set up compile options from project.json
    XBuild::CompileOptions compile_opts;
    compile_opts.optimization_level = project_doc.get("optimization_level", project.debug ? 0 : 2).asInt();
    compile_opts.debug_info = project.debug;
    compile_opts.cpp_standard = project_doc.get("cpp_standard", "c++23").asString();
    compile_opts.pic = project_doc.get("pic", true).asBool();
    compile_opts.warnings_as_errors = project_doc.get("warnings_as_errors", false).asBool();
    compile_opts.all_warnings = project_doc.get("all_warnings", true).asBool();
    compile_opts.extra_warnings = project_doc.get("extra_warnings", true).asBool();
    compile_opts.rtti = project_doc.get("rtti", true).asBool();
    compile_opts.exceptions = project_doc.get("exceptions", true).asBool();
    compile_opts.address_sanitizer = project_doc.get("address_sanitizer", false).asBool();
    compile_opts.memory_sanitizer = project_doc.get("memory_sanitizer", false).asBool();
    compile_opts.thread_sanitizer = project_doc.get("thread_sanitizer", false).asBool();
    compile_opts.undefined_sanitizer = project_doc.get("undefined_sanitizer", false).asBool();
    compile_opts.lto = project_doc.get("lto", false).asBool();
    compile_opts.lto_mode = project_doc.get("lto_mode", "thin").asString();
    compile_opts.optimize_size = project_doc.get("optimize_size", false).asBool();
    compile_opts.fast_math = project_doc.get("fast_math", false).asBool();
    compile_opts.target_triple = project_doc.get("target_triple", "").asString();
    compile_opts.target_cpu = project_doc.get("target_cpu", "").asString();

    // Extra flags
    for (const auto& flag : XBuild::Strings(project_doc.get("extra_compile_flags", Json::Value(Json::arrayValue)))) {
        compile_opts.extra_compile_flags.push_back(flag);
    }
    for (const auto& flag : XBuild::Strings(project_doc.get("extra_link_flags", Json::Value(Json::arrayValue)))) {
        compile_opts.extra_link_flags.push_back(flag);
    }

    // Generate mock code if enabled
    if (project_doc.get("mock_generate_gen_cpp", false).asBool()) {
        std::cout << "Generating mock code with Clang AST API...\n";
        XBuild::GenerateResult gen_result;
        if (project_doc.isMember("mock_targets")) {
            if (!XBuild::GenerateMockCode(project, project_doc["mock_targets"], gen_result)) {
                std::cerr << "xbuild: Mock code generation failed: " << gen_result.error << '\n';
                return 1;
            }
            std::cout << gen_result.output;
        }
    }

    // Collect includes and defines from project
    std::vector<std::string> includes = XBuild::Strings(project_doc["includes"]);
    std::vector<std::string> defines = XBuild::Strings(project_doc["defines"]);

    // Add vcpkg include directories
    for (const auto& dir : XBuild::Strings(project_doc.get("vcpkg_include_dirs", Json::Value(Json::arrayValue)))) {
        includes.push_back(dir);
    }

    // Process each target
    const auto& targets = project_doc["targets"];
    std::vector<std::vector<std::string>> all_object_files;
    std::vector<XBuild::CompileResult> all_compile_results;

    for (Json::ArrayIndex i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        std::cout << "Building target: " << target["name"].asString() << '\n';

        // Add target-specific includes and defines
        std::vector<std::string> target_includes = includes;
        for (const auto& inc : XBuild::Strings(target.get("include_target", Json::Value(Json::arrayValue)))) {
            target_includes.push_back(inc);
        }

        std::vector<std::string> target_defines = defines;
        for (const auto& def : XBuild::Strings(target.get("define_target", Json::Value(Json::arrayValue)))) {
            target_defines.push_back(def);
        }

        // Resolve sources
        std::vector<XBuild::SourceUnit> sources;
        if (!XBuild::ResolveSources(project, target, environment, sources, error)) {
            std::cerr << "xbuild: " << error << '\n';
            return 1;
        }

        std::cout << "  Compiling " << sources.size() << " source files...\n";

        // Compile sources with options
        std::vector<XBuild::CompileResult> compile_results;
        if (!XBuild::CompileSources(sources, project, target_includes, target_defines, compile_opts, compile_results)) {
            std::cerr << "xbuild: Compilation failed\n";
            for (const auto& result : compile_results) {
                if (!result.success && !result.error.empty()) {
                    std::cerr << "  Error: " << result.error << '\n';
                }
            }
            return 1;
        }

        // Collect object files
        std::vector<std::string> object_files;
        for (const auto& result : compile_results) {
            if (!result.object_file.empty()) {
                object_files.push_back(result.object_file);
            }
        }

        all_object_files.push_back(object_files);
        all_compile_results.insert(all_compile_results.end(),
                                    compile_results.begin(), compile_results.end());

        std::cout << "  Compilation successful\n";
    }

    // Link targets
    std::cout << "Linking targets...\n";
    std::vector<XBuild::LinkResult> link_results;
    if (!XBuild::LinkTargets(project, targets, all_object_files, link_results)) {
        std::cerr << "xbuild: Linking failed\n";
        for (const auto& result : link_results) {
            if (!result.success && !result.error.empty()) {
                std::cerr << "  Error: " << result.error << '\n';
            }
        }
        return 1;
    }

    std::cout << "Build successful!\n";
    for (const auto& result : link_results) {
        std::cout << "  Output: " << result.executable << '\n';
    }

    return 0;
}
