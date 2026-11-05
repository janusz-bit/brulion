#include "richtextpills.h"

#include "richtext_p.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// The anchor name of a page's app icon (InlineRenderer, markdown/inlinerenderer.h).
constexpr QStringView IconAnchorPrefix = u"brulion-icon:";

} // namespace

RichTextPills::RichTextPills(QObject *parent)
    : QObject(parent)
{
}

RichTextPills::~RichTextPills()
{
    detach();
}

void RichTextPills::classBegin()
{
    m_deferred = true;
}

void RichTextPills::componentComplete()
{
    m_deferred = false;
    if (m_ownsText)
        reload();
    else
        process();
}

void RichTextPills::setHtml(const QString &html)
{
    if (m_ownsText && m_html == html)
        return;
    m_html = html;
    m_ownsText = true;
    reload();
    Q_EMIT htmlChanged();
}

void RichTextPills::setMaximumLineCount(int lines)
{
    lines = std::max(0, lines);
    if (m_maxLines == lines)
        return;
    m_maxLines = lines;
    if (m_ownsText)
        reload();
    Q_EMIT maximumLineCountChanged();
}

void RichTextPills::setFont(const QFont &font)
{
    if (m_fontSet && m_font == font)
        return;
    m_font = font;
    m_fontSet = true;
    if (m_ownsText)
        reload();
    Q_EMIT fontChanged();
}

void RichTextPills::setWidth(qreal width)
{
    if (qFuzzyCompare(m_width + 1, width + 1))
        return;
    m_width = width;
    if (m_ownsText)
        reload();
    else
        refresh();
    Q_EMIT widthChanged();
}

void RichTextPills::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    attach(document ? document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void RichTextPills::setLineHeight(qreal px)
{
    if (qFuzzyCompare(m_lineHeight + 1, px + 1))
        return;
    m_lineHeight = px;
    if (m_ownsText)
        reload(); // (the line height decides where the text is cut)
    else
        process();
    Q_EMIT lineHeightChanged();
}

void RichTextPills::setInset(qreal px)
{
    if (qFuzzyCompare(m_inset + 1, px + 1))
        return;
    m_inset = px;
    refresh();
    Q_EMIT insetChanged();
}

QList<RichTextPill> RichTextPills::shapes() const
{
    QList<RichTextPill> out;
    out.reserve(m_pills.size());
    for (const Pill &p : m_pills) {
        out.append(RichTextPill{p.rect.x(), p.rect.y(), p.rect.width(), p.rect.height(), p.color,
                                !p.icon.isEmpty() ? u"icon"_s
                                : p.round         ? u"pill"_s
                                                  : u"box"_s,
                                p.icon});
    }
    return out;
}

QVariantList RichTextPills::pills() const
{
    QVariantList out;
    out.reserve(m_pills.size());
    for (const Pill &p : m_pills) {
        out.append(QVariantMap{
                {u"x"_s, p.rect.x()},
                {u"y"_s, p.rect.y()},
                {u"width"_s, p.rect.width()},
                {u"height"_s, p.rect.height()},
                {u"color"_s, p.color},
                {u"kind"_s, !p.icon.isEmpty() ? u"icon"_s
                            : p.round         ? u"pill"_s
                                              : u"box"_s},
                {u"icon"_s, p.icon},
        });
    }
    return out;
}

void RichTextPills::detach()
{
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_document = nullptr;
}

void RichTextPills::attach(QTextDocument *document)
{
    detach();
    m_document = document;
    if (document) {
        // The document's own change signal: InlineRenderer's HTML set by the TextEdit (setHtml, one
        // signal for the whole text) is handled before the TextEdit paints it.
        m_connections.append(connect(document, &QTextDocument::contentsChange, this,
                                     [this](int, int, int) { onContentsChange(); }));
        // A new width, font or text lays the document out again: the rectangles follow.
        QAbstractTextDocumentLayout *layout = document->documentLayout();
        m_connections.append(connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this,
                                     [this](const QSizeF &) { refresh(); }));
        m_connections.append(connect(layout, &QAbstractTextDocumentLayout::update, this,
                                     [this](const QRectF &) { refresh(); }));
        m_connections.append(connect(document, &QObject::destroyed, this, [this] {
            m_connections.clear();
            if (!m_pills.isEmpty()) {
                m_pills.clear();
                Q_EMIT pillsChanged();
            }
        }));
    }
    if (m_ownsText)
        reload();
    else
        process();
}

void RichTextPills::onContentsChange()
{
    if (m_changing)
        return;
    process();
}

void RichTextPills::reload()
{
    QTextDocument *doc = m_document;
    if (!doc || m_deferred || m_changing)
        return;
    m_changing = true;
    if (doc->isUndoRedoEnabled())
        doc->setUndoRedoEnabled(false);
    // The TextEdit's own font and width (it may set them on the document after this object).
    if (m_fontSet && doc->defaultFont() != m_font)
        doc->setDefaultFont(m_font);
    if (m_width > 0 && !qFuzzyCompare(doc->textWidth(), m_width))
        doc->setTextWidth(m_width);
    doc->setHtml(m_html);
    QTextCursor edit(doc);
    edit.beginEditBlock();
    applyLineHeight();
    takeBrushes();
    edit.endEditBlock();
    const bool cut = m_maxLines > 0 && cutToLines();
    m_changing = false;
    if (cut != m_truncated) {
        m_truncated = cut;
        Q_EMIT truncatedChanged();
    }
    refresh();
}

bool RichTextPills::cutToLines()
{
    QTextDocument *doc = m_document;
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    int before = 0; // lines of the blocks above
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        (void)layout->blockBoundingRect(block); // lays the block out (with the current width)
        const QTextLayout *text = block.layout();
        const int lines = text ? text->lineCount() : 0;
        if (before + lines < m_maxLines) {
            before += lines;
            continue;
        }
        // The last line shown is in this block: the text goes on after it?
        const QTextLine last = text->lineAt(m_maxLines - before - 1);
        const int cut = block.position() + last.textStart() + last.textLength();
        QTextCursor rest(doc);
        rest.setPosition(cut);
        rest.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        if (rest.selectedText().trimmed().isEmpty())
            return false; // (blanks, empty paragraphs)
        ellipsize(cut, m_maxLines - before);
        return true;
    }
    return false;
}

void RichTextPills::ellipsize(int cut, int lines)
{
    QTextDocument *doc = m_document;
    QTextCursor cursor(doc);
    cursor.setPosition(cut);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    // No blank (a space, a no-break space, a line break) before the "…".
    while (cursor.position() > cursor.block().position() && doc->characterAt(cursor.position() - 1).isSpace())
        cursor.deletePreviousChar();
    // The text's own colour, no link and no tint: "…" belongs to no token.
    QTextCharFormat format = cursor.charFormat();
    format.setAnchor(false);
    format.setAnchorHref(QString());
    format.setAnchorNames({});
    format.clearProperty(TintColorProperty);
    format.clearProperty(TintRoundProperty);
    format.clearBackground();
    format.clearForeground();
    format.setFontUnderline(false);
    format.setFontFixedPitch(false);
    cursor.insertText(QString(QChar(0x2026)), format);
    const QTextBlock block = cursor.block();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    // Shortened (by whole characters, then blanks) until "…" fits on the last line.
    for (int guard = 0; guard < 400; ++guard) {
        (void)layout->blockBoundingRect(block);
        const QTextLayout *text = block.layout();
        if (!text || text->lineCount() <= lines)
            break;
        QTextCursor before(doc);
        before.setPosition(cursor.position() - 1); // in front of the "…"
        if (before.position() <= block.position())
            break;
        before.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        before.removeSelectedText();
        while (before.position() > block.position() && doc->characterAt(before.position() - 1).isSpace())
            before.deletePreviousChar();
        cursor.setPosition(before.position() + 1);
    }
}

void RichTextPills::process()
{
    if (m_ownsText) {
        reload();
        return;
    }
    QTextDocument *doc = m_document;
    if (doc && !m_changing) {
        m_changing = true;
        // The rendered text has no undo history: format changes must not fill one.
        if (doc->isUndoRedoEnabled())
            doc->setUndoRedoEnabled(false);
        QTextCursor edit(doc);
        edit.beginEditBlock();
        applyLineHeight();
        takeBrushes();
        edit.endEditBlock(); // one contentsChange: the TextEdit repaints those blocks
        m_changing = false;
    }
    refresh();
}

void RichTextPills::applyLineHeight()
{
    if (m_lineHeight <= 0)
        return;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat current = block.blockFormat();
        if (current.lineHeightType() == QTextBlockFormat::FixedHeight && qFuzzyCompare(current.lineHeight(), m_lineHeight))
            continue;
        QTextBlockFormat f;
        f.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
        QTextCursor cursor(block);
        cursor.mergeBlockFormat(f);
    }
}

void RichTextPills::takeBrushes()
{
    // Collected first: changing a format splits and merges the fragments being walked.
    struct Change {
        int from;
        int length;
        QTextCharFormat format;
    };
    QList<Change> changes;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QTextCharFormat f = fragment.charFormat();
            if (!f.hasProperty(QTextFormat::BackgroundBrush) || f.background().style() == Qt::NoBrush)
                continue;
            // A chip or a tag has its own text colour; inline code is fixed-pitch, a highlight
            // keeps the text's colour (InlineRenderer: chipStyle, the tag's link, <code>, ==mark==).
            const bool round = !f.fontFixedPitch() && f.hasProperty(QTextFormat::ForegroundBrush);
            f.setProperty(TintColorProperty, f.background().color());
            f.setProperty(TintRoundProperty, round);
            f.clearBackground();
            changes.append({fragment.position(), fragment.length(), f});
        }
    }
    for (const Change &c : std::as_const(changes)) {
        QTextCursor cursor(m_document);
        cursor.setPosition(c.from);
        cursor.setPosition(c.from + c.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(c.format);
    }
}

void RichTextPills::refresh()
{
    if (m_computing || m_changing)
        return;
    QList<Pill> out;
    if (QTextDocument *doc = m_document) {
        m_computing = true;
        QAbstractTextDocumentLayout *layout = doc->documentLayout();
        const QFont base = doc->defaultFont();
        struct Span {
            int from;
            int to;
            QColor color;
            bool round;
            QFont font;
            QString icon;
            QString href; ///< an icon's link
        };
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            // The block's tinted spans: adjacent fragments of one colour are one span.
            QList<Span> spans;
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (!fragment.isValid())
                    continue;
                const QTextCharFormat f = fragment.charFormat();
                const QVariant tint = f.property(TintColorProperty);
                if (!tint.isValid())
                    continue;
                QColor color = tint.value<QColor>();
                const bool round = f.property(TintRoundProperty).toBool();
                // A page's app icon (InlineRenderer's iconLink): drawn in the link's colour.
                QString icon;
                for (const QString &name : f.anchorNames()) {
                    if (name.startsWith(IconAnchorPrefix)) {
                        icon = name.sliced(IconAnchorPrefix.size());
                        color = f.foreground().color();
                        break;
                    }
                }
                // Qt names only the anchor's first character: the rest of the icon's room (its
                // other no-break spaces, the same link, still transparent) is the icon's too.
                if (icon.isEmpty() && !spans.isEmpty() && !spans.last().icon.isEmpty()
                    && spans.last().to == fragment.position() && spans.last().href == f.anchorHref()
                    && color.alpha() == 0) {
                    spans.last().to += fragment.length();
                    continue;
                }
                if (!spans.isEmpty() && spans.last().to == fragment.position() && spans.last().color == color
                    && spans.last().icon == icon && icon.isEmpty()) {
                    spans.last().to += fragment.length();
                    spans.last().round = spans.last().round && round;
                } else {
                    spans.append({fragment.position(), fragment.position() + fragment.length(), color, round,
                                  f.font().resolve(base), icon, icon.isEmpty() ? QString() : f.anchorHref()});
                }
            }
            if (spans.isEmpty())
                continue;
            for (const Span &span : std::as_const(spans)) {
                const QFontMetricsF metrics(span.font);
                for (const detail::LineSegment &segment : detail::lineSegments(layout, block, span.from, span.to)) {
                    const QPointF origin = block.layout()->position();
                    // Centred on the span's glyphs, not on the fixed-height line box.
                    const qreal baseline = segment.line.y() + segment.line.ascent();
                    const qreal top = baseline - metrics.ascent() + m_inset;
                    const qreal bottom = baseline + metrics.descent() - m_inset;
                    out.append({QRectF(origin.x() + segment.x1, origin.y() + top, segment.x2 - segment.x1,
                                       std::max<qreal>(1, bottom - top)),
                                span.color, span.round, span.from, span.to, span.icon});
                }
            }
        }
        m_computing = false;
    }
    if (out != m_pills) {
        m_pills = out;
        Q_EMIT pillsChanged();
    }
}

} // namespace brulion::editor
