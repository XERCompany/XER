// code_editor_widget.h — IDEXEJSO's code editor: a custom-drawn
// Widget replacing the plain Textarea, adding tokenizer-based syntax
// highlighting, a line-number gutter, current-line highlight, mouse
// caret placement + drag selection, search-match overlays and
// clipboard shortcuts.
//
// The highlighting half is headless — TokenizeLine()/TokenizeDocument()
// take no widget state so `--selftest` can exercise them without a
// window. Languages are picked by file extension (LanguageForPath):
//   C++   .cc .cpp .cxx .h .hh .hpp .hxx .c
//   JS    .js .mjs .cjs
//   TOML  .toml
//   JSON  .json
//   HTML  .html .htm .xml .xhtml .svg  (with <script>/<style>
//         delegated to the JS/CSS tokenizers per line)
//   CSS   .css
// Everything else renders as plain text.
#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "widgets/widget.h"

namespace XER::IdeXejso {

// ---- headless highlighting -------------------------------------------------

enum class EditorLanguage {
    Plain,
    Cpp,
    JavaScript,
    Toml,
    Json,
    Html,
    Css,
};

EditorLanguage LanguageForPath(const std::string& rel_path);
const char* LanguageName(EditorLanguage lang);

enum class TokKind : int {
    Plain = 0,
    Keyword,
    Type,
    String,
    Comment,
    Number,
    Preproc,
    Punct,
    kCount,
};

// One highlighted run inside a single line — [begin, end) byte offsets
// into that line's text.
struct HighlightSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    TokKind kind = TokKind::Plain;
};

// Cross-line lexer state: the mode the NEXT line starts in. mode 0 is
// normal code; 1 is inside a /* */ block comment; 2 is inside a
// multi-line string — for C++ that is an R"delim(...)delim" raw string
// (aux = delim), for JS a `...` template literal (aux = "`").
struct LineLexState {
    int mode = 0;
    std::string aux;
};

// Tokenizes ONE line. `state` carries the mode INTO the line and is
// updated to the mode at end-of-line, so a document is highlighted by
// seeding a default state and feeding lines in order.
std::vector<HighlightSpan> TokenizeLine(EditorLanguage lang,
                                        std::string_view line,
                                        LineLexState& state);
std::vector<std::vector<HighlightSpan>> TokenizeDocument(
    EditorLanguage lang, const std::vector<std::string>& lines);

// ---- headless editing helpers ------------------------------------------------
// Small pure functions the widget's editing paths are built on; exposed
// (like TokenizeLine) so --selftest can exercise them without a window.

// Detects the file's indent convention from its leading whitespace:
// "\t" when any inspected line opens with a tab, else `spaces` spaces.
std::string DetectIndentUnit(const std::vector<std::string>& lines,
                             int spaces = 4);

// What Enter at `caret_col` in `line` should produce. `indent` is the
// new line's leading whitespace; `extra_line` is set when the split
// lands between an opener and its matching closer (`{|}`) — the caller
// then also emits a closing line indented `closing`.
struct AutoIndent {
    std::string indent;
    bool extra_line = false;
    std::string closing;
};
AutoIndent AutoIndentForNewline(std::string_view line,
                                std::size_t caret_col,
                                EditorLanguage lang,
                                std::string_view indent_unit);

// Auto-pair table: PairedClose('(') == ')' etc.; '\0' when unpaired.
char PairedClose(char open);
// True when typing a pair-capable char at `caret_col` may auto-pair:
// the next char must not be a word char (keeps `(`/`"` from pairing
// inside an identifier run).
bool AutoPairOk(std::string_view line, std::size_t caret_col);

// How a single typed pair char should resolve at `caret_col`:
// `overtype` = caret steps over an already-present closer, `pair` =
// insert `c` + PairedClose(c) and park the caret between. Both false =
// plain insertion.
struct PairPlan {
    bool overtype = false;
    bool pair = false;
};
PairPlan PlanPairInput(std::string_view line, std::size_t caret_col,
                       char c);

// How Backspace at `caret_col` should edit `line`:
//   join_prev  — caret at col 0, merge with the previous line.
//   otherwise  — erase [begin, end) (covers char delete, empty auto-
//                pair delete and leading-whitespace tab stops).
struct BackspacePlan {
    bool join_prev = false;
    std::size_t begin = 0;
    std::size_t end = 0;
};
BackspacePlan PlanBackspace(std::string_view line, std::size_t caret_col,
                            int tab_width = 4);

// One find-bar match, in line/column terms (byte column inside the
// line, len bytes).
struct SearchMatch {
    int line = 0;
    int col = 0;
    int len = 0;
};

// ---- the widget ---------------------------------------------------------------

class CodeEditorWidget final : public XER::UI::Widgets::Widget {
public:
    CodeEditorWidget();

    void SetText(std::string text);
    std::string GetText() const;
    const std::vector<std::string>& Lines() const { return lines_; }
    void SetPlaceholder(std::string placeholder);
    void SetReadOnly(bool read_only);
    void SetLanguage(EditorLanguage lang);
    EditorLanguage Language() const { return language_; }
    void SetOnChanged(std::function<void(const std::string&)> callback);
    // Fired whenever the caret moves (line is 0-based, col a byte col).
    void SetOnCaretChanged(std::function<void(int, int)> callback);

    int CaretLine() const { return caret_line_; }
    int CaretCol() const { return caret_col_; }
    void SetCaret(int line, int col);
    // Moves the caret to (line, col) and scrolls it into view.
    void GotoLine(int line, int col = 0);
    // Selects `len` bytes starting at (line, col) — used by find.
    void SelectRange(int line, int col, int len);
    bool HasSelection() const;
    // Inserts text at the caret (replacing any selection); '\n' splits
    // lines — used by the snippet inserter.
    void InsertTextAtCaret(const std::string& text);

    // Search-match overlay: the matches are painted behind the text;
    // `active` gets a stronger color (the current find result).
    void SetSearchMatches(std::vector<SearchMatch> matches,
                          int active = -1);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {120.0f, 60.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {400.0f, 300.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct UndoEntry {
        std::vector<std::string> lines;
        int caret_line = 0;
        int caret_col = 0;
    };

    // Editing primitives (all notify + invalidate via Changed()).
    void PushUndo();
    bool Undo();
    bool Redo();
    void InsertBytes(std::string_view bytes);
    void InsertNewline();
    void Backspace();
    void DeleteForward();
    bool DeleteSelection();
    std::string SelectedText() const;
    std::pair<int, int> SelFrom() const;   // ordered (line,col) min
    std::pair<int, int> SelTo() const;     // ordered (line,col) max
    bool HandleShortcut(XER::UI::Widgets::KeyCode key, bool shift);

    // Editing UX: indent/dedent the touched lines (Tab / Shift+Tab),
    // line-comment toggling (Ctrl+/), auto-pairs and selection wrap.
    std::string IndentUnit() const;
    void IndentLines(bool dedent);
    void ToggleComment();
    bool HandlePairInput(char c);      // true when the char was consumed
    void WrapSelection(char open);
    TokKind TokenKindAtCaret() const;

    void MoveHorizontal(int dir, bool extend);
    void MoveVertical(int dir, bool extend);
    void MoveLineStartEnd(bool end, bool extend);
    void MovePage(int dir, bool extend);
    void ClampCaret();
    void AfterCaretMove();
    std::size_t LineByteClamp(int line, int col) const;
    void EnsureCaretVisible();

    // Geometry — metrics captured in Render() so event-time code shares
    // the same values that were painted.
    float TextX() const { return pad_ + gutter_w_; }
    int VisibleLines() const;
    float MaxScrollX() const;
    float MaxScrollY() const;
    void ScrollVTo(float y);   // scrollbar-drag helpers
    void ScrollHTo(float x);
    void ResolveClick(XER::UI::Layout::Point position, bool extend,
                      XER::UI::Rendering::Renderer& renderer);
    void SelectWordAt(int line, int col);
    void SelectLineAt(int line);
    std::pair<int, int> PositionAt(XER::UI::Layout::Point position,
                                   XER::UI::Rendering::Renderer& renderer)
        const;
    float ColumnWidth(XER::UI::Rendering::Renderer& renderer, int line,
                      int col) const;
    void Retokenize();
    void Changed();

    std::vector<std::string> lines_{std::string{}};
    std::vector<std::vector<HighlightSpan>> spans_;
    std::string placeholder_;
    EditorLanguage language_ = EditorLanguage::Plain;
    int caret_line_ = 0;
    int caret_col_ = 0;
    int anchor_line_ = 0;    // selection anchor; == caret when collapsed
    int anchor_col_ = 0;
    int preferred_col_ = -1; // vertical-move memory (byte col)
    float scroll_line_ = 0.0f;
    float scroll_x_ = 0.0f;
    bool read_only_ = false;
    bool dragging_ = false;
    bool coalesce_typing_ = false;
    std::vector<UndoEntry> undo_;
    std::vector<UndoEntry> redo_;
    std::vector<SearchMatch> matches_;
    int active_match_ = -1;
    std::optional<XER::UI::Layout::Point> pending_click_;
    bool pending_extend_ = false;
    // pending click cadence: 0 caret, 1 word (double), 2 line (triple).
    int pending_select_ = 0;

    // Scrollbars (thin overlays; geometry from the last Render).
    float content_w_ = 0.0f;      // widest line in px (measured)
    std::size_t longest_line_ = 0;// longest line by bytes (Retokenize)
    float avg_char_w_ = 7.0f;     // fallback glyph width estimate
    float hbar_h_ = 0.0f;         // reserved bottom strip when shown
    bool vbar_visible_ = false;
    bool hbar_visible_ = false;
    XER::UI::Layout::Rect vbar_track_{};
    XER::UI::Layout::Rect vbar_thumb_{};
    XER::UI::Layout::Rect hbar_track_{};
    XER::UI::Layout::Rect hbar_thumb_{};
    bool vbar_drag_ = false;
    bool hbar_drag_ = false;
    float grab_off_ = 0.0f;

    // Double/triple click tracking.
    std::chrono::steady_clock::time_point last_click_{};
    XER::UI::Layout::Point last_click_pos_{};
    int click_count_ = 0;

    // Painted metrics (refreshed every Render).
    float pad_ = 8.0f;
    float line_height_ = 19.6f;
    float gutter_w_ = 44.0f;
    float font_size_ = 13.0f;
    std::function<void(const std::string&)> on_changed_;
    std::function<void(int, int)> on_caret_;
};

}  // namespace XER::IdeXejso
