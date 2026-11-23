// MarkdownHighlighter — syntax colours for the raw Markdown while a block (or the whole file in
// source mode) is edited: headings, emphasis, inline code, wiki/Markdown links, URLs, tags,
// block refs, task/reminder tokens (📅 ⏳ 🛫 ⏰ 🔁 ✅ …), block ids, `key::` properties, quote
// and list markers, comments, fences. A QSyntaxHighlighter attached to the QTextDocument of a
// QML TextEdit through `textDocument` (QQuickTextDocument), or to any QTextDocument from C++
// (setDocument, used by the tests).
//
//   TextEdit { id: edit; textFormat: TextEdit.PlainText }
//   MarkdownHighlighter { textDocument: edit.textDocument; palette: ({ ... }); mode: "inline" }
//
// mode: "inline" — one block body as edited inline (no frontmatter/fences across lines);
//       "source" — a whole file (fenced code, frontmatter and %% comments span lines);
//       "code"   — code/calc/query content: only comments (# and //) are dimmed — unless
//                  `language` is Luau ("luau", "lua": a ```luau block, syntax/luau.h), whose
//                  keywords, names of the library and of `b`, numbers, strings and comments
//                  are coloured; "source" colours the ```luau fences of a file the same way.
// palette (QML colors or colour strings): heading, marker (list/quote/heading marks, brackets),
// link, url, tag, code, codeBackground, highlightBackground, muted (properties, block ids),
// faint (comments, fences), due, scheduled, reminder, alarm, recurrence, done, priority, text (the
// plain text's colour: misspelled words take it with their underline, see spellCheck); Luau's
// keyword, constant, builtin (also `b`'s fields), function, number, string (also the braces of an
// interpolation).
// monoFamily: font family of inline code. lineHeight (px, > 0): every paragraph of the document
// gets this fixed line height (QTextBlockFormat::FixedHeight) — the same layout QML `Text` uses
// with `lineHeightMode: Text.FixedHeight`, so a block does not move when it switches between
// rendered and edited. Setting it disables the document's own undo stack (the block editor has
// its own); leave it 0 for editors that rely on TextEdit's undo (source mode).
// spellCheck (SpellCheck, docs/UI.md "Spell checking"): words it says are wrong get a red
// underline (format "spelling", `spellColor`) — prose only (spell/spellwords.h skips the syntax),
// never in "code" mode, fences, frontmatter, display formulas between `$$` lines or comments; the
// word ending at `spellCaret` is not marked while it is being typed. Verdicts come from the
// checker's thread: lines with words still pending are highlighted again once every word asked
// for has its answer (only those lines). misspelledAt(pos): the marked word there, for the
// editor's menu. Never a long stall in a large text (source mode, docs/UI.md "Spell checking"): a
// large edit (setting a page's text, a long paste) is highlighted without spelling, which a pass
// over its lines adds in slices of a few milliseconds; those lines, a pass over every line
// (spell checking switched on, languages or the dictionary changed) and the lines whose answers
// came are highlighted again in such slices too — a small text (an edited block) at once.
// reminderChips (the block editor's edited text, "inline" mode): every ⏰ / ⏰! reminder token stays
// a chip while its block is edited — `chipShapes` gives one rounded shape per line segment of each
// token ({x, y, width, height, color, kind "pill", from, to}: document positions of the token), in
// the document's coordinates, vertically centred on the text's glyphs less `chipInset`, coloured
// with the palette's reminderBackground / alarmBackground. A chip shows words, not its token: its
// `label` (markdown::reminderChipLabel(): "⏰ jutro 18:00", relative to `chipToday` in the UI
// language `chipLocale`) in `textColor` (reminderChip / alarmChip, else reminder / alarm) on its
// `baseline` — drawn by the editor over the pill —, while the document keeps the token's text
// byte for byte: its characters are laid out invisible (transparent, a 1-px font), the first one
// as wide as the label and the others with no width, so the token takes the label's room and
// never wraps inside. The editor opens the reminder dialog on a click and the chip's menu on a
// right-click or a long press (InlineEditor), and moves the caret over a chip as one unit
// (chipEndingAt / chipStartingAt / chipFromAround / chipToAround). Recomputed when the
// document's layout changes (text, width, font) and announced only when different.
#pragma once

#include "richtextpills.h"
#include "spellcheck.h"
#include "text/find.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace brulion::editor {

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged FINAL)
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    Q_PROPERTY(QString monoFamily READ monoFamily WRITE setMonoFamily NOTIFY monoFamilyChanged FINAL)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged FINAL)
    /// The language of "code" mode (a fenced block's info string): "luau" / "lua" colour Luau.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    /// The find bar's matches in this text (docs/UI.md "Find and replace"): tinted on top of the
    /// syntax with `findColor`. "": none.
    Q_PROPERTY(QString findQuery READ findQuery WRITE setFindQuery NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchCase READ findMatchCase WRITE setFindMatchCase NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchDiacritics READ findMatchDiacritics WRITE setFindMatchDiacritics NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findWholeWord READ findWholeWord WRITE setFindWholeWord NOTIFY findChanged FINAL)
    Q_PROPERTY(QColor findColor READ findColor WRITE setFindColor NOTIFY findChanged FINAL)
    /// Spell checking: null or inactive — none.
    Q_PROPERTY(
            brulion::editor::SpellCheck *spellCheck READ spellCheck WRITE setSpellCheck NOTIFY spellCheckChanged FINAL)
    /// The caret (a position in the document; -1: none): the word ending right at it is not marked.
    Q_PROPERTY(int spellCaret READ spellCaret WRITE setSpellCaret NOTIFY spellCaretChanged FINAL)
    Q_PROPERTY(QColor spellColor READ spellColor WRITE setSpellColor NOTIFY spellColorChanged FINAL)
    /// The part of the document on screen (document positions; −1: the whole document is checked):
    /// only its lines and SpellViewMargin lines around it are spell-checked (source mode's view).
    Q_PROPERTY(int spellViewFrom READ spellViewFrom WRITE setSpellViewFrom NOTIFY spellViewChanged FINAL)
    Q_PROPERTY(int spellViewTo READ spellViewTo WRITE setSpellViewTo NOTIFY spellViewChanged FINAL)
    /// Reminder tokens are chips (`chipShapes`); off: none.
    Q_PROPERTY(bool reminderChips READ reminderChips WRITE setReminderChips NOTIFY reminderChipsChanged FINAL)
    Q_PROPERTY(qreal chipInset READ chipInset WRITE setChipInset NOTIFY chipInsetChanged FINAL)
    Q_PROPERTY(QList<brulion::editor::RichTextPill> chipShapes READ chipShapes NOTIFY chipShapesChanged FINAL)
    /// The language of the chips' words: "pl" (default) or "en".
    Q_PROPERTY(QString chipLocale READ chipLocale WRITE setChipLocale NOTIFY chipLocaleChanged FINAL)
    /// The day the chips' words are relative to ("YYYY-MM-DD"; "" or invalid: absolute dates).
    Q_PROPERTY(QString chipToday READ chipToday WRITE setChipToday NOTIFY chipTodayChanged FINAL)

public:
    /// A reminder token of a line (positions in the line): `reminderChips`.
    struct LineChip {
        int from = 0;
        int to = 0;
        bool alarm = false;
        QString label;
        bool operator==(const LineChip &other) const = default;
    };
    /// The part of a line that is prose for spell checking ([from, to) of the line, `to` −1: its
    /// end; `from` −1: none — fences, code, the frontmatter, formulas and comments over lines in
    /// "source" mode, everything in "code" mode); `lineStart`: it starts the line (spell::findWords).
    struct SpellRange {
        int from = 0;
        int to = -1;
        bool lineStart = true;
        bool operator==(const SpellRange &other) const = default;
    };

    explicit MarkdownHighlighter(QObject *parent = nullptr);
    ~MarkdownHighlighter() override;

    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    QVariantMap palette() const { return m_palette; }
    void setPalette(const QVariantMap &palette);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString monoFamily() const { return m_monoFamily; }
    void setMonoFamily(const QString &family);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal px);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString findQuery() const { return m_findQuery; }
    void setFindQuery(const QString &query) { findChangedTo(query, m_findOptions); }
    bool findMatchCase() const { return m_findOptions.matchCase; }
    void setFindMatchCase(bool on) { setFindOption(&text::FindOptions::matchCase, on); }
    bool findMatchDiacritics() const { return m_findOptions.matchDiacritics; }
    void setFindMatchDiacritics(bool on) { setFindOption(&text::FindOptions::matchDiacritics, on); }
    bool findWholeWord() const { return m_findOptions.wholeWord; }
    void setFindWholeWord(bool on) { setFindOption(&text::FindOptions::wholeWord, on); }
    QColor findColor() const { return m_findColor; }
    void setFindColor(const QColor &color);
    SpellCheck *spellCheck() const { return m_spell; }
    void setSpellCheck(SpellCheck *spell);
    int spellCaret() const { return m_spellCaret; }
    void setSpellCaret(int position);
    QColor spellColor() const { return m_spellColor; }
    void setSpellColor(const QColor &color);
    int spellViewFrom() const { return m_spellViewFrom; }
    void setSpellViewFrom(int position) { setSpellView(position, m_spellViewTo); }
    int spellViewTo() const { return m_spellViewTo; }
    void setSpellViewTo(int position) { setSpellView(m_spellViewFrom, position); }
    /// Both ends of `spellView` at once.
    void setSpellView(int from, int to);
    /// Lines checked around the view, before and after it.
    static constexpr int SpellViewMargin = 100;
    bool reminderChips() const { return m_reminderChips; }
    void setReminderChips(bool on);
    qreal chipInset() const { return m_chipInset; }
    void setChipInset(qreal px);
    QList<RichTextPill> chipShapes() const { return m_chipShapes; }
    QString chipLocale() const { return m_chipLocale; }
    void setChipLocale(const QString &locale);
    QString chipToday() const { return m_chipToday; }
    void setChipToday(const QString &today);
    /// The caret as one unit with a chip (document positions; -1 when there is no such chip):
    /// the start of the chip ending at `position` (Left, Backspace), the end of the one starting
    /// there (Right, Delete), the start / end of the one `position` is strictly inside of.
    Q_INVOKABLE int chipEndingAt(int position) const;
    Q_INVOKABLE int chipStartingAt(int position) const;
    Q_INVOKABLE int chipFromAround(int position) const;
    Q_INVOKABLE int chipToAround(int position) const;
    /// Recomputes `chipShapes` from the current layout (layout changes call it by themselves).
    Q_INVOKABLE void refreshChips();

    /// The word marked as misspelled at `position` of the document (inside it or right after
    /// it): {from, to (document positions), word (as checked: "np." with its dot), text (as
    /// written)}; an empty map when none is marked there.
    Q_INVOKABLE QVariantMap misspelledAt(int position) const;
    /// No spelling left to apply: no line waiting for verdicts or to be highlighted again, no pass
    /// over the lines running (tests).
    bool spellingSettled() const { return m_spellPending.isEmpty() && m_spellReady.isEmpty() && m_spellSweep < 0; }

    /// Attach to a plain QTextDocument (C++/tests); same as QSyntaxHighlighter::setDocument plus
    /// the line-height handling.
    void attach(QTextDocument *document);

Q_SIGNALS:
    void textDocumentChanged();
    void paletteChanged();
    void modeChanged();
    void monoFamilyChanged();
    void lineHeightChanged();
    void languageChanged();
    void findChanged();
    void spellCheckChanged();
    void spellCaretChanged();
    void spellColorChanged();
    void spellViewChanged();
    void reminderChipsChanged();
    void chipInsetChanged();
    void chipShapesChanged();
    void chipLocaleChanged();
    void chipTodayChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Normal = 0, Fence = 1, Frontmatter = 2, Comment = 3 };

    QColor color(const char *key, const QColor &fallback) const;
    void rebuildFormats();
    void applyLineHeight(int from = 0, int charsAdded = -1);
    void highlightInline(const QString &text, int offset, int length);
    void highlightLine(const QString &line);
    /// Luau's colours over `line` (syntax/luau.h), starting in the packed lexer state `state`;
    /// returns the state the next line starts in.
    int highlightLuau(const QString &line, int state);
    /// One find option set (`option`: &text::FindOptions::matchCase, …).
    void setFindOption(bool text::FindOptions::*option, bool on);
    void findChangedTo(const QString &query, text::FindOptions options);
    void setRange(int from, int length, const QTextCharFormat &format);
    /// `format` merged into the range's formats; a valid `plainColor` becomes the foreground of the
    /// text that has none.
    void mergeRange(int from, int length, const QTextCharFormat &format, const QColor &plainColor = {});
    bool spellActive() const;
    void highlightSpelling(const QString &line);
    /// Highlights again the lines of m_spellReady, then those of the running pass (m_spellSweep);
    /// `sliced`: for SpellSliceMs at most, the rest on the next turn of the event loop.
    void rehighlightReadySpelling(bool sliced);
    /// Spell checking changed (switched on or off, every verdict forgotten, a new colour): every
    /// line again — at once in a small text, in slices in a large one —, or (switched off) only
    /// the lines with marks.
    void respell();
    /// A pass over the lines of document positions [from, to] (within the view's lines), in slices.
    void sweepSpelling(int from, int to);
    /// A pass over the lines [first, last] (block numbers), in slices.
    void sweepLines(int first, int last);
    /// Line `number` is spell-checked (inside the view's lines, or no view).
    bool inSpellView(int number) const
    {
        return m_spellViewFirst < 0 || (number >= m_spellViewFirst && number <= m_spellViewLast);
    }
    /// The word of the prose of `block` at `position` (in the block: spell::wordAt() over its
    /// SpellRange), in the block's positions.
    std::optional<spell::WordSpan> spellWordAt(const QTextBlock &block, int position) const;
    /// Highlights block `number` again (none: -1 or past the end).
    void rehighlightLine(int number);
    /// Block number of the wrong word ending at `position`, -1 when none ends there.
    int wrongWordBlockAt(int position) const;
    /// The current line's reminder tokens (m_lineChips) and its prose (m_lineSpell) kept on its
    /// block (LineData); the chips' shapes recomputed when they changed.
    void storeLineData();
    /// The current line's chips laid out as their labels (m_lineChips: invisible characters, the
    /// first one as wide as the label, the others with no width).
    void collapseLineChips();
    /// The chip [from, to) (document positions) of the line at `position` for which
    /// `match(from, to)` holds; nullopt: none.
    std::optional<std::pair<int, int>> chipWhere(int position, const std::function<bool(int, int)> &match) const;

    QPointer<QQuickTextDocument> m_quickDocument;
    QVariantMap m_palette;
    QString m_mode = QStringLiteral("inline");
    QString m_monoFamily;
    qreal m_lineHeight = 0;
    QString m_language;
    bool m_applyingFormat = false;
    QMetaObject::Connection m_contentsConnection;
    /// By name ("heading", "marker", … "spelling": rebuildFormats()); each format carries its name
    /// as a property, which the tests read back.
    QHash<QString, QTextCharFormat> m_formats;
    QString m_findQuery;
    text::FindOptions m_findOptions;
    QColor m_findColor = QColor(255, 196, 0, 90);
    QPointer<SpellCheck> m_spell;
    QList<QMetaObject::Connection> m_spellConnections;
    int m_spellCaret = -1;
    QColor m_spellColor = QColor(u"#c0392b");
    /// Lines with words whose verdicts are pending, lines with words marked, and lines to highlight
    /// again now (block numbers).
    QSet<int> m_spellPending;
    QSet<int> m_spellMarked;
    QSet<int> m_spellReady;
    /// A pass over the lines [m_spellSweep, m_spellSweepEnd] (block numbers; −1: none).
    int m_spellSweep = -1;
    int m_spellSweepEnd = -1;
    /// A large edit is being highlighted: its spelling comes from a pass after it.
    bool m_spellDeferred = false;
    QMetaObject::Connection m_bulkConnection;
    QMetaObject::Connection m_bulkDoneConnection;
    /// The prose of the line being highlighted (highlightLine()).
    SpellRange m_lineSpell;
    int m_spellViewFrom = -1;
    int m_spellViewTo = -1;
    /// The lines spell-checked (block numbers, the view's with its margins; −1: all).
    int m_spellViewFirst = -1;
    int m_spellViewLast = -1;
    /// The line where the word at the caret was left unmarked (-1: none).
    int m_spellSuppressedBlock = -1;
    QTimer m_spellTimer;
    bool m_reminderChips = false;
    qreal m_chipInset = 0;
    QList<LineChip> m_lineChips; ///< collected while a line is highlighted
    QList<RichTextPill> m_chipShapes;
    QString m_chipLocale = QStringLiteral("pl");
    QString m_chipToday;
    /// The document's font the chips were laid out for (a new one lays them out again).
    QFont m_chipFont;
    bool m_computingChips = false;
    QList<QMetaObject::Connection> m_layoutConnections;
};

} // namespace brulion::editor
