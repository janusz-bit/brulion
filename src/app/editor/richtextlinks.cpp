#include "richtextlinks.h"

#include "richtext_p.h"

#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextOption>

using namespace Qt::StringLiterals;

namespace brulion::editor {

RichTextLinks::RichTextLinks(QObject *parent) : QObject(parent) { }

RichTextLinks::~RichTextLinks()
{
    detach();
}

void RichTextLinks::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    refresh();
    Q_EMIT enabledChanged();
}

void RichTextLinks::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    attach(document ? document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void RichTextLinks::setHtml(const QString &html)
{
    if (m_html == html)
        return;
    m_html = html;
    m_ownDirty = true;
    refresh();
    Q_EMIT htmlChanged();
}

void RichTextLinks::setFont(const QFont &font)
{
    if (m_font == font)
        return;
    m_font = font;
    m_ownDirty = true;
    refresh();
    Q_EMIT fontChanged();
}

void RichTextLinks::setWidth(qreal width)
{
    if (qFuzzyCompare(m_width + 1, width + 1))
        return;
    m_width = width;
    if (m_own)
        m_own->setTextWidth(m_width > 0 ? m_width : -1);
    refresh();
    Q_EMIT widthChanged();
}

void RichTextLinks::setLineHeight(qreal px)
{
    if (qFuzzyCompare(m_lineHeight + 1, px + 1))
        return;
    m_lineHeight = px;
    m_ownDirty = true;
    refresh();
    Q_EMIT lineHeightChanged();
}

void RichTextLinks::setAlignment(int alignment)
{
    if (m_alignment == alignment)
        return;
    m_alignment = alignment;
    m_ownDirty = true;
    refresh();
    Q_EMIT alignmentChanged();
}

void RichTextLinks::setMaxHeight(qreal px)
{
    if (qFuzzyCompare(m_maxHeight + 1, px + 1))
        return;
    m_maxHeight = px;
    refresh();
    Q_EMIT maxHeightChanged();
}

QVariantList RichTextLinks::links() const
{
    QVariantList out;
    out.reserve(m_links.size());
    for (const Link &l : m_links) {
        QVariantList rects;
        for (const QRectF &r : l.rects)
            rects.append(
                    QVariantMap{{u"x"_s, r.x()}, {u"y"_s, r.y()}, {u"width"_s, r.width()}, {u"height"_s, r.height()}});
        out.append(QVariantMap{{u"href"_s, l.href},
                               {u"text"_s, l.text},
                               {u"x"_s, l.rect.x()},
                               {u"y"_s, l.rect.y()},
                               {u"width"_s, l.rect.width()},
                               {u"height"_s, l.rect.height()},
                               {u"rects"_s, rects}});
    }
    return out;
}

void RichTextLinks::detach()
{
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    m_document = nullptr;
}

void RichTextLinks::attach(QTextDocument *document)
{
    detach();
    m_document = document;
    if (document) {
        // A new text, width or font lays the document out again: the links follow.
        QAbstractTextDocumentLayout *layout = document->documentLayout();
        m_connections.append(connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this,
                                     [this](const QSizeF &) { refresh(); }));
        m_connections.append(connect(document, &QTextDocument::contentsChanged, this, [this] { refresh(); }));
        m_connections.append(connect(document, &QObject::destroyed, this, [this] {
            m_connections.clear();
            if (!m_links.isEmpty()) {
                m_links.clear();
                Q_EMIT linksChanged();
            }
        }));
    }
    refresh();
}

QTextDocument *RichTextLinks::source()
{
    if (m_document)
        return m_document;
    if (m_html.isEmpty())
        return nullptr;
    if (!m_own) {
        m_own = std::make_unique<QTextDocument>();
        m_own->setUndoRedoEnabled(false);
        m_own->setDocumentMargin(0);
        m_ownDirty = true;
    }
    if (m_ownDirty) {
        m_own->setDefaultFont(m_font);
        QTextOption option = m_own->defaultTextOption();
        option.setAlignment(Qt::Alignment(m_alignment));
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_own->setDefaultTextOption(option);
        m_own->setHtml(m_html);
        if (m_lineHeight > 0) {
            // As QML Text lays out rich text with a fixed line height.
            QTextCursor cursor(m_own.get());
            cursor.select(QTextCursor::Document);
            QTextBlockFormat f;
            f.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
            cursor.mergeBlockFormat(f);
        }
        m_own->setTextWidth(m_width > 0 ? m_width : -1);
        m_ownDirty = false;
    }
    return m_own.get();
}

void RichTextLinks::refresh()
{
    if (m_computing)
        return;
    QList<Link> out;
    if (m_enabled) {
        m_computing = true;
        if (QTextDocument *doc = source()) {
            out = linksOf(doc, m_lineHeight);
            if (m_maxHeight > 0) {
                for (auto it = out.begin(); it != out.end();) {
                    it->rects.removeIf([this](const QRectF &r) { return r.top() >= m_maxHeight - 0.5; });
                    if (it->rects.isEmpty()) {
                        it = out.erase(it);
                    } else {
                        it->rect = it->rects.constFirst();
                        ++it;
                    }
                }
            }
        }
        m_computing = false;
    } else if (m_own) {
        m_own.reset(); // nothing kept while off
    }
    if (out != m_links) {
        m_links = out;
        Q_EMIT linksChanged();
    }
}

QList<RichTextLinks::Link> RichTextLinks::linksOf(QTextDocument *document, qreal lineHeight)
{
    QList<Link> out;
    if (!document)
        return out;
    QAbstractTextDocumentLayout *layout = document->documentLayout();
    struct Anchor {
        QString href;
        QString text;
        int from;
        int to;
    };
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        // The block's anchors: adjacent fragments with one href are one link.
        QList<Anchor> anchors;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat f = fragment.charFormat();
            if (!f.isAnchor() || f.anchorHref().isEmpty() || f.anchorHref().startsWith(u"task:"))
                continue;
            const QString href = f.anchorHref();
            if (!anchors.isEmpty() && anchors.last().to == fragment.position() && anchors.last().href == href) {
                anchors.last().to += fragment.length();
                anchors.last().text += fragment.text();
            } else {
                anchors.append({href, fragment.text(), fragment.position(), fragment.position() + fragment.length()});
            }
        }
        if (anchors.isEmpty())
            continue;
        for (const Anchor &a : std::as_const(anchors)) {
            Link link;
            link.href = a.href;
            // No-break spaces read as spaces; the text the screen reader says.
            link.text = QString(a.text).replace(QChar(0x00A0), u' ').trimmed();
            for (const detail::LineSegment &segment : detail::lineSegments(layout, block, a.from, a.to)) {
                const QPointF origin = block.layout()->position();
                const qreal height = lineHeight > 0 ? lineHeight : segment.line.height();
                link.rects.append(QRectF(origin.x() + segment.x1, origin.y() + segment.line.y(),
                                         segment.x2 - segment.x1, height));
            }
            if (link.rects.isEmpty())
                continue;
            link.rect = link.rects.constFirst();
            out.append(link);
        }
    }
    return out;
}

} // namespace brulion::editor
