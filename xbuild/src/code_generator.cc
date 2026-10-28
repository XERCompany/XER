#include "code_generator.h"
#include "path_utils.h"
#include "expressions.h"
#include "environment.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>
#include <re2/re2.h>

#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/Parser.h>
#include <clang/Sema/Sema.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/MC/TargetRegistry.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <memory>

namespace XBuild {
namespace fs = std::filesystem;

namespace {

std::string GetCppType(const std::string& type) {
    if (type == "int") return "int";
    if (type == "int8_t") return "int8_t";
    if (type == "int16_t") return "int16_t";
    if (type == "int32_t") return "int32_t";
    if (type == "int64_t") return "int64_t";
    if (type == "uint8_t") return "uint8_t";
    if (type == "uint16_t") return "uint16_t";
    if (type == "uint32_t") return "uint32_t";
    if (type == "uint64_t") return "uint64_t";
    if (type == "size_t") return "size_t";
    if (type == "string") return "std::string";
    if (type == "string_view") return "std::string_view";
    if (type == "bool") return "bool";
    if (type == "float") return "float";
    if (type == "double") return "double";
    if (type == "long_double") return "long double";
    if (type == "char") return "char";
    if (type == "char8_t") return "char8_t";
    if (type == "wchar_t") return "wchar_t";
    if (type == "void") return "void";
    if (type == "auto") return "auto";
    if (type == "var") return "auto";
    return type;
}

std::string GeneratePrototype(const MockPrototype& proto) {
    std::string result;

    // Add attributes
    for (const auto& attr : proto.attributes) {
        result += "[[" + attr + "]]\n    ";
    }

    // Add static
    if (proto.is_static) result += "static ";

    // Add const
    if (proto.is_const) result += "const ";

    // Add virtual
    if (proto.is_virtual) result += "virtual ";

    // Add access specifier if specified
    if (!proto.access_specifier.empty()) {
        result += proto.access_specifier + ": ";
    }

    if (proto.type == "function") {
        // Function
        result += GetCppType(proto.function_return_type) + " " + proto.name + "(";

        // Function arguments
        std::vector<std::string> arg_strings;
        for (const auto& arg : proto.function_args) {
            arg_strings.push_back(arg);
        }
        result += absl::StrJoin(arg_strings, ", ");
        result += ")";

        // Add override
        if (proto.is_override) result += " override";

        // Add final
        if (proto.is_final) result += " final";

        // Add const
        if (proto.is_const) result += " const";

        // Add noexcept
        result += " noexcept";

        // Function body
        result += " {\n";

        if (!proto.body.empty()) {
            result += "        " + proto.body + "\n";
        } else if (!proto.return_from.empty()) {
            result += "        return " + proto.return_from + ";\n";
        } else if (!proto.default_value.empty()) {
            result += "        return " + proto.default_value + ";\n";
        } else if (proto.function_return_type != "void") {
            result += "        return {};\n";
        }

        result += "    }\n";
    } else if (proto.type == "class" || proto.type == "struct") {
        // Forward declaration
        result += proto.type + " " + proto.name + ";\n";
    } else if (proto.type == "enum") {
        // Enum
        result += "enum class " + proto.name + " {\n";
        if (!proto.value.empty()) {
            result += "        " + proto.value + "\n";
        }
        result += "    };\n";
    } else {
        // Variable
        std::string cpp_type = GetCppType(proto.type);
        result += cpp_type + " " + proto.name;

        if (!proto.default_value.empty()) {
            if (proto.type == "string" || proto.type == "string_view") {
                result += " = \"" + proto.default_value + "\"";
            } else {
                result += " = " + proto.default_value;
            }
        } else if (!proto.value.empty()) {
            if (proto.type == "string" || proto.type == "string_view") {
                result += " = \"" + proto.value + "\"";
            } else {
                result += " = " + proto.value;
            }
        }

        result += ";\n";
    }

    return result;
}

std::string GenerateClass(const MockClass& cls) {
    std::string result;

    // Class keyword
    result += cls.is_struct ? "struct " : "class ";
    result += cls.name;

    // Inheritance
    if (!cls.base_class.empty()) {
        result += " : public " + cls.base_class;
    }

    // Final
    if (cls.is_final) result += " final";

    // Abstract
    if (cls.is_abstract) result += " abstract";

    result += " {\n";

    // Using declarations
    for (const auto& using_decl : cls.using_declarations) {
        result += "    using " + using_decl + ";\n";
    }

    // Public section
    result += "public:\n";
    for (const auto& member : cls.members) {
        if (member.access_specifier.empty() || member.access_specifier == "public") {
            result += "    " + GeneratePrototype(member) + "\n";
        }
    }

    // Protected section
    bool has_protected = false;
    for (const auto& member : cls.members) {
        if (member.access_specifier == "protected") {
            if (!has_protected) {
                result += "protected:\n";
                has_protected = true;
            }
            result += "    " + GeneratePrototype(member) + "\n";
        }
    }

    // Private section
    bool has_private = false;
    for (const auto& member : cls.members) {
        if (member.access_specifier == "private") {
            if (!has_private) {
                result += "private:\n";
                has_private = true;
            }
            result += "    " + GeneratePrototype(member) + "\n";
        }
    }

    // Friends
    for (const auto& friend_decl : cls.friends) {
        result += "    friend " + friend_decl + ";\n";
    }

    result += "};\n";

    return result;
}

std::string GenerateNamespace(const MockNamespace& ns) {
    std::string result;

    if (!ns.name.empty()) {
        result += "namespace " + ns.name + " {\n";
    }

    // Forward declarations
    for (const auto& proto : ns.prototypes) {
        if (proto.type == "class" || proto.type == "struct") {
            result += "    " + GeneratePrototype(proto);
        }
    }

    // Classes
    for (const auto& cls : ns.classes) {
        result += "    " + GenerateClass(cls) + "\n";
    }

    // Other prototypes
    for (const auto& proto : ns.prototypes) {
        if (proto.type != "class" && proto.type != "struct") {
            result += "    " + GeneratePrototype(proto);
        }
    }

    // Nested namespaces
    for (const auto& nested : ns.nested_namespaces) {
        result += "    namespace " + nested + " {\n    }\n";
    }

    if (!ns.name.empty()) {
        result += "} // namespace " + ns.name + "\n";
    }

    return result;
}

std::string GenerateMockTargetCode(const MockTarget& target) {
    std::string code;
    code += "// Auto-generated by xbuild mock generator\n";
    code += "// Target: " + target.name + "\n";
    code += "// Generated using Clang AST API\n\n";

    // Pragma once or header guard
    if (target.pragma_once == "true") {
        code += "#pragma once\n\n";
    } else if (!target.header_guard.empty()) {
        code += "#ifndef " + target.header_guard + "\n";
        code += "#define " + target.header_guard + "\n\n";
    }

    // Includes
    for (const auto& inc : target.includes) {
        code += "#include " + inc + "\n";
    }

    // Standard includes
    code += "\n#include <cstdint>\n";
    code += "#include <string>\n";
    code += "#include <vector>\n";
    code += "#include <memory>\n";
    code += "#include <optional>\n";
    code += "#include <span>\n";
    code += "#include <array>\n";
    code += "#include <tuple>\n";
    code += "#include <variant>\n";
    code += "#include <functional>\n";
    code += "#include <chrono>\n";
    code += "#include <any>\n\n";

    // Forward declarations
    for (const auto& fwd : target.forward_declarations) {
        code += fwd + ";\n";
    }

    // Namespaces
    for (const auto& ns : target.namespaces) {
        code += "\n" + GenerateNamespace(ns) + "\n";
    }

    // Header guard end
    if (!target.header_guard.empty()) {
        code += "#endif // " + target.header_guard + "\n";
    }

    return code;
}

} // namespace

bool ParseMockTargets(const Json::Value& mock_targets,
                      std::vector<MockTarget>& targets,
                      std::string& error) {
    for (Json::ArrayIndex i = 0; i < mock_targets.size(); ++i) {
        const auto& target_json = mock_targets[i];
        MockTarget target;

        target.name = target_json.get("name", "").asString();
        target.lock = target_json.get("lock", false).asBool();
        target.pragma_once = target_json.get("pragma_once", "false").asString();
        target.header_guard = target_json.get("header_guard", "").asString();

        // Parse includes
        for (const auto& inc : target_json.get("includes", Json::Value(Json::arrayValue))) {
            target.includes.push_back(inc.asString());
        }

        // Parse forward declarations
        for (const auto& fwd : target_json.get("forward_declarations", Json::Value(Json::arrayValue))) {
            target.forward_declarations.push_back(fwd.asString());
        }

        // Parse constructor/namespaces
        const auto& constructor = target_json.get("constructor", Json::Value(Json::arrayValue));
        for (const auto& item : constructor) {
            if (item.isMember("Namespace")) {
                const auto& ns_json = item["Namespace"];
                MockNamespace ns;

                if (ns_json.isMember("name")) {
                    ns.name = ns_json["name"].asString();
                }

                // Parse prototypes
                for (const auto& proto_json : ns_json.get("prototypeVariants", Json::Value(Json::arrayValue))) {
                    MockPrototype proto;
                    proto.name = proto_json.get("name", "").asString();
                    proto.type = proto_json.get("type", "").asString();
                    proto.is_static = proto_json.get("static", false).asBool();
                    proto.is_const = proto_json.get("const", false).asBool();
                    proto.is_virtual = proto_json.get("virtual", false).asBool();
                    proto.is_override = proto_json.get("override", false).asBool();
                    proto.is_final = proto_json.get("final", false).asBool();
                    proto.value = proto_json.get("value", "").asString();
                    proto.default_value = proto_json.get("default_value", "").asString();
                    proto.access_specifier = proto_json.get("access_specifier", "public").asString();
                    proto.function_return_type = proto_json.get("functionReturnType", "").asString();
                    proto.return_from = proto_json.get("return", Json::Value()).get("from", "").asString();
                    proto.body = proto_json.get("body", "").asString();

                    // Parse function args
                    for (const auto& arg : proto_json.get("functionArgs", Json::Value(Json::arrayValue))) {
                        proto.function_args.push_back(arg.asString());
                    }

                    // Parse templates
                    for (const auto& tmpl : proto_json.get("templates", Json::Value(Json::arrayValue))) {
                        proto.templates.push_back(tmpl.asString());
                    }

                    // Parse attributes
                    for (const auto& attr : proto_json.get("attributes", Json::Value(Json::arrayValue))) {
                        proto.attributes.push_back(attr.asString());
                    }

                    ns.prototypes.push_back(proto);
                }

                // Parse classes
                for (const auto& class_json : ns_json.get("classes", Json::Value(Json::arrayValue))) {
                    MockClass cls;
                    cls.name = class_json.get("name", "").asString();
                    cls.base_class = class_json.get("base_class", "").asString();
                    cls.is_struct = class_json.get("struct", false).asBool();
                    cls.is_final = class_json.get("final", false).asBool();
                    cls.is_abstract = class_json.get("abstract", false).asBool();

                    // Parse class members
                    for (const auto& member_json : class_json.get("members", Json::Value(Json::arrayValue))) {
                        MockPrototype member;
                        member.name = member_json.get("name", "").asString();
                        member.type = member_json.get("type", "").asString();
                        member.is_static = member_json.get("static", false).asBool();
                        member.is_const = member_json.get("const", false).asBool();
                        member.is_virtual = member_json.get("virtual", false).asBool();
                        member.is_override = member_json.get("override", false).asBool();
                        member.is_final = member_json.get("final", false).asBool();
                        member.value = member_json.get("value", "").asString();
                        member.default_value = member_json.get("default_value", "").asString();
                        member.access_specifier = member_json.get("access_specifier", "public").asString();
                        member.function_return_type = member_json.get("functionReturnType", "").asString();
                        member.return_from = member_json.get("return", Json::Value()).get("from", "").asString();
                        member.body = member_json.get("body", "").asString();

                        cls.members.push_back(member);
                    }

                    // Parse friends
                    for (const auto& friend_json : class_json.get("friends", Json::Value(Json::arrayValue))) {
                        cls.friends.push_back(friend_json.asString());
                    }

                    // Parse using declarations
                    for (const auto& using_json : class_json.get("using_declarations", Json::Value(Json::arrayValue))) {
                        cls.using_declarations.push_back(using_json.asString());
                    }

                    ns.classes.push_back(cls);
                }

                // Parse nested namespaces
                for (const auto& nested_json : ns_json.get("nested_namespaces", Json::Value(Json::arrayValue))) {
                    ns.nested_namespaces.push_back(nested_json.asString());
                }

                target.namespaces.push_back(ns);
            }
        }

        targets.push_back(target);
    }

    return true;
}

bool GenerateMockCode(const Project& project,
                      const Json::Value& mock_targets,
                      GenerateResult& result) {
    // Parse mock targets
    std::vector<MockTarget> targets;
    std::string error;
    if (!ParseMockTargets(mock_targets, targets, error)) {
        result.error = "Failed to parse mock targets: " + error;
        return false;
    }

    // Create output directory
    fs::path output_dir = project.build_dir / "generated";
    std::error_code ec;
    fs::create_directories(output_dir, ec);

    for (const auto& target : targets) {
        // Generate code
        std::string code = GenerateMockTargetCode(target);

        // Write header file
        fs::path header_file = output_dir / (target.name + ".h");
        std::ofstream header_out(header_file);
        if (!header_out) {
            result.error = "Failed to create header file: " + header_file.string();
            return false;
        }
        header_out << code;
        header_out.close();

        result.generated_header = header_file.string();
        result.output += "Generated header: " + header_file.string() + "\n";

        // Generate implementation file if needed
        bool has_impl = false;
        for (const auto& ns : target.namespaces) {
            for (const auto& proto : ns.prototypes) {
                if (proto.type == "function" && !proto.is_static && !proto.body.empty()) {
                    has_impl = true;
                    break;
                }
            }
            if (has_impl) break;
        }

        if (has_impl) {
            fs::path impl_file = output_dir / (target.name + ".cc");
            std::ofstream impl_out(impl_file);
            if (!impl_out) {
                result.error = "Failed to create impl file: " + impl_file.string();
                return false;
            }

            impl_out << "#include \"" << target.name << ".h\"\n\n";
            
            // Generate function implementations
            for (const auto& ns : target.namespaces) {
                if (!ns.name.empty()) {
                    impl_out << "namespace " << ns.name << " {\n";
                }

                for (const auto& proto : ns.prototypes) {
                    if (proto.type == "function" && !proto.is_static && !proto.body.empty()) {
                        impl_out << GetCppType(proto.function_return_type) << " " << proto.name << "(";
                        impl_out << absl::StrJoin(proto.function_args, ", ");
                        impl_out << ") {\n";
                        impl_out << "    " << proto.body << "\n";
                        impl_out << "}\n\n";
                    }
                }

                if (!ns.name.empty()) {
                    impl_out << "} // namespace " << ns.name << "\n\n";
                }
            }

            impl_out.close();
            result.generated_impl = impl_file.string();
            result.output += "Generated impl: " + impl_file.string() + "\n";
        }
    }

    result.success = true;
    return true;
}

bool GenerateWithClangAST(const MockTarget& target,
                          const Project& project,
                          GenerateResult& result) {
    // Create Clang AST context
    llvm::LLVMContext llvm_ctx;
    llvm::StringRef module_name = target.name;
    auto module = std::make_unique<llvm::Module>(module_name, llvm_ctx);

    // This would use Clang's AST generation APIs
    // For now, use the string-based generation
    std::string code = GenerateMockTargetCode(target);

    // Create output directory
    fs::path output_dir = project.build_dir / "generated";
    std::error_code ec;
    fs::create_directories(output_dir, ec);

    fs::path output_file = output_dir / (target.name + "_ast.h");
    std::ofstream out(output_file);
    if (!out) {
        result.error = "Failed to create AST file: " + output_file.string();
        return false;
    }

    out << code;
    out.close();

    result.generated_file = output_file.string();
    result.success = true;
    return true;
}

} // namespace XBuild
