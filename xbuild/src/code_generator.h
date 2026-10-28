#pragma once
#include "model.h"
#include <string>
#include <vector>

namespace XBuild {

struct GenerateResult {
    bool success = false;
    std::string output;
    std::string error;
    std::string generated_file;
    std::string generated_header;
    std::string generated_impl;
};

struct MockPrototype {
    std::string name;
    std::string type;  // int, string, bool, float, double, function, class, struct, enum
    bool is_static = false;
    bool is_const = false;
    bool is_virtual = false;
    bool is_override = false;
    bool is_final = false;
    std::string value;
    std::string default_value;
    std::string access_specifier;  // public, private, protected
    std::vector<std::string> function_args;
    std::string function_return_type;
    std::string return_from;  // return value from another variable
    std::string body;  // custom function body
    std::vector<std::string> templates;
    std::vector<std::string> attributes;
};

struct MockClass {
    std::string name;
    std::string base_class;
    bool is_struct = false;
    bool is_final = false;
    bool is_abstract = false;
    std::vector<MockPrototype> members;
    std::vector<std::string> friends;
    std::vector<std::string> using_declarations;
};

struct MockNamespace {
    std::string name;
    std::vector<MockPrototype> prototypes;
    std::vector<MockClass> classes;
    std::vector<std::string> nested_namespaces;
};

struct MockTarget {
    std::string name;
    bool lock = false;
    std::vector<MockNamespace> namespaces;
    std::vector<std::string> includes;
    std::vector<std::string> forward_declarations;
    std::string header_guard;
    std::string pragma_once;
};

// Generate C++ source files from mock_targets configuration using Clang AST
bool GenerateMockCode(const Project& project,
                      const Json::Value& mock_targets,
                      GenerateResult& result);

// Generate C++ code using Clang AST API
bool GenerateWithClangAST(const MockTarget& target,
                          const Project& project,
                          GenerateResult& result);

// Parse mock_targets from JSON to internal structures
bool ParseMockTargets(const Json::Value& mock_targets,
                      std::vector<MockTarget>& targets,
                      std::string& error);

} // namespace XBuild
