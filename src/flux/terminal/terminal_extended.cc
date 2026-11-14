#include "terminal_extended.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace XER::Flux::Terminal {

// ProgressBar implementation
ProgressBar::ProgressBar(const std::string& label, int total, const ProgressBarStyle& style)
    : label_(label), current_(0), total_(total), style_(style) {
}

void ProgressBar::Update(int current) {
    current_ = current;
    std::cout << "\r" << Render() << std::flush;
}

void ProgressBar::Finish() {
    current_ = total_;
    std::cout << "\r" << Render() << std::endl;
}

void ProgressBar::Reset() {
    current_ = 0;
}

std::string ProgressBar::Render() const {
    std::stringstream ss;
    
    if (style_.show_label) {
        ss << label_ << ": ";
    }
    
    float percentage = static_cast<float>(current_) / total_;
    int filled = static_cast<int>(percentage * style_.width);
    
    if (style_.background_color != TerminalColor::kDefault) {
        ss << BackgroundSequence(style_.background_color);
    }
    
    ss << ForegroundSequence(style_.bar_color);
    
    for (int i = 0; i < style_.width; ++i) {
        if (i < filled) {
            ss << style_.fill_char;
        } else {
            ss << style_.empty_char;
        }
    }
    
    ss << ResetColorSequence();
    
    if (style_.show_percentage) {
        ss << " " << static_cast<int>(percentage * 100) << "%";
    }
    
    return ss.str();
}

// Table implementation
Table::Table(const TableStyle& style)
    : style_(style) {
}

void Table::SetHeader(const std::vector<std::string>& headers) {
    headers_ = headers;
}

void Table::AddRow(const std::vector<std::string>& row) {
    rows_.push_back(row);
}

void Table::Clear() {
    headers_.clear();
    rows_.clear();
}

std::string Table::Render() const {
    std::stringstream ss;
    
    if (headers_.empty() && rows_.empty()) {
        return "";
    }
    
    std::vector<int> widths = CalculateColumnWidths();
    
    if (style_.show_borders) {
        ss << RenderSeparator(widths) << "\n";
    }
    
    if (style_.show_header && !headers_.empty()) {
        ss << ForegroundSequence(style_.header_color);
        ss << RenderRow(headers_, widths);
        ss << ResetColorSequence();
        
        if (style_.show_borders) {
            ss << "\n" << RenderSeparator(widths);
        }
    }
    
    for (const auto& row : rows_) {
        ss << "\n";
        if (style_.row_color != TerminalColor::kDefault) {
            ss << ForegroundSequence(style_.row_color);
        }
        ss << RenderRow(row, widths);
        if (style_.row_color != TerminalColor::kDefault) {
            ss << ResetColorSequence();
        }
    }
    
    if (style_.show_borders) {
        ss << "\n" << RenderSeparator(widths);
    }
    
    return ss.str();
}

std::vector<int> Table::CalculateColumnWidths() const {
    std::vector<int> widths;
    
    // Initialize with header widths
    for (const auto& header : headers_) {
        widths.push_back(static_cast<int>(header.length()) + style_.padding * 2);
    }
    
    // Expand for row content
    for (const auto& row : rows_) {
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            int content_width = static_cast<int>(row[i].length()) + style_.padding * 2;
            if (content_width > widths[i]) {
                widths[i] = content_width;
            }
        }
    }
    
    return widths;
}

std::string Table::RenderRow(const std::vector<std::string>& row, const std::vector<int>& widths) const {
    std::stringstream ss;
    
    for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
        if (style_.show_borders) {
            ss << style_.vertical_char;
        }
        
        std::string padding(style_.padding, ' ');
        ss << padding;
        
        int content_width = widths[i] - style_.padding * 2;
        std::string content = row[i];
        
        if (static_cast<int>(content.length()) > content_width) {
            content = content.substr(0, content_width - 3) + "...";
        }
        
        ss << std::left << std::setw(content_width) << content;
        ss << padding;
    }
    
    if (style_.show_borders) {
        ss << style_.vertical_char;
    }
    
    return ss.str();
}

std::string Table::RenderSeparator(const std::vector<int>& widths) const {
    std::stringstream ss;
    
    for (int width : widths) {
        ss << style_.corner_char;
        ss << std::string(width, style_.horizontal_char);
    }
    
    ss << style_.corner_char;
    
    return ss.str();
}

// Menu implementation
Menu::Menu(const std::string& title, const std::vector<MenuOption>& options)
    : title_(title), options_(options) {
}

void Menu::SetTitle(const std::string& title) {
    title_ = title;
}

void Menu::AddOption(const MenuOption& option) {
    options_.push_back(option);
}

void Menu::ClearOptions() {
    options_.clear();
}

int Menu::Display() const {
    return DisplayWithPrompt("Select an option: ");
}

int Menu::DisplayWithPrompt(const std::string& prompt) const {
    std::cout << Box::Title(title_) << "\n\n";
    
    for (size_t i = 0; i < options_.size(); ++i) {
        const auto& option = options_[i];
        
        if (!option.enabled) {
            std::cout << ColorUtils::Dim(std::to_string(i + 1) + ". " + option.label) << "\n";
        } else {
            std::cout << ColorUtils::Green(std::to_string(i + 1) + ". " + option.label);
            
            if (option.shortcut != '\0') {
                std::cout << " [" << option.shortcut << "]";
            }
            
            std::cout << "\n";
            
            if (!option.description.empty()) {
                std::cout << "   " << ColorUtils::Dim(option.description) << "\n";
            }
        }
    }
    
    std::cout << "\n" << prompt;
    
    std::string input;
    std::getline(std::cin, input);
    
    try {
        int choice = std::stoi(input);
        if (choice >= 1 && choice <= static_cast<int>(options_.size())) {
            if (options_[choice - 1].enabled) {
                return choice - 1;
            }
        }
    } catch (...) {
        // Check for shortcut
        if (input.length() == 1) {
            char c = input[0];
            for (size_t i = 0; i < options_.size(); ++i) {
                if (options_[i].shortcut == c && options_[i].enabled) {
                    return static_cast<int>(i);
                }
            }
        }
    }
    
    return -1;
}

// CommandRegistry implementation
void CommandRegistry::Register(const Command& command) {
    commands_[command.name] = command;
}

void CommandRegistry::Unregister(const std::string& name) {
    commands_.erase(name);
}

bool CommandRegistry::Exists(const std::string& name) const {
    return commands_.find(name) != commands_.end();
}

CommandRegistry::Command CommandRegistry::Get(const std::string& name) const {
    auto it = commands_.find(name);
    if (it != commands_.end()) {
        return it->second;
    }
    return Command{};
}

std::vector<CommandRegistry::Command> CommandRegistry::GetAll() const {
    std::vector<Command> commands;
    for (const auto& pair : commands_) {
        commands.push_back(pair.second);
    }
    return commands;
}

std::vector<CommandRegistry::Command> CommandRegistry::MatchingPrefix(const std::string& prefix) const {
    std::vector<Command> matches;
    for (const auto& pair : commands_) {
        if (pair.first.find(prefix) == 0) {
            matches.push_back(pair.second);
        }
    }
    return matches;
}

void CommandRegistry::Execute(const std::string& name, const std::vector<std::string>& args) const {
    auto it = commands_.find(name);
    if (it != commands_.end()) {
        it->second.handler(args);
    }
}

// Autocomplete implementation
Autocomplete::Autocomplete() {
}

void Autocomplete::AddWord(const std::string& word) {
    words_.push_back(word);
}

void Autocomplete::AddWords(const std::vector<std::string>& words) {
    words_.insert(words_.end(), words.begin(), words.end());
}

void Autocomplete::Clear() {
    words_.clear();
}

std::vector<std::string> Autocomplete::Complete(const std::string& prefix, int max_suggestions) const {
    std::vector<std::string> matches;
    
    for (const auto& word : words_) {
        if (word.find(prefix) == 0) {
            matches.push_back(word);
            if (matches.size() >= static_cast<size_t>(max_suggestions)) {
                break;
            }
        }
    }
    
    return matches;
}

std::string Autocomplete::GetCommonPrefix(const std::string& prefix) const {
    auto matches = Complete(prefix, 0);
    
    if (matches.empty()) {
        return prefix;
    }
    
    if (matches.size() == 1) {
        return matches[0];
    }
    
    std::string common = matches[0];
    for (size_t i = 1; i < matches.size(); ++i) {
        size_t j = 0;
        while (j < common.length() && j < matches[i].length() && common[j] == matches[i][j]) {
            ++j;
        }
        common = common.substr(0, j);
        if (common.empty()) {
            break;
        }
    }
    
    return common;
}

// Prompt implementation
std::string Prompt::ReadLine(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

std::string Prompt::ReadPassword(const std::string& prompt) {
    std::cout << prompt;
    
#ifdef _WIN32
    std::string password;
    int c;
    while ((c = _getch()) != '\r') {
        if (c == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b";
            }
        } else {
            password += static_cast<char>(c);
            std::cout << '*';
        }
    }
    std::cout << "\n";
    return password;
#else
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    std::string password;
    std::getline(std::cin, password);
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
    return password;
#endif
}

bool Prompt::ReadYesNo(const std::string& prompt) {
    std::string input = ReadLine(prompt);
    std::transform(input.begin(), input.end(), input.begin(), ::tolower);
    return input == "y" || input == "yes";
}

int Prompt::ReadChoice(const std::string& prompt, const std::vector<std::string>& choices) {
    std::cout << prompt << "\n";
    for (size_t i = 0; i < choices.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << choices[i] << "\n";
    }
    
    std::string input = ReadLine("Choice: ");
    try {
        int choice = std::stoi(input);
        if (choice >= 1 && choice <= static_cast<int>(choices.size())) {
            return choice - 1;
        }
    } catch (...) {
    }
    
    return -1;
}

int Prompt::ReadNumber(const std::string& prompt, int min, int max) {
    while (true) {
        std::string input = ReadLine(prompt);
        try {
            int value = std::stoi(input);
            if (value >= min && value <= max) {
                return value;
            }
            std::cout << "Please enter a number between " << min << " and " << max << "\n";
        } catch (...) {
            std::cout << "Invalid input. Please enter a number.\n";
        }
    }
}

double Prompt::ReadFloat(const std::string& prompt, double min, double max) {
    while (true) {
        std::string input = ReadLine(prompt);
        try {
            double value = std::stod(input);
            if (value >= min && value <= max) {
                return value;
            }
            std::cout << "Please enter a number between " << min << " and " << max << "\n";
        } catch (...) {
            std::cout << "Invalid input. Please enter a number.\n";
        }
    }
}

// ColorUtils implementation
std::string ColorUtils::ApplyColor(std::string_view text, TerminalColor fg, TerminalColor bg) {
    std::string result = ForegroundSequence(fg);
    if (bg != TerminalColor::kDefault) {
        result += BackgroundSequence(bg);
    }
    result += text;
    result += ResetColorSequence();
    return result;
}

std::string ColorUtils::ApplyStyle(std::string_view text, TextStyle style) {
    std::string result = "\x1b[" + std::to_string(static_cast<int>(style)) + "m";
    result += text;
    result += "\x1b[0m";
    return result;
}

std::string ColorUtils::ApplyBoth(std::string_view text, TerminalColor fg, TextStyle style) {
    std::string result = ForegroundSequence(fg);
    result += "\x1b[" + std::to_string(static_cast<int>(style)) + "m";
    result += text;
    result += "\x1b[0m";
    return result;
}

std::string ColorUtils::Reset() {
    return ResetColorSequence();
}

std::string ColorUtils::Red(std::string_view text) {
    return ApplyColor(text, TerminalColor::kRed);
}

std::string ColorUtils::Green(std::string_view text) {
    return ApplyColor(text, TerminalColor::kGreen);
}

std::string ColorUtils::Yellow(std::string_view text) {
    return ApplyColor(text, TerminalColor::kYellow);
}

std::string ColorUtils::Blue(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBlue);
}

std::string ColorUtils::Magenta(std::string_view text) {
    return ApplyColor(text, TerminalColor::kMagenta);
}

std::string ColorUtils::Cyan(std::string_view text) {
    return ApplyColor(text, TerminalColor::kCyan);
}

std::string ColorUtils::White(std::string_view text) {
    return ApplyColor(text, TerminalColor::kWhite);
}

std::string ColorUtils::BrightRed(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightRed);
}

std::string ColorUtils::BrightGreen(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightGreen);
}

std::string ColorUtils::BrightYellow(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightYellow);
}

std::string ColorUtils::BrightBlue(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightBlue);
}

std::string ColorUtils::BrightMagenta(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightMagenta);
}

std::string ColorUtils::BrightCyan(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightCyan);
}

std::string ColorUtils::BrightWhite(std::string_view text) {
    return ApplyColor(text, TerminalColor::kBrightWhite);
}

std::string ColorUtils::Bold(std::string_view text) {
    return ApplyStyle(text, TextStyle::kBold);
}

std::string ColorUtils::Dim(std::string_view text) {
    return ApplyStyle(text, TextStyle::kDim);
}

std::string ColorUtils::Underline(std::string_view text) {
    return ApplyStyle(text, TextStyle::kUnderline);
}

// Screen implementation
void Screen::Clear() {
    std::cout << "\x1b[2J\x1b[H";
}

void Screen::MoveCursor(int row, int col) {
    std::cout << "\x1b[" << row << ";" << col << "H";
}

void Screen::SaveCursor() {
    std::cout << "\x1b[s";
}

void Screen::RestoreCursor() {
    std::cout << "\x1b[u";
}

void Screen::HideCursor() {
    std::cout << "\x1b[?25l";
}

void Screen::ShowCursor() {
    std::cout << "\x1b[?25h";
}

void Screen::MoveUp(int lines) {
    std::cout << "\x1b[" << lines << "A";
}

void Screen::MoveDown(int lines) {
    std::cout << "\x1b[" << lines << "B";
}

void Screen::MoveLeft(int cols) {
    std::cout << "\x1b[" << cols << "D";
}

void Screen::MoveRight(int cols) {
    std::cout << "\x1b[" << cols << "C";
}

void Screen::ClearLine() {
    std::cout << "\x1b[2K";
}

void Screen::ClearToEndOfLine() {
    std::cout << "\x1b[0K";
}

void Screen::ClearToStartOfLine() {
    std::cout << "\x1b[1K";
}

void Screen::ClearScreen() {
    std::cout << "\x1b[2J";
}

void Screen::ClearToEndOfScreen() {
    std::cout << "\x1b[0J";
}

void Screen::ClearToStartOfScreen() {
    std::cout << "\x1b[1J";
}

int Screen::GetWidth() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
#endif
}

int Screen::GetHeight() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_row;
#endif
}

// Box implementation
std::string Box::SingleLine(const std::string& content, int width) {
    if (width == 0) {
        width = static_cast<int>(content.length()) + 4;
    }
    
    std::string line(width, '-');
    std::stringstream ss;
    ss << "+" << line << "+\n";
    ss << "| " << std::left << std::setw(width - 4) << content << " |\n";
    ss << "+" << line << "+";
    return ss.str();
}

std::string Box::DoubleLine(const std::string& content, int width) {
    if (width == 0) {
        width = static_cast<int>(content.length()) + 4;
    }
    
    std::string line(width, '=');
    std::stringstream ss;
    ss << "+" << line << "+\n";
    ss << "| " << std::left << std::setw(width - 4) << content << " |\n";
    ss << "+" << line << "+";
    return ss.str();
}

std::string Box::Rounded(const std::string& content, int width) {
    if (width == 0) {
        width = static_cast<int>(content.length()) + 4;
    }
    
    std::string line(width - 2, '-');
    std::stringstream ss;
    ss << "/" << line << "\\\n";
    ss << "| " << std::left << std::setw(width - 4) << content << " |\n";
    ss << "\\" << line << "/";
    return ss.str();
}

std::string Box::Heavy(const std::string& content, int width) {
    if (width == 0) {
        width = static_cast<int>(content.length()) + 4;
    }
    
    std::string line(width, '#');
    std::stringstream ss;
    ss << "#" << line << "#\n";
    ss << "# " << std::left << std::setw(width - 4) << content << " #\n";
    ss << "#" << line << "#";
    return ss.str();
}

std::string Box::Title(const std::string& title, int width) {
    return ColorUtils::BrightCyan(SingleLine(title, width));
}

std::string Box::Info(const std::string& text, int width) {
    return ColorUtils::Blue(SingleLine("INFO: " + text, width));
}

std::string Box::Warning(const std::string& text, int width) {
    return ColorUtils::Yellow(SingleLine("WARNING: " + text, width));
}

std::string Box::Error(const std::string& text, int width) {
    return ColorUtils::Red(SingleLine("ERROR: " + text, width));
}

std::string Box::Success(const std::string& text, int width) {
    return ColorUtils::Green(SingleLine("SUCCESS: " + text, width));
}

// Spinner implementation
Spinner::Spinner(Style style)
    : style_(style), frame_(0), active_(false) {
    InitializeFrames();
}

void Spinner::InitializeFrames() {
    switch (style_) {
        case Style::kDots:
            frames_ = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            break;
        case Style::kLine:
            frames_ = {"-", "\\", "|", "/"};
            break;
        case Style::kBar:
            frames_ = {"=", ">", "="};
            break;
        case Style::kArrow:
            frames_ = {">", ">>", ">>>"};
            break;
        case Style::kPipe:
            frames_ = {"|", "||", "|||", "||||"};
            break;
    }
}

void Spinner::Start() {
    active_ = true;
    frame_ = 0;
}

void Spinner::Stop() {
    active_ = false;
}

void Spinner::Update() {
    if (!active_) {
        return;
    }
    
    frame_ = (frame_ + 1) % frames_.size();
}

std::string Spinner::GetCurrent() const {
    if (frames_.empty()) {
        return "";
    }
    return frames_[frame_];
}

// ColoredLogger implementation
ColoredLogger::Level ColoredLogger::level_ = ColoredLogger::Level::kInfo;

void ColoredLogger::Debug(const std::string& message) {
    if (level_ <= Level::kDebug) {
        std::cout << ColoredLogger::FormatLogMessage(Level::kDebug, message) << std::endl;
    }
}

void ColoredLogger::Info(const std::string& message) {
    if (level_ <= Level::kInfo) {
        std::cout << ColoredLogger::FormatLogMessage(Level::kInfo, message) << std::endl;
    }
}

void ColoredLogger::Warning(const std::string& message) {
    if (level_ <= Level::kWarning) {
        std::cout << ColoredLogger::FormatLogMessage(Level::kWarning, message) << std::endl;
    }
}

void ColoredLogger::Error(const std::string& message) {
    if (level_ <= Level::kError) {
        std::cout << ColoredLogger::FormatLogMessage(Level::kError, message) << std::endl;
    }
}

void ColoredLogger::Critical(const std::string& message) {
    if (level_ <= Level::kCritical) {
        std::cout << ColoredLogger::FormatLogMessage(Level::kCritical, message) << std::endl;
    }
}

void ColoredLogger::SetLevel(Level level) {
    level_ = level;
}

ColoredLogger::Level ColoredLogger::GetLevel() {
    return level_;
}

std::string ColoredLogger::FormatLogMessage(Level level, const std::string& message) {
    std::string level_str;
    switch (level) {
        case Level::kDebug: level_str = "DEBUG"; break;
        case Level::kInfo: level_str = "INFO"; break;
        case Level::kWarning: level_str = "WARN"; break;
        case Level::kError: level_str = "ERROR"; break;
        case Level::kCritical: level_str = "CRITICAL"; break;
    }
    
    std::string colored = ColorUtils::ApplyColor("[" + level_str + "]", GetLevelColor(level));
    return colored + " " + message;
}

TerminalColor ColoredLogger::GetLevelColor(Level level) {
    switch (level) {
        case Level::kDebug: return TerminalColor::kBrightBlack;
        case Level::kInfo: return TerminalColor::kBlue;
        case Level::kWarning: return TerminalColor::kYellow;
        case Level::kError: return TerminalColor::kRed;
        case Level::kCritical: return TerminalColor::kBrightRed;
        default: return TerminalColor::kDefault;
    }
}

// InteractiveShell implementation
InteractiveShell::InteractiveShell()
    : prompt_("> "), running_(false) {
}

void InteractiveShell::SetPrompt(const std::string& prompt) {
    prompt_ = prompt;
}

void InteractiveShell::SetHistory(std::shared_ptr<History::CommandHistory> history) {
    history_ = history;
}

void InteractiveShell::SetCommandRegistry(std::shared_ptr<CommandRegistry> registry) {
    registry_ = registry;
}

void InteractiveShell::SetAutocomplete(std::shared_ptr<Autocomplete> autocomplete) {
    autocomplete_ = autocomplete;
}

void InteractiveShell::Run() {
    running_ = true;
    
    while (running_) {
        std::string command = ReadCommand();
        
        if (command.empty()) {
            continue;
        }
        
        if (command == "exit" || command == "quit") {
            running_ = false;
            continue;
        }
        
        if (history_) {
            history_->Push(command);
        }
        
        ExecuteCommand(command);
    }
}

void InteractiveShell::Stop() {
    running_ = false;
}

std::string InteractiveShell::ReadCommand() {
    DisplayPrompt();
    
    std::string input;
    std::getline(std::cin, input);
    
    return input;
}

void InteractiveShell::ExecuteCommand(const std::string& command) {
    if (!registry_) {
        std::cout << "No command registry set\n";
        return;
    }
    
    // Parse command and args
    std::istringstream iss(command);
    std::string cmd_name;
    std::vector<std::string> args;
    
    iss >> cmd_name;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }
    
    registry_->Execute(cmd_name, args);
}

void InteractiveShell::DisplayPrompt() {
    std::cout << ColorUtils::Green(prompt_);
}

std::string InteractiveShell::HandleTabCompletion(const std::string& input) {
    if (!autocomplete_) {
        return input;
    }
    
    std::string completed = autocomplete_->GetCommonPrefix(input);
    if (completed != input) {
        return completed;
    }
    
    auto suggestions = autocomplete_->Complete(input, 10);
    if (suggestions.size() > 1) {
        std::cout << "\n";
        for (const auto& suggestion : suggestions) {
            std::cout << "  " << suggestion << "\n";
        }
        DisplayPrompt();
    }
    
    return input;
}

}  // namespace XER::Flux::Terminal
