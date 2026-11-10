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
//       "code"   — code/calc/query content: only comments (# and //) are dimmed.
// palette (QML colors or colour strings): heading, marker (list/quote/heading marks, brackets),
// link, url, tag, code, codeBackground, highlightBackground, muted (properties, block ids),
// faint (comments, fences), due, scheduled, reminder, alarm, recurrence, done, priority, text (the
// plain text's colour: misspelled words take it with their underline, see spellCheck).
// monoFamily: font family of inline code. lineHeight (px, > 0): every paragraph of the document
// gets this fixed line height (QTextBlockFormat::FixedHeight) — the same layout QML `Text` uses
// with `lineHeightMode: Text.FixedHeight`, so a block does not move when it switches between
// rendered and edited. Setting it disables the document's own undo stack (the block editor has
// its own); leave it 0 for editors that rely on TextEdit's undo (source mode).
// spellCheck (SpellCheck, docs/UI.md "Spell checking"): words it says are wrong get a red
// underline (format "spelling", `spellColor`) — prose only (spell/spellwords.h skips the syntax),
// never in "code" mode, fences, frontmatter or comments; the word ending at `spellCaret` is not
// marked while it is being typed. Verdicts come from the checker's thread: lines with words still
// pending are highlighted again when they arrive (only those lines). misspelledAt(pos): the
// marked word there, for the editor's menu.
// reminderChips (the block editor's edited text, "inline" mode): every ⏰ / ⏰! reminder token stays
// a chip while its block is edited — `chipShapes` gives one rounded shape per line segment of each
// token ({x, y, width, height, color, kind "pill", from, to}: document positions of the token), in
// the document's coordinates, vertically centred on the text's glyphs less `chipInset`, coloured
// with the palette's reminderBackground / alarmBackground (its text: reminderChip / alarmChip,
// else reminder / alarm). The editor draws them under the text and opens the reminder dialog on a
// click (InlineEditor). Recomputed when the document's layout changes (text, width, font) and
// announced only when different.
#pragma once

#include "richtextpills.h"
#include "spellcheck.h"
#include "text/find.h"

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>
#include <QVariantMap>

namespace brulion::editor {

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged FINAL)
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    Q_PROPERTY(QString monoFamily READ monoFamily WRITE setMonoFamily NOTIFY monoFamilyChanged FINAL)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged FINAL)
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
    /// Reminder tokens are chips (`chipShapes`); off: none.
    Q_PROPERTY(bool reminderChips READ reminderChips WRITE setReminderChips NOTIFY reminderChipsChanged FINAL)
    Q_PROPERTY(qreal chipInset READ chipInset WRITE setChipInset NOTIFY chipInsetChanged FINAL)
    Q_PROPERTY(QList<brulion::editor::RichTextPill> chipShapes READ chipShapes NOTIFY chipShapesChanged FINAL)

public:
    /// A reminder token of a line (positions in the line): `reminderChips`.
    struct LineChip {
        int from = 0;
        int to = 0;
        bool alarm = false;
        bool operator==(const LineChip &other) const = default;
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
    bool reminderChips() const { return m_reminderChips; }
    void setReminderChips(bool on);
    qreal chipInset() const { return m_chipInset; }
    void setChipInset(qreal px);
    QList<RichTextPill> chipShapes() const { return m_chipShapes; }
    /// Recomputes `chipShapes` from the current layout (layout changes call it by themselves).
    Q_INVOKABLE void refreshChips();

    /// The word marked as misspelled at `position` of the document (inside it or right after
    /// it): {from, to (document positions), word (as checked: "np." with its dot), text (as
    /// written)}; an empty map when none is marked there.
    Q_INVOKABLE QVariantMap misspelledAt(int position) const;

    /// Attach to a plain QTextDocument (C++/tests); same as QSyntaxHighlighter::setDocument plus
    /// the line-height handling.
    void attach(QTextDocument *document);

Q_SIGNALS:
    void textDocumentChanged();
    void paletteChanged();
    void modeChanged();
    void monoFamilyChanged();
    void lineHeightChanged();
    void findChanged();
    void spellCheckChanged();
    void spellCaretChanged();
    void spellColorChanged();
    void reminderChipsChanged();
    void chipInsetChanged();
    void chipShapesChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Normal = 0, Fence = 1, Frontmatter = 2, Comment = 3 };

    QColor color(const char *key, const QColor &fallback) const;
    void rebuildFormats();
    void applyLineHeight(int from = 0, int charsAdded = -1);
    void highlightInline(const QString &text, int offset, int length);
    void highlightLine(const QString &line);
    /// One find option set (`option`: &text::FindOptions::matchCase, …).
    void setFindOption(bool text::FindOptions::*option, bool on);
    void findChangedTo(const QString &query, text::FindOptions options);
    void setRange(int from, int length, const QTextCharFormat &format);
    /// `format` merged into the range's formats; a valid `plainColor` becomes the foreground of the
    /// text that has none.
    void mergeRange(int from, int length, const QTextCharFormat &format, const QColor &plainColor = {});
    bool spellActive() const;
    void highlightSpelling(const QString &line);
    void rehighlightPendingSpelling();
    /// Highlights block `number` again (none: -1 or past the end).
    void rehighlightLine(int number);
    /// Block number of the wrong word ending at `position`, -1 when none ends there.
    int wrongWordBlockAt(int position) const;
    /// The current line's reminder tokens (m_lineChips) kept on its block (ChipData); the shapes
    /// recomputed when they changed.
    void storeLineChips();

    QPointer<QQuickTextDocument> m_quickDocument;
    QVariantMap m_palette;
    QString m_mode = QStringLiteral("inline");
    QString m_monoFamily;
    qreal m_lineHeight = 0;
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
    /// Lines with words whose verdicts are pending, and lines with words marked (block numbers).
    QSet<int> m_spellPending;
    QSet<int> m_spellMarked;
    /// The line where the word at the caret was left unmarked (-1: none).
    int m_spellSuppressedBlock = -1;
    QTimer m_spellTimer;
    bool m_reminderChips = false;
    qreal m_chipInset = 0;
    QList<LineChip> m_lineChips; ///< collected while a line is highlighted
    QList<RichTextPill> m_chipShapes;
    bool m_computingChips = false;
    QList<QMetaObject::Connection> m_layoutConnections;
};

} // namespace brulion::editor
