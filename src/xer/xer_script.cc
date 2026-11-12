#include "xer/xer_script.h"

#include "xer/xer_encode.h"

#include <absl/container/flat_hash_map.h>
#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_replace.h>
#include <range/v3/view/transform.hpp>

#include <uv.h>

#include "flux/terminal/terminal_output_renderer.h"

#if ENGINE_HAS_ICU
#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#endif

#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;
};

enum class TokenKind {
    kEndOfFile,
    kIdentifier,
    kNumber,
    kString,
    kImport,
    kAs,
    kAsync,
    kLocal,
    kThen,
    kEnd,
    kLParen,
    kRParen,
    kLBrace,
    kRBrace,
    kComma,
    kColon,
    kSemicolon,
    kEqual,
    kArrow,
};

struct Token {
    TokenKind kind = TokenKind::kEndOfFile;
    std::string text;
    SourceLocation location;
};

struct Expression;

struct CallArgument {
    std::optional<std::string> name;
    std::unique_ptr<Expression> value;
};

struct MapEntry {
    std::string key;
    std::unique_ptr<Expression> value;
};

enum class ExpressionKind {
    kNumber,
    kString,
    kIdentifier,
    kCall,
    kMap,
};

struct Expression {
    ExpressionKind kind = ExpressionKind::kIdentifier;
    SourceLocation location;
    std::int64_t number_value = 0;
    std::string string_value;
    std::string identifier_name;
    std::optional<std::string> receiver_name;
    std::string callee_name;
    std::vector<CallArgument> call_arguments;
    std::vector<MapEntry> map_entries;
};

enum class StatementKind {
    kLocalAssignment,
    kExpression,
};

struct Statement {
    StatementKind kind = StatementKind::kExpression;
    SourceLocation location;
    std::string local_name;
    std::unique_ptr<Expression> value;
};

struct ImportDeclaration {
    SourceLocation location;
    std::string module_name;
    std::string alias;
};

struct FunctionDeclaration {
    SourceLocation location;
    bool is_async = false;
    std::string name;
    std::vector<Statement> body;
};

struct Program {
    std::vector<ImportDeclaration> imports;
    std::vector<FunctionDeclaration> functions;
    std::vector<Statement> top_level_statements;
};

class ParserError : public std::runtime_error {
public:
    ParserError(const SourceLocation& location, const std::string& message)
        : std::runtime_error(absl::StrFormat("%zu:%zu: %s", location.line, location.column, message)),
          location_(location) {}

    const SourceLocation& location() const { return location_; }

private:
    SourceLocation location_;
};

class RuntimeError : public std::runtime_error {
public:
    RuntimeError(const SourceLocation& location, const std::string& message)
        : std::runtime_error(absl::StrFormat("%zu:%zu: %s", location.line, location.column, message)),
          location_(location) {}

    const SourceLocation& location() const { return location_; }

private:
    SourceLocation location_;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> Tokenize() {
        std::vector<Token> tokens;
        while (!AtEnd()) {
            SkipWhitespaceAndComments();
            if (AtEnd()) {
                break;
            }

            const SourceLocation location = location_;
            const char ch = Peek();

            if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
                tokens.push_back(LexIdentifierOrKeyword());
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(ch))) {
                tokens.push_back(LexNumber());
                continue;
            }

            switch (ch) {
                case '@':
                    tokens.push_back(LexImportKeyword());
                    break;
                case '(': tokens.push_back(MakeSingleCharToken(TokenKind::kLParen)); break;
                case ')': tokens.push_back(MakeSingleCharToken(TokenKind::kRParen)); break;
                case '{': tokens.push_back(MakeSingleCharToken(TokenKind::kLBrace)); break;
                case '}': tokens.push_back(MakeSingleCharToken(TokenKind::kRBrace)); break;
                case ',': tokens.push_back(MakeSingleCharToken(TokenKind::kComma)); break;
                case ':': tokens.push_back(MakeSingleCharToken(TokenKind::kColon)); break;
                case ';': tokens.push_back(MakeSingleCharToken(TokenKind::kSemicolon)); break;
                case '=': tokens.push_back(MakeSingleCharToken(TokenKind::kEqual)); break;
                case '-': tokens.push_back(LexArrow()); break;
                case '"': tokens.push_back(LexString()); break;
                default:
                    throw ParserError(location, absl::StrFormat("unexpected character '%c'", ch));
            }
        }

        tokens.push_back(Token{TokenKind::kEndOfFile, std::string(), location_});
        return tokens;
    }

private:
    bool AtEnd() const { return index_ >= source_.size(); }

    char Peek() const { return AtEnd() ? '\0' : source_[index_]; }

    char PeekNext() const {
        return (index_ + 1) < source_.size() ? source_[index_ + 1] : '\0';
    }

    char Advance() {
        const char ch = source_[index_++];
        if (ch == '\n') {
            ++location_.line;
            location_.column = 1;
        } else {
            ++location_.column;
        }
        return ch;
    }

    void SkipWhitespaceAndComments() {
        while (!AtEnd()) {
            if (std::isspace(static_cast<unsigned char>(Peek()))) {
                Advance();
                continue;
            }

            if (Peek() == '/' && PeekNext() == '/') {
                while (!AtEnd() && Peek() != '\n') {
                    Advance();
                }
                continue;
            }

            break;
        }
    }

    Token MakeSingleCharToken(TokenKind kind) {
        const SourceLocation location = location_;
        const char ch = Advance();
        return Token{kind, std::string(1, ch), location};
    }

    Token LexIdentifierOrKeyword() {
        const SourceLocation location = location_;
        std::string text;
        while (!AtEnd()) {
            const char ch = Peek();
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                break;
            }
            text.push_back(Advance());
        }

        if (text == "as") {
            return Token{TokenKind::kAs, text, location};
        }
        if (text == "async") {
            return Token{TokenKind::kAsync, text, location};
        }
        if (text == "local") {
            return Token{TokenKind::kLocal, text, location};
        }
        if (text == "then") {
            return Token{TokenKind::kThen, text, location};
        }
        if (text == "end") {
            return Token{TokenKind::kEnd, text, location};
        }
        return Token{TokenKind::kIdentifier, text, location};
    }

    Token LexNumber() {
        const SourceLocation location = location_;
        std::string text;
        while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
            text.push_back(Advance());
        }
        return Token{TokenKind::kNumber, text, location};
    }

    Token LexImportKeyword() {
        const SourceLocation location = location_;
        Advance();

        std::string keyword;
        while (!AtEnd()) {
            const char ch = Peek();
            if (!std::isalpha(static_cast<unsigned char>(ch))) {
                break;
            }
            keyword.push_back(Advance());
        }

        if (keyword != "import") {
            throw ParserError(location, absl::StrFormat("unknown directive '@%s'", keyword));
        }
        return Token{TokenKind::kImport, "@import", location};
    }

    Token LexArrow() {
        const SourceLocation location = location_;
        Advance();
        if (Peek() != '>') {
            throw ParserError(location, "expected '->'");
        }
        Advance();
        return Token{TokenKind::kArrow, "->", location};
    }

    Token LexString() {
        const SourceLocation location = location_;
        Advance();

        std::string text;
        while (!AtEnd()) {
            const char ch = Advance();
            if (ch == '"') {
                return Token{TokenKind::kString, text, location};
            }

            if (ch == '\\') {
                if (AtEnd()) {
                    throw ParserError(location, "unterminated escape sequence");
                }

                const char escaped = Advance();
                switch (escaped) {
                    case 'n': text.push_back('\n'); break;
                    case 'r': text.push_back('\r'); break;
                    case 't': text.push_back('\t'); break;
                    case '"': text.push_back('"'); break;
                    case '\\': text.push_back('\\'); break;
                    default:
                        throw ParserError(location, absl::StrFormat("unsupported escape '\\%c'", escaped));
                }
                continue;
            }

            if (ch == '\n') {
                throw ParserError(location, "unterminated string literal");
            }
            text.push_back(ch);
        }

        throw ParserError(location, "unterminated string literal");
    }

    std::string source_;
    std::size_t index_ = 0;
    SourceLocation location_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Program ParseProgram() {
        Program program;
        while (!Check(TokenKind::kEndOfFile)) {
            if (Match(TokenKind::kImport)) {
                program.imports.push_back(ParseImportDeclaration());
                continue;
            }

            if (IsFunctionDeclarationStart()) {
                program.functions.push_back(ParseFunctionDeclaration());
                continue;
            }

            program.top_level_statements.push_back(ParseStatement());
            ConsumeOptionalSemicolon();
        }
        return program;
    }

private:
    bool Check(TokenKind kind) const {
        return Peek().kind == kind;
    }

    bool Match(TokenKind kind) {
        if (!Check(kind)) {
            return false;
        }
        Advance();
        return true;
    }

    const Token& Peek() const {
        return tokens_[index_];
    }

    const Token& Previous() const {
        return tokens_[index_ - 1];
    }

    const Token& PeekAt(std::size_t offset) const {
        const std::size_t target = index_ + offset;
        if (target >= tokens_.size()) {
            return tokens_.back();
        }
        return tokens_[target];
    }

    const Token& Advance() {
        if (!Check(TokenKind::kEndOfFile)) {
            ++index_;
        }
        return Previous();
    }

    const Token& Expect(TokenKind kind, const std::string& message) {
        if (!Check(kind)) {
            throw ParserError(Peek().location, message);
        }
        return Advance();
    }

    void ConsumeOptionalSemicolon() {
        Match(TokenKind::kSemicolon);
    }

    bool IsFunctionDeclarationStart() const {
        std::size_t offset = 0;
        if (PeekAt(offset).kind == TokenKind::kAsync) {
            ++offset;
        }

        return PeekAt(offset).kind == TokenKind::kIdentifier
            && PeekAt(offset + 1).kind == TokenKind::kLParen
            && PeekAt(offset + 2).kind == TokenKind::kRParen
            && PeekAt(offset + 3).kind == TokenKind::kArrow
            && PeekAt(offset + 4).kind == TokenKind::kThen;
    }

    ImportDeclaration ParseImportDeclaration() {
        ImportDeclaration import_decl;
        import_decl.location = Previous().location;
        Expect(TokenKind::kLParen, "expected '(' after @import");
        import_decl.module_name = Expect(TokenKind::kString, "expected module name string").text;
        Expect(TokenKind::kRParen, "expected ')' after module name");
        Expect(TokenKind::kAs, "expected 'as' after import declaration");
        import_decl.alias = Expect(TokenKind::kIdentifier, "expected import alias").text;
        ConsumeOptionalSemicolon();
        return import_decl;
    }

    FunctionDeclaration ParseFunctionDeclaration() {
        FunctionDeclaration function;
        function.is_async = Match(TokenKind::kAsync);
        function.location = Peek().location;
        function.name = Expect(TokenKind::kIdentifier, "expected function name").text;
        Expect(TokenKind::kLParen, "expected '(' after function name");
        Expect(TokenKind::kRParen, "expected ')' after function name");
        Expect(TokenKind::kArrow, "expected '->' after function signature");
        Expect(TokenKind::kThen, "expected 'then' after '->'");
        Expect(TokenKind::kLBrace, "expected '{' to start function body");
        while (!Check(TokenKind::kRBrace) && !Check(TokenKind::kEndOfFile)) {
            function.body.push_back(ParseStatement());
            ConsumeOptionalSemicolon();
        }
        Expect(TokenKind::kRBrace, "expected '}' after function body");
        Expect(TokenKind::kEnd, "expected 'end' after function body");
        ConsumeOptionalSemicolon();
        return function;
    }

    Statement ParseStatement() {
        Statement statement;
        statement.location = Peek().location;
        if (Match(TokenKind::kLocal)) {
            statement.kind = StatementKind::kLocalAssignment;
            statement.local_name = Expect(TokenKind::kIdentifier, "expected local variable name").text;
            Expect(TokenKind::kEqual, "expected '=' after local variable name");
            statement.value = ParseExpression();
            return statement;
        }

        statement.kind = StatementKind::kExpression;
        statement.value = ParseExpression();
        return statement;
    }

    std::unique_ptr<Expression> ParseExpression() {
        return ParsePrimaryExpression();
    }

    std::unique_ptr<Expression> ParsePrimaryExpression() {
        if (Match(TokenKind::kNumber)) {
            auto expression = std::make_unique<Expression>();
            expression->kind = ExpressionKind::kNumber;
            expression->location = Previous().location;
            expression->number_value = std::stoll(Previous().text);
            return expression;
        }

        if (Match(TokenKind::kString)) {
            auto expression = std::make_unique<Expression>();
            expression->kind = ExpressionKind::kString;
            expression->location = Previous().location;
            expression->string_value = Previous().text;
            return expression;
        }

        if (Match(TokenKind::kLBrace)) {
            return ParseMapExpression(Previous().location);
        }

        if (Match(TokenKind::kIdentifier)) {
            const Token identifier = Previous();
            if (Match(TokenKind::kColon)) {
                const Token callee = Expect(TokenKind::kIdentifier, "expected method name after ':'");
                return ParseCallExpression(identifier.location, identifier.text, callee.text);
            }

            if (Check(TokenKind::kLParen)) {
                return ParseCallExpression(identifier.location, std::nullopt, identifier.text);
            }

            auto expression = std::make_unique<Expression>();
            expression->kind = ExpressionKind::kIdentifier;
            expression->location = identifier.location;
            expression->identifier_name = identifier.text;
            return expression;
        }

        throw ParserError(Peek().location, "expected expression");
    }

    std::unique_ptr<Expression> ParseMapExpression(const SourceLocation& location) {
        auto expression = std::make_unique<Expression>();
        expression->kind = ExpressionKind::kMap;
        expression->location = location;

        if (!Check(TokenKind::kRBrace)) {
            do {
                std::string key;
                if (Match(TokenKind::kString) || Match(TokenKind::kIdentifier)) {
                    key = Previous().text;
                } else {
                    throw ParserError(Peek().location, "expected string or identifier key in object literal");
                }

                Expect(TokenKind::kColon, "expected ':' after object literal key");

                MapEntry entry;
                entry.key = std::move(key);
                entry.value = ParseExpression();
                expression->map_entries.push_back(std::move(entry));
            } while (Match(TokenKind::kComma));
        }

        Expect(TokenKind::kRBrace, "expected '}' after object literal");
        return expression;
    }

    std::unique_ptr<Expression> ParseCallExpression(
        const SourceLocation& location,
        std::optional<std::string> receiver,
        std::string callee) {
        auto expression = std::make_unique<Expression>();
        expression->kind = ExpressionKind::kCall;
        expression->location = location;
        expression->receiver_name = std::move(receiver);
        expression->callee_name = std::move(callee);

        Expect(TokenKind::kLParen, "expected '(' after callable name");
        if (!Check(TokenKind::kRParen)) {
            do {
                CallArgument argument;
                if (Check(TokenKind::kIdentifier) && PeekAt(1).kind == TokenKind::kColon) {
                    argument.name = Advance().text;
                    Expect(TokenKind::kColon, "expected ':' after named argument");
                }
                argument.value = ParseExpression();
                expression->call_arguments.push_back(std::move(argument));
            } while (Match(TokenKind::kComma));
        }
        Expect(TokenKind::kRParen, "expected ')' after call arguments");
        return expression;
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
};

struct RuntimeValue {
    using Object = absl::flat_hash_map<std::string, RuntimeValue>;

    std::variant<std::monostate, std::int64_t, std::string, std::shared_ptr<Object>> data;

    RuntimeValue() = default;
    explicit RuntimeValue(std::int64_t value) : data(value) {}
    explicit RuntimeValue(std::string value) : data(std::move(value)) {}
    explicit RuntimeValue(std::shared_ptr<Object> value) : data(std::move(value)) {}

    bool IsNull() const { return std::holds_alternative<std::monostate>(data); }

    std::string ToString() const {
        if (const auto* integer = std::get_if<std::int64_t>(&data)) {
            return std::to_string(*integer);
        }
        if (const auto* text = std::get_if<std::string>(&data)) {
            return *text;
        }
        if (std::holds_alternative<std::shared_ptr<Object>>(data)) {
            return "{object}";
        }
        return "null";
    }
};

class XerRuntime {
public:
    explicit XerRuntime(const Program& program) : program_(program) {
        module_aliases_.emplace("console", "console");
    }

    void Execute() {
        for (const auto& import_decl : program_.imports) {
            RegisterImport(import_decl);
        }

        for (const auto& function : program_.functions) {
            functions_.emplace(function.name, &function);
        }

        scopes_.emplace_back();
        for (const auto& statement : program_.top_level_statements) {
            ExecuteStatement(statement);
        }
    }

private:
    void RegisterImport(const ImportDeclaration& import_decl) {
        std::string module_name = import_decl.module_name;
        absl::AsciiStrToLower(&module_name);
        if (module_name != "timer") {
            throw RuntimeError(import_decl.location,
                               absl::StrFormat("unsupported module '%s'", import_decl.module_name));
        }

        module_aliases_[import_decl.alias] = module_name;
    }

    void ExecuteStatement(const Statement& statement) {
        RuntimeValue value = EvaluateExpression(*statement.value);
        if (statement.kind == StatementKind::kLocalAssignment) {
            scopes_.back()[statement.local_name] = std::move(value);
        }
    }

    RuntimeValue EvaluateExpression(const Expression& expression) {
        switch (expression.kind) {
            case ExpressionKind::kNumber:
                return RuntimeValue(expression.number_value);
            case ExpressionKind::kString:
                return RuntimeValue(expression.string_value);
            case ExpressionKind::kIdentifier:
                return LookupVariable(expression.location, expression.identifier_name);
            case ExpressionKind::kMap:
                return EvaluateMap(expression);
            case ExpressionKind::kCall:
                return EvaluateCall(expression);
        }

        throw RuntimeError(expression.location, "unsupported expression kind");
    }

    RuntimeValue EvaluateMap(const Expression& expression) {
        auto object = std::make_shared<RuntimeValue::Object>();
        for (const auto& entry : expression.map_entries) {
            (*object)[entry.key] = EvaluateExpression(*entry.value);
        }
        return RuntimeValue(std::move(object));
    }

    RuntimeValue EvaluateCall(const Expression& expression) {
        if (expression.receiver_name.has_value()) {
            const std::string receiver = *expression.receiver_name;
            const auto module_it = module_aliases_.find(receiver);
            if (module_it == module_aliases_.end()) {
                throw RuntimeError(expression.location,
                                   absl::StrFormat("unknown receiver '%s'", receiver));
            }
            return ExecuteModuleCall(expression.location, module_it->second, expression.callee_name, expression.call_arguments);
        }

        const auto function_it = functions_.find(expression.callee_name);
        if (function_it == functions_.end()) {
            throw RuntimeError(expression.location,
                               absl::StrFormat("unknown function '%s'", expression.callee_name));
        }
        if (!expression.call_arguments.empty()) {
            throw RuntimeError(expression.location,
                               absl::StrFormat("function '%s' does not accept arguments", expression.callee_name));
        }

        scopes_.emplace_back();
        for (const auto& statement : function_it->second->body) {
            ExecuteStatement(statement);
        }
        scopes_.pop_back();
        return RuntimeValue();
    }

    RuntimeValue ExecuteModuleCall(
        const SourceLocation& location,
        const std::string& module_name,
        const std::string& method_name,
        const std::vector<CallArgument>& arguments) {
        if (module_name == "timer") {
            if (method_name == "now") {
                if (!arguments.empty()) {
                    throw RuntimeError(location, "Timer:now() does not accept arguments");
                }
                const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now());
                return RuntimeValue(now.time_since_epoch().count());
            }

            if (method_name == "sleep") {
                const std::int64_t timeout_ms = ResolveIntegerArgument(location, arguments, "timeout", 0);
                if (timeout_ms < 0) {
                    throw RuntimeError(location, "Timer:sleep(timeout: ...) requires a non-negative timeout");
                }
                SleepFor(std::max<std::int64_t>(0, timeout_ms));
                return RuntimeValue();
            }
        }

        if (module_name == "console" && method_name == "log") {
            return ExecuteConsoleLog(location, arguments);
        }

        throw RuntimeError(location,
                           absl::StrFormat("unknown module call '%s:%s'", module_name, method_name));
    }

    RuntimeValue ExecuteConsoleLog(const SourceLocation& location, const std::vector<CallArgument>& arguments) {
        if (arguments.empty() || arguments.size() > 2) {
            throw RuntimeError(location, "console:log expects format string and optional placeholder object");
        }

        RuntimeValue format_value = EvaluateExpression(*arguments[0].value);
        const auto* format_text = std::get_if<std::string>(&format_value.data);
        if (format_text == nullptr) {
            throw RuntimeError(location, "console:log format must be a string");
        }

        std::string output = *format_text;
        if (arguments.size() == 2) {
            RuntimeValue replacements_value = EvaluateExpression(*arguments[1].value);
            const auto* object = std::get_if<std::shared_ptr<RuntimeValue::Object>>(&replacements_value.data);
            if (object == nullptr || !*object) {
                throw RuntimeError(location, "console:log placeholders must be an object literal");
            }

            std::vector<std::pair<std::string, std::string>> replacements;
            const auto replacement_view = (**object) | ranges::views::transform([](const auto& entry) {
                return std::pair<std::string, std::string>(
                    absl::StrCat("%", entry.first, "%"),
                    entry.second.ToString());
            });
            for (const auto& replacement : replacement_view) {
                replacements.push_back(replacement);
            }

            output = absl::StrReplaceAll(output, replacements);
        }

		XER::Flux::Terminal::WriteLine(XER::Flux::Terminal::OutputStream::kStdout, output);
        return RuntimeValue();
    }

    RuntimeValue LookupVariable(const SourceLocation& location, const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto variable_it = it->find(name);
            if (variable_it != it->end()) {
                return variable_it->second;
            }
        }

        throw RuntimeError(location, absl::StrFormat("unknown variable '%s'", name));
    }

    std::int64_t ResolveIntegerArgument(
        const SourceLocation& location,
        const std::vector<CallArgument>& arguments,
        const std::string& name,
        std::size_t positional_index) {
        for (const auto& argument : arguments) {
            if (argument.name.has_value() && *argument.name == name) {
                return RequireInteger(location, EvaluateExpression(*argument.value), name);
            }
        }

        if (positional_index < arguments.size()) {
            return RequireInteger(location, EvaluateExpression(*arguments[positional_index].value), name);
        }

        throw RuntimeError(location,
                           absl::StrFormat("missing integer argument '%s'", name));
    }

    std::int64_t RequireInteger(
        const SourceLocation& location,
        const RuntimeValue& value,
        std::string_view argument_name) const {
        if (const auto* integer = std::get_if<std::int64_t>(&value.data)) {
            return *integer;
        }

        throw RuntimeError(location,
                           absl::StrFormat("argument '%s' must be an integer", argument_name));
    }

    void SleepFor(std::int64_t timeout_ms) {
        struct SleepState {
            bool fired = false;
        } state;

        uv_loop_t loop;
        if (uv_loop_init(&loop) != 0) {
            throw std::runtime_error("uv_loop_init failed");
        }

        uv_timer_t timer;
        timer.data = &state;
        uv_timer_init(&loop, &timer);
        uv_timer_start(
            &timer,
            [](uv_timer_t* handle) {
                auto* sleep_state = static_cast<SleepState*>(handle->data);
                sleep_state->fired = true;
                uv_timer_stop(handle);
                uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
            },
            static_cast<uint64_t>(timeout_ms),
            0);

        while (!state.fired) {
            uv_run(&loop, UV_RUN_DEFAULT);
        }

        uv_run(&loop, UV_RUN_DEFAULT);
        uv_loop_close(&loop);
    }

    const Program& program_;
    absl::flat_hash_map<std::string, std::string> module_aliases_;
    absl::flat_hash_map<std::string, const FunctionDeclaration*> functions_;
    std::vector<absl::flat_hash_map<std::string, RuntimeValue>> scopes_;
};

std::string ReadWholeFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error(absl::StrFormat("unable to open '%s'", path));
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string NormalizeSource(std::string source) {
    if (source.size() >= 3
        && static_cast<unsigned char>(source[0]) == 0xEF
        && static_cast<unsigned char>(source[1]) == 0xBB
        && static_cast<unsigned char>(source[2]) == 0xBF) {
        source.erase(0, 3);
    }

#if ENGINE_HAS_ICU
    UErrorCode status = U_ZERO_ERROR;
    const icu::Normalizer2* normalizer = icu::Normalizer2::getNFCInstance(status);
    if (U_FAILURE(status) || normalizer == nullptr) {
        throw std::runtime_error("ICU normalizer initialization failed");
    }

    icu::UnicodeString unicode_source = icu::UnicodeString::fromUTF8(source);
    icu::UnicodeString normalized;
    normalizer->normalize(unicode_source, normalized, status);
    if (U_FAILURE(status)) {
        throw std::runtime_error("ICU source normalization failed");
    }

    std::string normalized_utf8;
    normalized.toUTF8String(normalized_utf8);
    return normalized_utf8;
#else
    return source;
#endif
}

std::string ReadRuntimeSource(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    absl::AsciiStrToLower(&extension);

    if (extension == ".bin") {
        std::string protection_error;
        const Xer::XerProtectionOptions protection = Xer::ResolveProtectionOptionsFromEnvironment(&protection_error);
        if (!protection_error.empty()) {
            throw std::runtime_error(protection_error);
        }

        std::string buffer_error;
        const Xer::XerBuffer encoded = Xer::XerBuffer::FromFile(path, &buffer_error);
        if (!buffer_error.empty()) {
            throw std::runtime_error(buffer_error);
        }

        Xer::XerEncode encoder;
        std::string decode_error;
        Xer::XerBuffer decoded = encoder.Decode(encoded, protection, &decode_error);
        if (!decode_error.empty()) {
            throw std::runtime_error(decode_error);
        }
        return NormalizeSource(decoded.ToString());
    }

    return NormalizeSource(ReadWholeFile(path));
}

void EnsureProtectedArtifacts(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    absl::AsciiStrToLower(&extension);
    if (extension != ".xer") {
        return;
    }

    std::string protection_error;
    const Xer::XerProtectionOptions protection = Xer::ResolveProtectionOptionsFromEnvironment(&protection_error);
    if (!protection_error.empty()) {
        throw std::runtime_error(protection_error);
    }

    Xer::XerEncode encoder;
    std::string compile_error;
    const Xer::XerEncodedBlock block = encoder.Compile(path, protection, &compile_error);
    if (!compile_error.empty()) {
        throw std::runtime_error(compile_error);
    }

    std::string write_error;
    if (!encoder.WriteArtifacts(path, block, std::filesystem::path(), &write_error)) {
        throw std::runtime_error(write_error.empty() ? "failed to write .bin/.bak artifacts" : write_error);
    }
}

}  // namespace

XerScript::XerScript(std::string path) : path_(std::move(path)) {}

bool XerScript::Supports(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    absl::AsciiStrToLower(&extension);
    return extension == ".xer" || extension == ".bin";
}

bool XerScript::Run() const {
    std::string error;
    const bool ok = Run(&error);
    if (!ok && !error.empty()) {
		XER::Flux::Terminal::WriteLine(XER::Flux::Terminal::OutputStream::kStderr, error);
    }
    return ok;
}

bool XerScript::Run(std::string* error_out) const {
    try {
        EnsureProtectedArtifacts(path_);
        const std::string source = ReadRuntimeSource(path_);
        Lexer lexer(source);
        Parser parser(lexer.Tokenize());
        const Program program = parser.ParseProgram();
        XerRuntime runtime(program);
        runtime.Execute();
        return true;
    } catch (const std::exception& error) {
        if (error_out != nullptr) {
            *error_out = absl::StrFormat("XER runtime error in %s: %s", path_, error.what());
        }
        return false;
    }
}