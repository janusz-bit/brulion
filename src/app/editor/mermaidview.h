// MermaidView — a ```mermaid block drawn (docs/UI.md "The block editor", docs/SYNTAX.md §2
// "Diagrams"): the core module diagram parses and lays out `source` (Brulion's own renderer,
// nothing from the note is executed) and this item paints its scene with QPainter in the theme's
// colours — QML passes Theme's text, muted text, background, border and accent colours and
// `dark` (core diagram::themedPalette: Obsidian-like, nodes tinted with the accent). Text is
// measured with the app's font (`fontFamily`, `fontPixelSize`).
//
// The diagram is drawn at its natural size, scaled down to the item's width when wider (never
// up) and centred; implicitWidth/implicitHeight follow (the height for the current width).
// `status`: "ok", "unsupported" (a Mermaid type Brulion does not draw yet: `typeName`), "error"
// (`message`: translated, with the line). Laid-out sources are kept in a small cache shared by all
// views (a delegate made again while scrolling lays nothing out again); colours only repaint.
//
//   MermaidView { width: parent.width; source: block.model.editText; fontFamily: Theme.fontUi
//                 fontPixelSize: Theme.textBase; textColor: Theme.fg; … dark: Theme.dark }
#pragma once

#include "diagram/diagram.h"

#include <QColor>
#include <QQuickPaintedItem>
#include <QString>

#include <memory>

namespace brulion::editor {

class MermaidView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged FINAL)
    Q_PROPERTY(qreal fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontChanged FINAL)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor mutedColor READ mutedColor WRITE setMutedColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY colorsChanged FINAL)
    /// "ok" | "unsupported" | "error".
    Q_PROPERTY(QString status READ status NOTIFY resultChanged FINAL)
    /// What went wrong (translated), "" when drawn.
    Q_PROPERTY(QString message READ message NOTIFY resultChanged FINAL)
    /// The Mermaid type ("flowchart", "classDiagram", …), "" when none.
    Q_PROPERTY(QString typeName READ typeName NOTIFY resultChanged FINAL)
    /// The diagram's title, or a description of it (accessibility).
    Q_PROPERTY(QString description READ description NOTIFY resultChanged FINAL)
    /// The diagram's natural size (0 × 0 when not drawn).
    Q_PROPERTY(QSizeF diagramSize READ diagramSize NOTIFY resultChanged FINAL)

public:
    explicit MermaidView(QQuickItem *parent = nullptr);
    ~MermaidView() override;

    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &family);
    qreal fontPixelSize() const { return m_fontPixelSize; }
    void setFontPixelSize(qreal size);
    QColor textColor() const { return m_text; }
    void setTextColor(const QColor &c);
    QColor mutedColor() const { return m_muted; }
    void setMutedColor(const QColor &c);
    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &c);
    QColor borderColor() const { return m_border; }
    void setBorderColor(const QColor &c);
    QColor accentColor() const { return m_accent; }
    void setAccentColor(const QColor &c);
    bool dark() const { return m_dark; }
    void setDark(bool dark);

    QString status() const;
    QString message() const;
    QString typeName() const;
    QString description() const;
    QSizeF diagramSize() const;

    void paint(QPainter *painter) override;

    /// The translated text of `error` (the type for Unsupported).
    static QString errorMessage(const diagram::Error &error, const QString &typeName);
    /// Paints `scene` in `palette`'s colours, text in `family` (tests, screenshots).
    static void paintScene(QPainter *painter, const diagram::Scene &scene, const diagram::Palette &palette,
                           const QString &family);
    /// Metrics measuring with the app's font `family`.
    static diagram::Metrics fontMetrics(const QString &family, qreal pixelSize);
    /// The result for `source` laid out in `family` at `pixelSize`, from the shared cache.
    static std::shared_ptr<const diagram::Result> layout(const QString &source, const QString &family, qreal pixelSize);

Q_SIGNALS:
    void sourceChanged();
    void fontChanged();
    void colorsChanged();
    void resultChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void relayout();
    void updateImplicitSize();
    diagram::Palette palette() const;

    QString m_source;
    QString m_fontFamily;
    qreal m_fontPixelSize = 14;
    QColor m_text = QColor(0x1f, 0x24, 0x30);
    QColor m_muted = QColor(0x53, 0x5a, 0x6a);
    QColor m_background = QColor(0xfb, 0xf7, 0xee);
    QColor m_border = QColor(0xdd, 0xd6, 0xc6);
    QColor m_accent = QColor(0x2c, 0x6a, 0xa3);
    bool m_dark = false;
    std::shared_ptr<const diagram::Result> m_result;
};

} // namespace brulion::editor
