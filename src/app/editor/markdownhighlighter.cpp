#include "markdownhighlighter.h"

#include "markdown/inline.h"
#include "markdown/tokens.h"
#include "text/text.h"

#include <QCoreApplication>
#include <QFont>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// Property holding the format's name (tests read it back from QTextLayout::formats()).
constexpr int NameProperty = QTextFormat::UserProperty + 1;

constexpr int StateMask = 0x7;

int encodeFence(QChar ch, int length)
{
    return 1 | (ch == u'~' ? 0x8 : 0) | (std::min(length, 255) << 4);
}

QColor toColor(const QVariant &v)
{
    if (!v.isValid())
        return {};
    if (v.metaType() == QMetaType::fromType<QColor>())
        return v.value<QColor>();
    return QColor::fromString(v.toString());
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QObject *parent) : QSyntaxHighlighter(parent)
{
    rebuildFormats();
}

MarkdownHighlighter::~MarkdownHighlighter() = default;

QStringList MarkdownHighlighter::formatNames()
{
    return {u"heading"_s, u"marker"_s, u"link"_s,     u"url"_s,      u"tag"_s,       u"code"_s,      u"bold"_s,
            u"italic"_s,  u"strike"_s, u"highlight"_s, u"muted"_s,   u"faint"_s,     u"blockId"_s,   u"property"_s,
            u"due"_s,     u"scheduled"_s, u"start"_s, u"reminder"_s, u"alarm"_s,     u"recurrence"_s, u"done"_s,
            u"priority"_s, u"blockRef"_s, u"comment"_s, u"fence"_s};
}

void MarkdownHighlighter::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    attach(document ? document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void MarkdownHighlighter::attach(QTextDocument *document)
{
    if (m_contentsConnection)
        disconnect(m_contentsConnection);
    // QTextDocument reports edits (contentsChange) only once it has a layout; a TextEdit's
    // document always has one, a bare one (tests, C++ users) gets it here.
    if (document)
        (void)document->documentLayout();
    setDocument(document);
    if (document) {
        m_contentsConnection = connect(document, &QTextDocument::contentsChange, this,
                                       [this](int from, int, int added) { applyLineHeight(from, added); });
        applyLineHeight();
        // QSyntaxHighlighter highlights a non-empty document on the next event loop pass and
        // ignores edits until then: run that pass now (no flash of plain text, typing right
        // away is highlighted).
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
        rehighlight();
    }
}

void MarkdownHighlighter::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    rebuildFormats();
    rehighlight();
    Q_EMIT paletteChanged();
}

void MarkdownHighlighter::setMode(const QString &mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    rehighlight();
    Q_EMIT modeChanged();
}

void MarkdownHighlighter::setMonoFamily(const QString &family)
{
    if (m_monoFamily == family)
        return;
    m_monoFamily = family;
    rebuildFormats();
    rehighlight();
    Q_EMIT monoFamilyChanged();
}

void MarkdownHighlighter::setLineHeight(qreal px)
{
    if (qFuzzyCompare(m_lineHeight + 1, px + 1))
        return;
    m_lineHeight = px;
    applyLineHeight();
    Q_EMIT lineHeightChanged();
}

void MarkdownHighlighter::applyLineHeight(int from, int charsAdded)
{
    QTextDocument *doc = document();
    if (!doc || m_lineHeight <= 0 || m_applyingFormat)
        return;
    m_applyingFormat = true;
    // The block editor keeps its own undo history; format changes must not land in TextEdit's.
    if (doc->isUndoRedoEnabled())
        doc->setUndoRedoEnabled(false);
    QTextBlock block = charsAdded < 0 ? doc->begin() : doc->findBlock(from);
    const QTextBlock end = charsAdded < 0 ? QTextBlock() : doc->findBlock(from + std::max(0, charsAdded)).next();
    for (; block.isValid() && block != end; block = block.next()) {
        const QTextBlockFormat current = block.blockFormat();
        if (current.lineHeightType() == QTextBlockFormat::FixedHeight && qFuzzyCompare(current.lineHeight(), m_lineHeight))
            continue;
        QTextBlockFormat f;
        f.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
        QTextCursor cursor(block);
        cursor.mergeBlockFormat(f);
    }
    m_applyingFormat = false;
}

QColor MarkdownHighlighter::color(const char *key, const QColor &fallback) const
{
    const QColor c = toColor(m_palette.value(QString::fromLatin1(key)));
    return c.isValid() ? c : fallback;
}

void MarkdownHighlighter::rebuildFormats()
{
    m_formats.clear();
    const auto make = [this](const QString &name) -> QTextCharFormat & {
        QTextCharFormat &f = m_formats[name];
        f.setProperty(NameProperty, name);
        return f;
    };
    const QColor link = color("link", QColor(u"#2c6aa3"_s));
    const QColor muted = color("muted", QColor(u"#535a6a"_s));
    const QColor faint = color("faint", QColor(u"#676c7b"_s));
    const QColor marker = color("marker", faint);

    QTextCharFormat &heading = make(u"heading"_s);
    heading.setFontWeight(QFont::DemiBold);
    heading.setForeground(color("heading", QColor(u"#1f2430"_s)));
    make(u"marker"_s).setForeground(marker);
    make(u"link"_s).setForeground(link);
    make(u"blockRef"_s).setForeground(link);
    QTextCharFormat &url = make(u"url"_s);
    url.setForeground(color("url", link));
    url.setFontUnderline(true);
    make(u"tag"_s).setForeground(color("tag", QColor(u"#2b5f8f"_s)));
    QTextCharFormat &code = make(u"code"_s);
    code.setForeground(color("code", QColor(u"#1f2430"_s)));
    code.setBackground(color("codeBackground", QColor(u"#f3eee2"_s)));
    if (!m_monoFamily.isEmpty())
        code.setFontFamilies({m_monoFamily});
    QTextCharFormat &fence = make(u"fence"_s);
    fence.setForeground(faint);
    if (!m_monoFamily.isEmpty())
        fence.setFontFamilies({m_monoFamily});
    make(u"bold"_s).setFontWeight(QFont::Bold);
    make(u"italic"_s).setFontItalic(true);
    make(u"strike"_s).setFontStrikeOut(true);
    make(u"highlight"_s).setBackground(color("highlightBackground", QColor(242, 179, 61, 97)));
    make(u"muted"_s).setForeground(muted);
    make(u"faint"_s).setForeground(faint);
    make(u"blockId"_s).setForeground(faint);
    make(u"property"_s).setForeground(muted);
    QTextCharFormat &comment = make(u"comment"_s);
    comment.setForeground(faint);
    comment.setFontItalic(true);
    make(u"due"_s).setForeground(color("due", link));
    make(u"scheduled"_s).setForeground(color("scheduled", QColor(u"#6a4f93"_s)));
    make(u"start"_s).setForeground(color("scheduled", QColor(u"#6a4f93"_s)));
    make(u"reminder"_s).setForeground(color("reminder", QColor(u"#7a4f00"_s)));
    QTextCharFormat &alarm = make(u"alarm"_s);
    alarm.setForeground(color("alarm", QColor(u"#7a4f00"_s)));
    alarm.setFontWeight(QFont::DemiBold);
    make(u"recurrence"_s).setForeground(color("recurrence", QColor(u"#2a6e44"_s)));
    make(u"done"_s).setForeground(color("done", muted));
    make(u"priority"_s).setForeground(color("priority", QColor(u"#a8382d"_s)));
}

void MarkdownHighlighter::setRange(int from, int length, const QTextCharFormat &format)
{
    if (length > 0)
        setFormat(from, length, format);
}

void MarkdownHighlighter::mergeRange(int from, int length, const QTextCharFormat &format)
{
    // Merge with what is already there, one run of equal formats at a time.
    int pos = from;
    const int end = from + length;
    while (pos < end) {
        const QTextCharFormat current = QSyntaxHighlighter::format(pos);
        int run = pos + 1;
        while (run < end && QSyntaxHighlighter::format(run) == current)
            ++run;
        QTextCharFormat merged = current;
        merged.merge(format);
        // Keep the most specific name for tests: the newly merged one.
        merged.setProperty(NameProperty, format.property(NameProperty));
        setFormat(pos, run - pos, merged);
        pos = run;
    }
}

void MarkdownHighlighter::highlightBlock(const QString &line)
{
    const int prev = previousBlockState() < 0 ? Normal : previousBlockState();
    setCurrentBlockState(Normal);

    if (m_mode == u"code") {
        static const QRegularExpression commentRe(u"^\\s*(#|//).*$"_s);
        if (commentRe.match(line).hasMatch())
            setRange(0, int(line.size()), m_formats[u"comment"_s]);
        return;
    }

    const bool source = m_mode == u"source";
    if (source) {
        const int kind = prev & StateMask;
        static const QRegularExpression fenceRe(u"^\\s{0,3}(`{3,}|~{3,})"_s);
        const auto fm = fenceRe.match(line);
        if (kind == Fence) {
            const QChar ch = (prev & 0x8) ? u'~' : u'`';
            const int len = prev >> 4;
            const QString trimmed = line.trimmed();
            if (fm.hasMatch() && fm.captured(1).front() == ch && fm.capturedLength(1) >= len
                && trimmed.size() == fm.capturedLength(1)) {
                setRange(0, int(line.size()), m_formats[u"fence"_s]);
                return;
            }
            setRange(0, int(line.size()), m_formats[u"code"_s]);
            setCurrentBlockState(prev);
            return;
        }
        if (kind == Frontmatter || (currentBlock().blockNumber() == 0 && line.trimmed() == u"---")) {
            const bool opening = kind != Frontmatter;
            if (!opening && (line.trimmed() == u"---" || line.trimmed() == u"...")) {
                setRange(0, int(line.size()), m_formats[u"fence"_s]);
                return;
            }
            if (opening) {
                setRange(0, int(line.size()), m_formats[u"fence"_s]);
            } else {
                static const QRegularExpression keyRe(u"^(\\s*[^:#\\s][^:]*:)"_s);
                const auto km = keyRe.match(line);
                setRange(0, int(line.size()), m_formats[u"muted"_s]);
                if (km.hasMatch())
                    setRange(0, km.capturedLength(1), m_formats[u"property"_s]);
            }
            setCurrentBlockState(Frontmatter);
            return;
        }
        if (fm.hasMatch()) {
            setRange(0, int(line.size()), m_formats[u"fence"_s]);
            setCurrentBlockState(encodeFence(fm.captured(1).front(), int(fm.capturedLength(1))));
            return;
        }
        if (kind == Comment) {
            const qsizetype close = line.indexOf(u"%%"_s);
            if (close < 0) {
                setRange(0, int(line.size()), m_formats[u"comment"_s]);
                setCurrentBlockState(Comment);
                return;
            }
            setRange(0, int(close + 2), m_formats[u"comment"_s]);
            highlightInline(line, int(close + 2), int(line.size() - close - 2));
            return;
        }
    }

    highlightInline(line, 0, int(line.size()));
    if (source) {
        // An odd number of %% opens a comment that continues on the next lines.
        const QString masked = markdown::maskInlineCode(line);
        if (line.count(u"%%"_s) % 2 == 1 && masked.count(u"%%"_s) == line.count(u"%%"_s)) {
            const qsizetype open = line.lastIndexOf(u"%%"_s);
            setRange(int(open), int(line.size() - open), m_formats[u"comment"_s]);
            setCurrentBlockState(Comment);
        }
    }
}

void MarkdownHighlighter::highlightInline(const QString &full, int offset, int length)
{
    const QString text = full.mid(offset, length);
    if (text.isEmpty())
        return;
    const auto at = [offset](qsizetype p) { return int(offset + p); };
    QString masked = text;

    // Inline code spans and %%comments%% first: nothing inside them is syntax.
    static const QRegularExpression codeRe(u"(`+)(.+?)\\1"_s);
    for (auto it = codeRe.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"code"_s]);
        setRange(at(m.capturedStart()), int(m.capturedLength(1)), m_formats[u"fence"_s]);
        setRange(at(m.capturedEnd() - m.capturedLength(1)), int(m.capturedLength(1)), m_formats[u"fence"_s]);
        for (qsizetype k = m.capturedStart(); k < m.capturedEnd(); ++k)
            masked[k] = u' ';
    }
    static const QRegularExpression commentRe(u"%%.*?%%"_s);
    for (auto it = commentRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"comment"_s]);
        for (qsizetype k = m.capturedStart(); k < m.capturedEnd(); ++k)
            masked[k] = u' ';
    }

    // Line-level syntax.
    int contentStart = 0;
    if (offset == 0) {
        static const QRegularExpression listRe(u"^(\\s*)([-*+]|\\d{1,9}[.)])(\\s+)"_s);
        static const QRegularExpression quoteRe(u"^(\\s*>\\s?)+"_s);
        static const QRegularExpression checkboxRe(u"^\\[[^\\]]\\](?=\\s|$)"_s);
        static const QRegularExpression keywordRe(u"^(TODO|DOING|DONE|LATER|NOW|WAITING|CANCELED|CANCELLED|IN-PROGRESS)(?=\\s|$)"_s);
        if (const auto q = quoteRe.match(masked); q.hasMatch()) {
            setRange(0, int(q.capturedLength()), m_formats[u"marker"_s]);
            contentStart = int(q.capturedLength());
            static const QRegularExpression calloutRe(u"^\\[![A-Za-z0-9_-]+\\][+-]?"_s);
            const auto c = calloutRe.matchView(QStringView(masked).sliced(contentStart));
            if (c.hasMatch()) {
                QTextCharFormat f = m_formats[u"muted"_s];
                f.setFontWeight(QFont::DemiBold);
                setRange(contentStart, int(c.capturedLength()), f);
            }
        }
        if (m_mode == u"source") {
            if (const auto l = listRe.matchView(QStringView(masked).sliced(contentStart)); l.hasMatch()) {
                setRange(contentStart + int(l.capturedStart(2)), int(l.capturedLength(2)), m_formats[u"marker"_s]);
                contentStart += int(l.capturedLength());
            }
        }
        const QStringView rest = QStringView(masked).sliced(contentStart);
        if (const auto c = checkboxRe.matchView(rest); c.hasMatch()) {
            setRange(contentStart, int(c.capturedLength()), m_formats[u"marker"_s]);
        } else if (const auto k = keywordRe.matchView(rest); k.hasMatch()) {
            QTextCharFormat f = m_formats[u"marker"_s];
            f.setFontWeight(QFont::DemiBold);
            setRange(contentStart, int(k.capturedLength()), f);
        }
        // Headings ("# Title", also inside list items).
        static const QRegularExpression headingRe(u"^(\\s{0,3})(#{1,6})(\\s+|$)"_s);
        if (const auto h = headingRe.matchView(rest); h.hasMatch()) {
            setRange(contentStart, int(rest.size()), m_formats[u"heading"_s]);
            QTextCharFormat mark = m_formats[u"heading"_s];
            mark.merge(m_formats[u"marker"_s]);
            mark.setProperty(NameProperty, u"marker"_s);
            setRange(contentStart + int(h.capturedStart(2)), int(h.capturedLength(2)), mark);
        }
        // key:: value
        static const QRegularExpression propRe(u"^\\s*([A-Za-z0-9_-]+)::"_s);
        if (const auto p = propRe.matchView(rest); p.hasMatch())
            setRange(contentStart + int(p.capturedStart(1)), int(p.capturedLength(1)) + 2, m_formats[u"property"_s]);
    }

    // Emphasis.
    const auto emphasis = [&](const QRegularExpression &re, const QString &name, int markLen) {
        for (auto it = re.globalMatch(masked); it.hasNext();) {
            const auto m = it.next();
            mergeRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[name]);
            mergeRange(at(m.capturedStart()), markLen, m_formats[u"marker"_s]);
            mergeRange(at(m.capturedEnd() - markLen), markLen, m_formats[u"marker"_s]);
        }
    };
    static const QRegularExpression boldRe(u"(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)\\1"_s);
    static const QRegularExpression italicRe(u"(?<![*\\w])\\*(?=[^\\s*])([^*\\n]+?)(?<=[^\\s*])\\*(?![*\\w])"_s);
    static const QRegularExpression italicUnderscoreRe = text::unicodeRegex(u"(?<![\\w_])_(?=[^\\s_])([^_\\n]+?)(?<=[^\\s_])_(?![\\w_])"_s);
    static const QRegularExpression strikeRe(u"~~(?=\\S)(.+?)(?<=\\S)~~"_s);
    static const QRegularExpression highlightRe(u"==(?=\\S)(.+?)(?<=\\S)=="_s);
    emphasis(boldRe, u"bold"_s, 2);
    emphasis(italicRe, u"italic"_s, 1);
    emphasis(italicUnderscoreRe, u"italic"_s, 1);
    emphasis(strikeRe, u"strike"_s, 2);
    emphasis(highlightRe, u"highlight"_s, 2);

    // Links.
    static const QRegularExpression wikiRe(u"!?\\[\\[([^\\[\\]\\n]+)\\]\\]"_s);
    for (auto it = wikiRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"link"_s]);
        const int open = int(m.capturedStart(1) - m.capturedStart());
        mergeRange(at(m.capturedStart()), open, m_formats[u"marker"_s]);
        mergeRange(at(m.capturedEnd() - 2), 2, m_formats[u"marker"_s]);
    }
    static const QRegularExpression refRe(u"!?\\(\\(([A-Za-z0-9-]+)\\)\\)"_s);
    for (auto it = refRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"blockRef"_s]);
    }
    static const QRegularExpression mdLinkRe(u"!?\\[([^\\]\\n]*)\\]\\(([^)\\n]*)\\)"_s);
    for (auto it = mdLinkRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() > 0 && masked[m.capturedStart() - 1] == u'[')
            continue;
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"marker"_s]);
        setRange(at(m.capturedStart(1)), int(m.capturedLength(1)), m_formats[u"link"_s]);
        setRange(at(m.capturedStart(2)), int(m.capturedLength(2)), m_formats[u"faint"_s]);
    }
    static const QRegularExpression urlRe(u"(?<![\\w(/])(https?://|mailto:)[^\\s<>()\\]]+"_s);
    for (auto it = urlRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        // Not inside a Markdown link target (already faint).
        if (QSyntaxHighlighter::format(at(m.capturedStart())).property(NameProperty).toString() == u"faint")
            continue;
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"url"_s]);
    }

    // Tags.
    static const QRegularExpression tagRe = text::unicodeRegex(u"(?:^|(?<=[\\s(]))#(\\[\\[[^\\]]+\\]\\]|[\\p{L}\\p{N}\\p{M}_\\-/]*[\\p{L}\\p{M}_\\-/][\\p{L}\\p{N}\\p{M}_\\-/]*)"_s);
    for (auto it = tagRe.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        setRange(at(m.capturedStart()), int(m.capturedLength()), m_formats[u"tag"_s]);
    }

    // Metadata tokens (dates, reminders, recurrence, priorities, block ids).
    for (const markdown::Token &t : markdown::findTokens(masked)) {
        QString name;
        switch (t.kind) {
        case markdown::TokenKind::Due: name = u"due"_s; break;
        case markdown::TokenKind::Scheduled: name = u"scheduled"_s; break;
        case markdown::TokenKind::Start: name = u"start"_s; break;
        case markdown::TokenKind::Created:
        case markdown::TokenKind::Done:
        case markdown::TokenKind::Cancelled: name = u"done"_s; break;
        case markdown::TokenKind::Recurrence: name = u"recurrence"_s; break;
        case markdown::TokenKind::Priority:
        case markdown::TokenKind::LogseqPriority: name = u"priority"_s; break;
        case markdown::TokenKind::Reminder:
        case markdown::TokenKind::ReminderCompat: name = t.alarm ? u"alarm"_s : u"reminder"_s; break;
        case markdown::TokenKind::Planning: name = t.field == markdown::TokenKind::Due ? u"due"_s : u"scheduled"_s; break;
        case markdown::TokenKind::TaskId:
        case markdown::TokenKind::DependsOn:
        case markdown::TokenKind::OnCompletion: name = u"muted"_s; break;
        }
        setRange(at(t.from), int(t.to - t.from), m_formats[name]);
    }
    if (const auto id = markdown::blockIdAtEnd(masked))
        setRange(at(id->from), int(id->to - id->from), m_formats[u"blockId"_s]);
}

} // namespace brulion::editor
