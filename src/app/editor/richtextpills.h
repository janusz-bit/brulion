// RichTextPills — rounded shapes for the tinted spans of a block's rendered text (docs/UI.md "The
// block editor"). InlineRenderer's chips and tags, inline `code` and ==highlights== are HTML spans
// with a background colour, which Qt's rich text paints as square boxes the height of the line.
// Attached to the QTextDocument of a read-only rich-text TextEdit (`textDocument`), it
//   1. gives every paragraph the fixed line height `lineHeight` (QTextBlockFormat::FixedHeight,
//      as MarkdownHighlighter does for the edited text and QML Text for its rich text), so the
//      rendered and the edited block have the same lines;
//   2. takes the background brush off every such span in the document's own change signal —
//      synchronously, before the next frame, so no square is ever painted — and keeps its colour
//      and kind on the span's format (user properties), so a later change of the layout or of
//      other formats finds them again;
//   3. exposes `pills`: one rectangle per line segment of each span ({x, y, width, height, color,
//      kind}, in the document's coordinates — the TextEdit's with textMargin 0), from the
//      document's own layout (QTextLine::cursorToX), vertically centred on the span's glyphs:
//      its font's ascent above and descent below the line's baseline, less `inset` on each side.
//      Recomputed when the layout changes (width, font, text) and only announced when different.
// kind: "pill" for a span with its own text colour (a chip, a tag: radius = height / 2), "box" for
// a fixed-pitch span (inline code) or any other background (a highlight: a small radius). Adjacent
// fragments of one colour are one span (a highlight with bold words in it).
//
//   TextEdit { id: edit; textFormat: TextEdit.RichText; readOnly: true; text: html }
//   RichTextPills { id: pills; textDocument: edit.textDocument; lineHeight: 26 }
//   Repeater { model: pills.pills
//              delegate: Rectangle { required property var modelData; x: modelData.x; …
//                                    radius: modelData.kind === "pill" ? height / 2 : 3 } }
#pragma once

#include <QColor>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QRectF>
#include <QVariantList>

class QTextDocument;

namespace brulion::editor {

class RichTextPills : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged FINAL)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged FINAL)
    Q_PROPERTY(qreal inset READ inset WRITE setInset NOTIFY insetChanged FINAL)
    Q_PROPERTY(QVariantList pills READ pills NOTIFY pillsChanged FINAL)

public:
    /// One line segment of a tinted span.
    struct Pill {
        QRectF rect;
        QColor color;
        bool round = true; ///< "pill" (else "box")
        int from = 0;      ///< document positions of the whole span
        int to = 0;
        bool operator==(const Pill &other) const = default;
    };

    explicit RichTextPills(QObject *parent = nullptr);
    ~RichTextPills() override;

    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal px);
    qreal inset() const { return m_inset; }
    void setInset(qreal px);
    QVariantList pills() const;

    /// Attach to a plain QTextDocument (C++/tests); what setTextDocument does for a TextEdit's.
    void attach(QTextDocument *document);
    /// The current segments (tests).
    const QList<Pill> &pillList() const { return m_pills; }
    /// Recomputes the rectangles from the current layout (QML calls it when the item's width
    /// changes; layout changes call it by themselves).
    Q_INVOKABLE void refresh();

    /// The property ids kept on a span's format once its brush is gone (tests).
    static constexpr int TintColorProperty = 0x100000 + 0x7101; // QTextFormat::UserProperty + …
    static constexpr int TintRoundProperty = 0x100000 + 0x7102;

Q_SIGNALS:
    void textDocumentChanged();
    void lineHeightChanged();
    void insetChanged();
    void pillsChanged();

private:
    void onContentsChange();
    /// Fixed line heights and the brushes taken off (one edit block), then refresh().
    void process();
    void applyLineHeight();
    void takeBrushes();
    void detach();

    QPointer<QQuickTextDocument> m_quickDocument;
    QPointer<QTextDocument> m_document;
    QList<QMetaObject::Connection> m_connections;
    qreal m_lineHeight = 0;
    qreal m_inset = 0;
    QList<Pill> m_pills;
    bool m_changing = false;  ///< our own document changes: their signals are ignored
    bool m_computing = false; ///< in refresh(): layout signals it causes are ignored
};

} // namespace brulion::editor
