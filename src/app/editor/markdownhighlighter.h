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
// faint (comments, fences), due, scheduled, reminder, alarm, recurrence, done, priority.
// monoFamily: font family of inline code. lineHeight (px, > 0): every paragraph of the document
// gets this fixed line height (QTextBlockFormat::FixedHeight) — the same layout QML `Text` uses
// with `lineHeightMode: Text.FixedHeight`, so a block does not move when it switches between
// rendered and edited. Setting it disables the document's own undo stack (the block editor has
// its own); leave it 0 for editors that rely on TextEdit's undo (source mode).
#pragma once

#include "text/find.h"

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
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

public:
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
    void setFindQuery(const QString &query);
    bool findMatchCase() const { return m_findOptions.matchCase; }
    void setFindMatchCase(bool on);
    bool findMatchDiacritics() const { return m_findOptions.matchDiacritics; }
    void setFindMatchDiacritics(bool on);
    bool findWholeWord() const { return m_findOptions.wholeWord; }
    void setFindWholeWord(bool on);
    QColor findColor() const { return m_findColor; }
    void setFindColor(const QColor &color);

    /// Attach to a plain QTextDocument (C++/tests); same as QSyntaxHighlighter::setDocument plus
    /// the line-height handling.
    void attach(QTextDocument *document);

    /// Format names used for a character range, for tests: "heading", "marker", "link", "url",
    /// "tag", "code", "bold", "italic", "strike", "highlight", "muted", "faint", "blockId",
    /// "property", "due", "scheduled", "start", "reminder", "alarm", "recurrence", "done",
    /// "priority", "blockRef", "comment", "fence".
    static QStringList formatNames();

Q_SIGNALS:
    void textDocumentChanged();
    void paletteChanged();
    void modeChanged();
    void monoFamilyChanged();
    void lineHeightChanged();
    void findChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Normal = 0, Fence = 1, Frontmatter = 2, Comment = 3 };

    QColor color(const char *key, const QColor &fallback) const;
    void rebuildFormats();
    void applyLineHeight(int from = 0, int charsAdded = -1);
    void highlightInline(const QString &text, int offset, int length);
    void highlightLine(const QString &line);
    void findChangedTo(const QString &query, const text::FindOptions &options);
    void setRange(int from, int length, const QTextCharFormat &format);
    void mergeRange(int from, int length, const QTextCharFormat &format);

    QPointer<QQuickTextDocument> m_quickDocument;
    QVariantMap m_palette;
    QString m_mode = QStringLiteral("inline");
    QString m_monoFamily;
    qreal m_lineHeight = 0;
    bool m_applyingFormat = false;
    QMetaObject::Connection m_contentsConnection;
    QHash<QString, QTextCharFormat> m_formats;
    QString m_findQuery;
    text::FindOptions m_findOptions;
    QColor m_findColor = QColor(255, 196, 0, 90);
};

} // namespace brulion::editor
