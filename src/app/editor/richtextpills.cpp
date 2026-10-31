#include "richtextpills.h"

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

RichTextPills::RichTextPills(QObject *parent)
    : QObject(parent)
{
}

RichTextPills::~RichTextPills()
{
    detach();
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
                {u"kind"_s, p.round ? u"pill"_s : u"box"_s},
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
    process();
}

void RichTextPills::onContentsChange()
{
    if (m_changing)
        return;
    process();
}

void RichTextPills::process()
{
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
                const QColor color = tint.value<QColor>();
                const bool round = f.property(TintRoundProperty).toBool();
                if (!spans.isEmpty() && spans.last().to == fragment.position() && spans.last().color == color) {
                    spans.last().to += fragment.length();
                    spans.last().round = spans.last().round && round;
                } else {
                    spans.append({fragment.position(), fragment.position() + fragment.length(), color, round,
                                  f.font().resolve(base)});
                }
            }
            if (spans.isEmpty())
                continue;
            (void)layout->blockBoundingRect(block); // lays the block out (with the current width)
            const QTextLayout *text = block.layout();
            if (!text)
                continue;
            const QPointF origin = text->position();
            const int at = block.position();
            for (const Span &span : std::as_const(spans)) {
                const QFontMetricsF metrics(span.font);
                for (int i = 0; i < text->lineCount(); ++i) {
                    const QTextLine line = text->lineAt(i);
                    const int lineEnd = line.textStart() + line.textLength();
                    const int start = std::max(span.from - at, line.textStart());
                    const int end = std::min(span.to - at, lineEnd);
                    if (start >= end)
                        continue;
                    qreal x1 = line.cursorToX(start);
                    qreal x2 = line.cursorToX(end);
                    if (x2 < x1)
                        std::swap(x1, x2);
                    // A span going on in the next line: not the space the line broke at.
                    if (span.to - at > lineEnd)
                        x2 = std::min(x2, line.naturalTextRect().right());
                    if (x2 - x1 < 0.5)
                        continue;
                    // Centred on the span's glyphs, not on the fixed-height line box.
                    const qreal baseline = line.y() + line.ascent();
                    const qreal top = baseline - metrics.ascent() + m_inset;
                    const qreal bottom = baseline + metrics.descent() - m_inset;
                    out.append({QRectF(origin.x() + x1, origin.y() + top, x2 - x1, std::max<qreal>(1, bottom - top)),
                                span.color, span.round, span.from, span.to});
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
