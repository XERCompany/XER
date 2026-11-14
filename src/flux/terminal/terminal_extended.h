#pragma once

#include "terminal_colors.h"
#include "terminal_interface.h"
#include "../history/command_history.h"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <map>

namespace XER::Flux::Terminal {

// Extended terminal styles
enum class TextStyle {
    kNormal = 0,
    kBold = 1,
    kDim = 2,
    kItalic = 3,
    kUnderline = 4,
    kBlink = 5,
    kReverse = 7,
    kHidden = 8,
    kStrikethrough = 9
};

// Progress bar style
struct ProgressBarStyle {
    TerminalColor bar_color = TerminalColor::kGreen;
    TerminalColor background_color = TerminalColor::kDefault;
    char fill_char = '=';
    char empty_char = '-';
    int width = 40;
    bool show_percentage = true;
    bool show_label = true;
};

// Table style
struct TableStyle {
    TerminalColor header_color = TerminalColor::kBrightCyan;
    TerminalColor border_color = TerminalColor::kBlue;
    TerminalColor row_color = TerminalColor::kDefault;
    char horizontal_char = '-';
    char vertical_char = '|';
    char corner_char = '+';
    bool show_header = true;
    bool show_borders = true;
    int padding = 1;
};

// Menu option
struct MenuOption {
    std::string label;
    std::string description;
    char shortcut = '\0';
    bool enabled = true;
};

// Progress bar
class ProgressBar {
public:
    ProgressBar(const std::string& label, int total, const ProgressBarStyle& style = ProgressBarStyle());
    
    void Update(int current);
    void Finish();
    void Reset();
    
    std::string Render() const;
    
private:
    std::string label_;
    int current_;
    int total_;
    ProgressBarStyle style_;
};

// Table renderer
class Table {
public:
    Table(const TableStyle& style = TableStyle{});
    
    void SetHeader(const std::vector<std::string>& headers);
    void AddRow(const std::vector<std::string>& row);
    void Clear();
    
    std::string Render() const;
    
private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    TableStyle style_;
    
    std::vector<int> CalculateColumnWidths() const;
    std::string RenderRow(const std::vector<std::string>& row, const std::vector<int>& widths) const;
    std::string RenderSeparator(const std::vector<int>& widths) const;
};

// Interactive menu
class Menu {
public:
    Menu(const std::string& title, const std::vector<MenuOption>& options);
    
    void SetTitle(const std::string& title);
    void AddOption(const MenuOption& option);
    void ClearOptions();
    
    int Display() const;
    int DisplayWithPrompt(const std::string& prompt) const;
    
private:
    std::string title_;
    std::vector<MenuOption> options_;
};

// Command registry
class CommandRegistry {
public:
    using CommandHandler = std::function<void(const std::vector<std::string>& args)>;
    
    struct Command {
        std::string name;
        std::string description;
        std::string usage;
        CommandHandler handler;
    };
    
    void Register(const Command& command);
    void Unregister(const std::string& name);
    bool Exists(const std::string& name) const;
    
    Command Get(const std::string& name) const;
    std::vector<Command> GetAll() const;
    std::vector<Command> MatchingPrefix(const std::string& prefix) const;
    
    void Execute(const std::string& name, const std::vector<std::string>& args) const;
    
private:
    std::map<std::string, Command> commands_;
};

// Autocomplete
class Autocomplete {
public:
    Autocomplete();
    
    void AddWord(const std::string& word);
    void AddWords(const std::vector<std::string>& words);
    void Clear();
    
    std::vector<std::string> Complete(const std::string& prefix, int max_suggestions = 10) const;
    std::string GetCommonPrefix(const std::string& prefix) const;
    
private:
    std::vector<std::string> words_;
};

// Interactive prompt
class Prompt {
public:
    static std::string ReadLine(const std::string& prompt = "");
    static std::string ReadPassword(const std::string& prompt = "Password: ");
    static bool ReadYesNo(const std::string& prompt = "Continue? (y/n): ");
    static int ReadChoice(const std::string& prompt, const std::vector<std::string>& choices);
    static int ReadNumber(const std::string& prompt, int min = 0, int max = 100);
    static double ReadFloat(const std::string& prompt, double min = 0.0, double max = 1.0);
};

// Color utilities
class ColorUtils {
public:
    static std::string ApplyColor(std::string_view text, TerminalColor fg, TerminalColor bg = TerminalColor::kDefault);
    static std::string ApplyStyle(std::string_view text, TextStyle style);
    static std::string ApplyBoth(std::string_view text, TerminalColor fg, TextStyle style);
    static std::string Reset();
    
    static std::string Red(std::string_view text);
    static std::string Green(std::string_view text);
    static std::string Yellow(std::string_view text);
    static std::string Blue(std::string_view text);
    static std::string Magenta(std::string_view text);
    static std::string Cyan(std::string_view text);
    static std::string White(std::string_view text);
    static std::string BrightRed(std::string_view text);
    static std::string BrightGreen(std::string_view text);
    static std::string BrightYellow(std::string_view text);
    static std::string BrightBlue(std::string_view text);
    static std::string BrightMagenta(std::string_view text);
    static std::string BrightCyan(std::string_view text);
    static std::string BrightWhite(std::string_view text);
    static std::string Bold(std::string_view text);
    static std::string Dim(std::string_view text);
    static std::string Underline(std::string_view text);
};

// Screen utilities
class Screen {
public:
    static void Clear();
    static void MoveCursor(int row, int col);
    static void SaveCursor();
    static void RestoreCursor();
    static void HideCursor();
    static void ShowCursor();
    static void MoveUp(int lines = 1);
    static void MoveDown(int lines = 1);
    static void MoveLeft(int cols = 1);
    static void MoveRight(int cols = 1);
    static void ClearLine();
    static void ClearToEndOfLine();
    static void ClearToStartOfLine();
    static void ClearScreen();
    static void ClearToEndOfScreen();
    static void ClearToStartOfScreen();
    
    static int GetWidth();
    static int GetHeight();
};

// Box drawing
class Box {
public:
    static std::string SingleLine(const std::string& content, int width = 0);
    static std::string DoubleLine(const std::string& content, int width = 0);
    static std::string Rounded(const std::string& content, int width = 0);
    static std::string Heavy(const std::string& content, int width = 0);
    
    static std::string Title(const std::string& title, int width = 0);
    static std::string Info(const std::string& text, int width = 0);
    static std::string Warning(const std::string& text, int width = 0);
    static std::string Error(const std::string& text, int width = 0);
    static std::string Success(const std::string& text, int width = 0);
};

// Spinner for loading states
class Spinner {
public:
    enum class Style {
        kDots,
        kLine,
        kBar,
        kArrow,
        kPipe
    };
    
    explicit Spinner(Style style = Style::kDots);
    
    void Start();
    void Stop();
    void Update();
    std::string GetCurrent() const;
    
private:
    Style style_;
    int frame_;
    bool active_;
    std::vector<std::string> frames_;
    
    void InitializeFrames();
};

// Logger with colors
class ColoredLogger {
public:
    enum class Level {
        kDebug,
        kInfo,
        kWarning,
        kError,
        kCritical
    };
    
    static void Debug(const std::string& message);
    static void Info(const std::string& message);
    static void Warning(const std::string& message);
    static void Error(const std::string& message);
    static void Critical(const std::string& message);
    
    static void SetLevel(Level level);
    static Level GetLevel();
    
private:
    static Level level_;
    static std::string FormatLogMessage(Level level, const std::string& message);
    static TerminalColor GetLevelColor(Level level);
};

// Interactive shell
class InteractiveShell {
public:
    InteractiveShell();
    
    void SetPrompt(const std::string& prompt);
    void SetHistory(std::shared_ptr<History::CommandHistory> history);
    void SetCommandRegistry(std::shared_ptr<CommandRegistry> registry);
    void SetAutocomplete(std::shared_ptr<Autocomplete> autocomplete);
    
    void Run();
    void Stop();
    
    std::string ReadCommand();
    void ExecuteCommand(const std::string& command);
    
private:
    std::string prompt_;
    std::shared_ptr<History::CommandHistory> history_;
    std::shared_ptr<CommandRegistry> registry_;
    std::shared_ptr<Autocomplete> autocomplete_;
    bool running_;
    
    void DisplayPrompt();
    std::string HandleTabCompletion(const std::string& input);
};

}  // namespace XER::Flux::Terminal
