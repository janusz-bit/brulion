// RichTextLinks — the links (anchors) of a rendered rich text and where they are: the accessible
// link items of BLinkTargets.qml (screen readers, Android's UiAutomator; docs/UI.md "Links in
// rendered text"), so every link of a block's text — each path part of [[A/B/C]] too — is a link
// of its own for them, not one static text.
//
// One entry per anchor in document order: consecutive fragments with the same href are one link
// (a label with <b> inside), two anchors with the same href apart are two; `task:` is left out
// (TaskBox is the checkbox's accessible item). Geometry comes from a TextEdit's own document
// (`textDocument`: the tinted text of BlockText, with RichTextPills' fixed line height) or — Qt
// Quick's Text has no layout API — from the same HTML laid out again with `font`, `width`, the
// fixed `lineHeight` and `alignment` (QML Text's own layout of rich text: a QTextDocument without
// margin, its blocks at the fixed line height). Nothing is laid out while `enabled` is false.
//
//   RichTextLinks { enabled: DeviceInfo.screenReader; html: text.text; font: text.font
//                   width: text.width; lineHeight: 26 }
//   Repeater { model: links.links; delegate: Item { x: modelData.x; … Accessible.role: Accessible.Link } }
#pragma once

#include <QFont>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QRectF>
#include <QString>
#include <QVariantList>

#include <memory>

class QTextDocument;

namespace brulion::editor {

class RichTextLinks : public QObject {
    Q_OBJECT
    /// Off: no links and nothing laid out (DeviceInfo.screenReader in BLinkTargets).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    /// The document of a rich-text TextEdit (its own layout is read).
    Q_PROPERTY(
            QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged FINAL)
    /// The rich text of a Text item, laid out here with font, width, lineHeight and alignment
    /// (used when there is no textDocument).
    Q_PROPERTY(QString html READ html WRITE setHtml NOTIFY htmlChanged FINAL)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
    Q_PROPERTY(qreal width READ width WRITE setWidth NOTIFY widthChanged FINAL)
    /// The fixed line height of the text (0: the font's).
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged FINAL)
    /// Qt::Alignment of the lines (Text.horizontalAlignment).
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged FINAL)
    /// Only what starts above this (an elided row's visible lines); 0: everything.
    Q_PROPERTY(qreal maxHeight READ maxHeight WRITE setMaxHeight NOTIFY maxHeightChanged FINAL)
    /// [{href, text, x, y, width, height, rects}] — the rectangle of the anchor's first line
    /// segment (the text item's coordinates); `rects`: every segment's [{x, y, width, height}].
    Q_PROPERTY(QVariantList links READ links NOTIFY linksChanged FINAL)

public:
    struct Link {
        QString href;
        QString text;
        QRectF rect;
        QList<QRectF> rects;
        bool operator==(const Link &other) const = default;
    };

    explicit RichTextLinks(QObject *parent = nullptr);
    ~RichTextLinks() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    QString html() const { return m_html; }
    void setHtml(const QString &html);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    qreal width() const { return m_width; }
    void setWidth(qreal width);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal px);
    int alignment() const { return m_alignment; }
    void setAlignment(int alignment);
    qreal maxHeight() const { return m_maxHeight; }
    void setMaxHeight(qreal px);
    QVariantList links() const;

    /// Read a plain QTextDocument (C++/tests) as setTextDocument() reads a TextEdit's.
    void attach(QTextDocument *document);
    /// The current links (tests).
    const QList<Link> &linkList() const { return m_links; }
    /// Read the links again (layout changes of an attached document do it by themselves).
    Q_INVOKABLE void refresh();

    /// The links of a laid-out document (pure: tests); `lineHeight` > 0: each segment that tall
    /// (the fixed line box), else the line's own height.
    static QList<Link> linksOf(QTextDocument *document, qreal lineHeight = 0);

Q_SIGNALS:
    void enabledChanged();
    void textDocumentChanged();
    void htmlChanged();
    void fontChanged();
    void widthChanged();
    void lineHeightChanged();
    void alignmentChanged();
    void maxHeightChanged();
    void linksChanged();

private:
    void detach();
    /// The document read: the attached one, else the own one laid out from `html`.
    QTextDocument *source();

    bool m_enabled = true;
    QPointer<QQuickTextDocument> m_quickDocument;
    QPointer<QTextDocument> m_document;
    QList<QMetaObject::Connection> m_connections;
    std::unique_ptr<QTextDocument> m_own; ///< the `html` mode's document
    bool m_ownDirty = true;
    QString m_html;
    QFont m_font;
    qreal m_width = 0;
    qreal m_lineHeight = 0;
    int m_alignment = Qt::AlignLeft;
    qreal m_maxHeight = 0;
    QList<Link> m_links;
    bool m_computing = false;
};

} // namespace brulion::editor
