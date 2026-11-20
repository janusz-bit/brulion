// vim — Vim keys for a text editor (docs/UI.md "Vim mode"): a GUI-free state machine that reads
// keys and moves the caret and edits text through an abstract Buffer, so that one engine drives
// both the block editor (a page as a sequence of blocks: src/app/editor/vimpagebuffer.h) and the
// Markdown source mode (one text: src/app/editor/vimsourcebuffer.h). TextBuffer (textbuffer.h) is a
// plain in-memory buffer (the tests).
//
//   vim::Shared shared;               // registers, the last search, the last change: app-wide
//   vim::TextBuffer buffer(u"Zażółć gęślą jaźń"_s);
//   vim::Engine engine(&buffer, &shared);
//   engine.feed(vim::Key::fromText(u"d"_s));
//   engine.feed(vim::Key::fromText(u"w"_s));   // "gęślą jaźń"
//
// The engine knows lines and columns only (a column is a UTF-16 index into the line; the caret
// steps over whole characters: surrogate pairs and combining marks). What a line is and how an
// edit is made is the buffer's: the block editor's lines are the lines of its blocks' texts, a
// linewise delete removes whole blocks, a put of lines inserts blocks, one change of Vim is one
// step of the editor's undo history (Buffer::beginChange() / endChange()).
//
// Supported (docs/UI.md "Vim mode" has the table): modes (normal, insert, replace, visual, visual
// line, the command line of : / ?), counts, motions (h j k l w W b B e E ge gE 0 ^ $ g_ gg G { } f
// F t T ; , % | + - _ Enter, n N * #, ` ' marks, Ctrl+D / Ctrl+U), operators (d c y > < g~ gu gU)
// with motions and text objects (iw aw iW aW ip ap i" a" i' a' i` a` i( a( ib ab i[ a[ i{ a{ iB aB
// i< a<), dd cc yy D C S s x X r R ~ J gJ p P gp gP Y, u and Ctrl+R (the buffer's undo / redo),
// the dot repeat, registers ("" "0-"9 "- "a-"z "A-"Z "+ "* "_ ". "/ ":), marks (m, `, ', '<,
// '>, ``), :w :q :q! :wq :x :{line} :[range]s/old/new/[gin] (plain text: no patterns) and :noh.
// Commands are read from the character a key types, not its place on the keyboard (Vim's way):
// with a Polish layout the letters are Latin, and ą ę ó … are only arguments (fą, rź, /żółw).
#pragma once

#include <QChar>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <Qt>

#include <compare>
#include <functional>

namespace brulion::vim {

/// A place in a buffer: line and column (a UTF-16 index into the line's text).
struct Position {
    int line = 0;
    int column = 0;
    friend bool operator==(const Position &, const Position &) = default;
    friend std::strong_ordering operator<=>(const Position &, const Position &) = default;
};

enum class Mode {
    Normal,
    Insert,     ///< the editor takes typing (the engine only watches for Esc)
    Replace,    ///< R: typed characters replace the text (the engine types them)
    Visual,     ///< v: characters from the anchor to the caret (both included)
    VisualLine, ///< V: whole lines
};

/// z commands on folds (the block editor's folded blocks).
enum class FoldAction { Toggle, Open, Close, OpenAll, CloseAll };
/// zt / zz / zb: where the caret's line goes in the view.
enum class ScrollTo { Top, Center, Bottom };

/// The shape of a selection shown by the buffer.
enum class SelectionShape { None, Characters, Lines };

/// A register's content (Vim's `:registers`).
struct Register {
    QString text;          ///< lines joined by "\n"; linewise: without a final "\n"
    bool linewise = false; ///< whole lines (yy, dd, V…y): a put inserts lines
    /// Linewise text taken from whole blocks of a block editor: their Markdown (list markers and
    /// the indentation relative to the first one) — what a put into a block editor inserts as
    /// blocks; empty otherwise.
    QString blocks;
    bool isEmpty() const { return text.isEmpty() && !linewise; }
    friend bool operator==(const Register &, const Register &) = default;
};

/// A key as the editor got it: Qt::Key, its modifiers and the text it types.
struct Key {
    int key = 0;
    Qt::KeyboardModifiers modifiers;
    QString text;

    /// A key that types `text` (one character; the tests' and the dot repeat's keys).
    static Key fromText(const QString &text);
    /// `key` with `modifiers` and no text (Esc, Enter, Ctrl+R…).
    static Key special(int key, Qt::KeyboardModifiers modifiers = {});
};

/// Keys written as Vim writes them in mappings: characters as themselves, <Esc> <CR> <BS> <Del>
/// <Tab> <Left> <Right> <Up> <Down> <Home> <End> <PageUp> <PageDown> <Space> <lt>, <C-x> (Ctrl+x),
/// <C-[> (the tests' and the self-tests' keys).
QList<Key> keysFromNotation(const QString &notation);

/// What the engine reports to the user (the app words it: VimSession::messageText()).
enum class Message {
    None,
    PatternNotFound,    ///< arg: the pattern
    SearchHitBottom,    ///< the search went on from the top
    SearchHitTop,       ///< … from the bottom
    Substituted,        ///< count substitutions on count2 lines
    NotAnEditorCommand, ///< arg: the command
    NothingToUndo,      ///< "Already at oldest change"
    NothingToRedo,      ///< "Already at newest change"
    RegisterEmpty,      ///< arg: the register's name
    MarkNotSet,         ///< arg: the mark's name
    InvalidRange,
    NotSaved, ///< :w found nothing to save to (or the save failed)
    Saved,
    Unsupported, ///< arg: what (a command the engine does not have)
    ReadOnly,    ///< the buffer refused an edit
};

/// A buffer the engine edits — the block editor's page, the source mode's text, TextBuffer.
/// Positions given to it are valid (line < lineCount(), column <= line(l).size()).
class Buffer {
public:
    virtual ~Buffer() = default;

    // --- Text --------------------------------------------------------------------------------
    /// At least 1 (an empty buffer has one empty line).
    virtual int lineCount() const = 0;
    virtual QString line(int i) const = 0;

    // --- Caret ---------------------------------------------------------------------------------
    virtual Position cursor() const = 0;
    /// Put the caret at `p` (ends a selection shown). `insert`: the caret is the insert mode's
    /// (it may stand after the line's last character).
    virtual void setCursor(Position p) = 0;
    /// Show the visual selection: from `anchor` to `cursor`, both ends included (`Lines`: their
    /// whole lines), the caret at `cursor`. None: none (setCursor() follows).
    virtual void showSelection(Position anchor, Position cursor, SelectionShape shape)
    {
        Q_UNUSED(anchor);
        Q_UNUSED(shape);
        setCursor(cursor);
    }

    // --- Edits (between beginChange() and endChange()) -----------------------------------------
    /// Replace [from, to) — characters; across lines the line breaks between them belong to it —
    /// with `text` ("\n" starts a new line). False when the buffer refuses (read-only, a block
    /// that is no text).
    virtual bool replace(Position from, Position to, const QString &text) = 0;
    /// Remove lines [first, last] whole (a buffer keeps at least one, empty, line).
    virtual bool removeLines(int first, int last) = 0;
    /// Insert `reg`'s lines (linewise) below (`below`) or above line `line`; returns the first line
    /// they are on now (-1: refused).
    virtual int putLines(int line, bool below, const Register &reg) = 0;
    /// A new empty line below / above `line` to type into (o / O); returns where the caret goes
    /// (after the indentation the new line gets). line -1: refused.
    virtual Position openLine(int line, bool below) = 0;
    /// Indent (`levels` > 0) or outdent (< 0) lines [first, last] (> <).
    virtual bool shiftLines(int first, int last, int levels) = 0;
    /// What a yank or a delete of lines [first, last] puts into a register (the default: their
    /// text; a block editor adds the blocks' Markdown when the lines are whole blocks).
    virtual Register linesRegister(int first, int last) const;

    // --- Undo ----------------------------------------------------------------------------------
    /// One change of Vim (an operator, x, p, an insert from i to Esc…) is one undo step: the edits
    /// between these two calls (nested calls count once).
    virtual void beginChange() = 0;
    virtual void endChange() = 0;
    /// One step of the buffer's history; false when there is none. The buffer puts the caret
    /// where the change was.
    virtual bool undo() = 0;
    virtual bool redo() = 0;

    // --- The editor around it ------------------------------------------------------------------
    /// The engine's mode changed (insert / replace: the editor takes typing or shows the caret).
    virtual void modeChanged(Mode mode) { Q_UNUSED(mode); }
    /// :w — false when nothing could be saved.
    virtual bool save() { return false; }
    /// :q — close what is edited (`force`: :q!, unsaved changes are the editor's: it saves them
    /// anyway, notes never lose text).
    virtual void quit(bool force) { Q_UNUSED(force); }
    /// Lines one screen shows (Ctrl+D / Ctrl+U move half of it).
    virtual int pageLines() const { return 20; }
    /// za zo zc zR zM on line `line`; false when the buffer has no folds there.
    virtual bool fold(int line, FoldAction action)
    {
        Q_UNUSED(line);
        Q_UNUSED(action);
        return false;
    }
    /// zt zz zb: scroll the view so that line `line` is at its top, middle or bottom.
    virtual void scroll(int line, ScrollTo where)
    {
        Q_UNUSED(line);
        Q_UNUSED(where);
    }
    /// A place that follows the text's edits as well as the buffer can (marks): `anchor()` of a
    /// position, `anchorPosition()` back (line -1: gone). The default keeps the position as it is.
    virtual QVariant anchor(Position p) const;
    virtual Position anchorPosition(const QVariant &anchor) const;
};

/// What all engines share (Vim's registers and its last search, char search and change are
/// global): one per app.
class Shared {
public:
    /// The system clipboard ("+ and "*): `read` returns its text; without these they are
    /// ordinary registers.
    std::function<QString()> readClipboard;
    std::function<void(const QString &)> writeClipboard;

    /// Register `name` (lowercase for a-z; '"' the unnamed one).
    Register get(QChar name) const;
    /// A yank into `name` (0 / '"': none given) — "0 and the unnamed register.
    void yanked(QChar name, const Register &reg);
    /// A delete or a change into `name` — the unnamed register and "1 (shifting "1-"8 to "2-"9)
    /// or, for less than a line, "-.
    void deleted(QChar name, const Register &reg);
    /// A register that can be read and written ('"', 0-9, a-z, A-Z, -, +, *, _), or read (. / :).
    static bool isRegister(QChar name);
    static bool isWritable(QChar name);

    QString lastSearch;
    bool lastSearchForward = true;
    bool lastSearchWholeWord = false;
    QString lastSubstitute; ///< :s's text and replacement (:s and & without them)
    QString lastReplacement;
    QString lastInserted; ///< ". register
    QString lastCommand;  ///< ": register
    QStringList searchHistory;
    QStringList commandHistory;
    // The last f F t T and its character (; ,).
    QChar lastFind;
    QString lastFindChar;

    /// The last change (the dot repeat): its keys and the text its insert typed.
    struct Change {
        QString keys;     ///< the command's keys, without the count ("dw", "ci(", "x", "o")
        int count = 0;    ///< 0: none
        QChar reg;        ///< register given, 0 none
        QString inserted; ///< what the insert mode typed ("" none)
        bool insertsText = false;
        // A visual operator: the size of the selection it worked on.
        bool visual = false;
        bool visualLines = false;
        int visualLineSpan = 0;   ///< lines - 1
        int visualColumnSpan = 0; ///< on one line: characters - 1; else the last line's end column
    };
    Change lastChange;

private:
    void store(QChar name, const Register &reg);
    QHash<QChar, Register> m_registers;
};

/// The Vim state machine for one buffer.
class Engine {
public:
    Engine(Buffer *buffer, Shared *shared);

    /// Another buffer (the block editor ↔ source mode): back to normal mode.
    void setBuffer(Buffer *buffer);
    Buffer *buffer() const { return m_buffer; }

    /// The editor's key: true when the engine took it (the editor must not act on it). In insert
    /// mode only Esc / Ctrl+[ are taken; in the other modes every key that types a character, and
    /// the special keys Vim has (Esc, Enter, Backspace, Delete, the arrows, Home, End, Ctrl+R,
    /// Ctrl+D, Ctrl+U); other keys with Ctrl, Alt or Meta are left to the editor's shortcuts.
    bool feed(const Key &key);

    Mode mode() const { return m_mode; }
    /// The keys of a command being typed ("2d", "\"a", "f") — Vim's showcmd.
    QString pending() const;
    /// The command line being typed (":s/a/b", "/żółw"), "" when none.
    QString commandLine() const;
    bool commandLineActive() const { return m_cmdType != QChar(); }
    Message message() const { return m_message; }
    QString messageArg() const { return m_messageArg; }
    int messageCount() const { return m_messageCount; }
    int messageCount2() const { return m_messageCount2; }
    void clearMessage();

    /// Back to normal mode with nothing pending (a page changed under it, the editor lost the
    /// keys): an insert in progress ends (its change too).
    void reset();
    /// Insert mode where the caret is, without a command (the user clicked into the text to type
    /// while the setting was just switched on…).
    void startInsert();
    /// A selection made elsewhere (the mouse, Shift+arrows, a block selection): visual mode over
    /// it (`lines`: visual line mode).
    void adoptSelection(Position anchor, Position cursor, bool lines);
    /// The visual selection's anchor (visual modes).
    Position visualAnchor() const { return m_visualAnchor; }

private:
    struct Range {
        Position from; ///< charwise: [from, to); linewise: lines from.line..to.line
        Position to;
        bool linewise = false;
        bool valid = false;
    };
    enum class MotionKind { Exclusive, Inclusive, Linewise };
    struct Motion {
        Position to;
        MotionKind kind = MotionKind::Exclusive;
        bool valid = false;
        bool jump = false;     ///< sets the `` mark
        bool keepWant = false; ///< j k: keep the wanted column
        bool wantEnd = false;  ///< $: the wanted column is the end of every line
    };
    /// A parsed command (see parse()).
    struct Command {
        QChar reg;
        int count = 0;        ///< both counts multiplied; 0 none
        QString op;           ///< "d" "c" "y" ">" "<" "g~" "gu" "gU", "" none
        bool doubled = false; ///< dd, cc, yy, >>, <<, g~~, guu, gUU
        QString keys;         ///< motion, text object or action ("w", "iw", "x", "f")
        QString arg;          ///< f t F T r m ` ' : the character typed after it
        QString all;          ///< every key without the register and counts (the dot repeat's)
    };
    enum class Parse { Incomplete, Invalid, Done };

    QString tokenOf(const Key &key) const;
    bool feedToken(const QString &token);
    bool feedInsert(const QString &token);
    bool feedReplace(const QString &token);
    bool feedCommandLine(const QString &token);
    Parse parse(const QString &keys, Command &cmd) const;
    Parse parseMotion(const QString &keys, int &i, Command &cmd, bool objects) const;
    void execute(const Command &cmd, bool repeating);
    void executeVisual(const Command &cmd, bool repeating);
    bool isMotionKey(const QString &keys) const;

    // Motions (motions.cpp).
    Motion motion(const QString &keys, const QString &arg, int count, bool hasCount, bool forOperator);
    Range textObject(const QString &keys, int count, Position anchor, Position cursor, bool visual) const;
    Position searchFrom(Position from, const QString &pattern, bool forward, bool wholeWord, bool wrapMessages,
                        bool &found);
    // Helpers.
    int lineLength(int line) const;
    QString lineText(int line) const;
    int lastLine() const;
    Position clampNormal(Position p) const;
    Position firstNonBlank(int line) const;
    QString textOf(Position from, Position to) const;
    Position nextChar(Position p) const; ///< the next character on the line (stays at the end)
    Position prevChar(Position p) const;
    Position afterChar(Position p) const; ///< after the character at p (an inclusive end)
    void moveTo(Position p, bool keepWant = false);
    void setMode(Mode m);
    void setMessage(Message m, const QString &arg = {}, int count = 0, int count2 = 0);
    void beginChange();
    void endChange();

    // Operators and commands.
    Range rangeOf(const Motion &m, Position start, const QString &op, bool forced) const;
    void applyOperator(const QString &op, const Range &range, QChar reg, int count, bool visual);
    void yankRange(const Range &range, QChar reg, bool isDelete);
    Register rangeRegister(const Range &range) const;
    void deleteRange(const Range &range);
    void changeCase(const Range &range, const QString &op);
    void put(QChar reg, int count, bool after, bool cursorAfter);
    void join(int count, bool spaces);
    void replaceChars(int count, const QString &with);
    void toggleCase(int count);
    void enterInsert(Position at, const QString &how);
    void finishInsert();
    void undo(int count, bool redo);
    void repeatLastChange(int count);
    void recordChange(const Command &cmd, bool insertsText);
    void setMark(QChar name, Position p);
    bool markPosition(QChar name, Position &p);
    void jumpMarkHere();

    // The command line (excommand.cpp).
    void runCommandLine(QChar type, const QString &text);
    void runExCommand(const QString &text);
    bool parseLineRange(const QString &text, int &i, int &first, int &last, bool &given);
    bool parseLineAddress(const QString &text, int &i, int &line);
    void substitute(int first, int last, const QString &args);
    void searchCommand(const QString &pattern, bool forward, int count);

    Buffer *m_buffer = nullptr;
    Shared *m_shared = nullptr;
    Mode m_mode = Mode::Normal;
    QString m_pending; ///< keys of the command being typed
    void syncWant(Position cur);
    int m_want = 0;            ///< the column j / k keep
    Position m_wantAt{-1, -1}; ///< where the caret was when m_want was taken
    bool m_wantEnd = false;    ///< after $: the end of every line
    Position m_visualAnchor;
    Position m_lastVisualStart, m_lastVisualEnd; ///< '< '>
    bool m_lastVisualLines = false;
    bool m_hasLastVisual = false;
    // The insert in progress.
    Position m_insertStart;
    int m_insertCount = 1;
    bool m_insertRecords = false; ///< the change it belongs to is recorded at Esc
    Command m_insertCommand;
    bool m_changeOpen = false;
    bool m_repeating = false;
    QStringList m_replaced;        ///< replace mode: the characters typed over (Backspace)
    Shared::Change m_visualChange; ///< a visual c: its change, recorded at Esc
    bool m_visualChangePending = false;
    // The command line.
    QChar m_cmdType; ///< ':' '/' '?' or 0
    QString m_cmdText;
    Command m_cmdPending; ///< an operator waiting for a search (d/foo)
    bool m_cmdOperator = false;
    bool m_cmdFromVisual = false;
    int m_historyIndex = -1;
    // Marks.
    QHash<QChar, QVariant> m_marks;
    Message m_message = Message::None;
    QString m_messageArg;
    int m_messageCount = 0;
    int m_messageCount2 = 0;
};

// --- Characters (motions.cpp; exported for the tests and the buffers) --------------------------

/// Vim's character classes: 0 blank, 1 punctuation, 2 a word character (letters of any script,
/// digits, '_').
int charClass(QChar c);
/// The next / previous character boundary of `text` after / before `i` (surrogate pairs and
/// combining marks belong to the character before them).
int nextBoundary(const QString &text, int i);
int previousBoundary(const QString &text, int i);
/// The leading blanks of `line`.
QString indentationOf(const QString &line);

} // namespace brulion::vim
