// ChartView — a ```chart block drawn (docs/SYNTAX.md §9 "Charts", docs/UI.md "The block editor"):
// BlockListModel's `chartData` holds the numbers the block names (chart::resolveData: the table,
// calc block or query of the page), this item lays them out at its width with the app's font
// (chart::layoutChart — Brulion's own code, nothing from the note is executed) and paints the
// scene with MermaidView's painter in the theme's colours (diagram::themedPalette: series in the
// palette's slice colours, light and dark). Its height follows the width (implicitHeight).
//
// `status`: "ok" or "error" (`message`: translated, with the line); `description` is the text
// alternative (chart::describeChart: the type, title and every value) for screen readers.
//
//   ChartView { width: parent.width; source: block.model.editText; chartData: block.model.chartData
//               english: …; fontFamily: Theme.fontUi; fontPixelSize: Theme.textSm; textColor: …
//               dark: Theme.dark }
#pragma once

#include "chart/chart.h"

#include <QColor>
#include <QQuickPaintedItem>
#include <QString>
#include <QVariantMap>

namespace brulion::editor {

class ChartView : public QQuickPaintedItem {
    Q_OBJECT
    /// The block's text (between the fences): the type and title.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    /// BlockListModel's chartData of the block.
    Q_PROPERTY(QVariantMap chartData READ chartData WRITE setChartData NOTIFY chartDataChanged FINAL)
    /// Numbers of the axis in English (`12.5`) instead of Polish (`12,5`).
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY chartDataChanged FINAL)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged FINAL)
    Q_PROPERTY(qreal fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontChanged FINAL)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor mutedColor READ mutedColor WRITE setMutedColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY colorsChanged FINAL)
    /// "ok" | "error".
    Q_PROPERTY(QString status READ status NOTIFY resultChanged FINAL)
    /// Why nothing is drawn (translated), "" when drawn.
    Q_PROPERTY(QString message READ message NOTIFY resultChanged FINAL)
    /// The text alternative ("" when nothing is drawn).
    Q_PROPERTY(QString description READ description NOTIFY resultChanged FINAL)
    /// The chart's type in words ("Wykres słupkowy").
    Q_PROPERTY(QString typeName READ typeName NOTIFY resultChanged FINAL)

public:
    explicit ChartView(QQuickItem *parent = nullptr);
    ~ChartView() override;

    QString source() const { return m_source; }
    void setSource(const QString &source);
    QVariantMap chartData() const { return m_data; }
    void setChartData(const QVariantMap &data);
    bool english() const { return m_english; }
    void setEnglish(bool english);
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
    QString description() const;
    QString typeName() const;
    /// The laid-out scene (tests).
    const diagram::Scene &scene() const { return m_scene; }

    void paint(QPainter *painter) override;

    /// The chart data of a chartData map (BlockListModel): labels and series (null values: gaps).
    static chart::Data dataFromMap(const QVariantMap &map);

Q_SIGNALS:
    void sourceChanged();
    void chartDataChanged();
    void fontChanged();
    void colorsChanged();
    void resultChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void reread();
    void relayout();
    diagram::Palette palette() const;

    QString m_source;
    QVariantMap m_data;
    bool m_english = false;
    QString m_fontFamily;
    qreal m_fontPixelSize = 13;
    QColor m_text = QColor(0x1f, 0x24, 0x30);
    QColor m_muted = QColor(0x53, 0x5a, 0x6a);
    QColor m_background = QColor(0xfb, 0xf7, 0xee);
    QColor m_border = QColor(0xdd, 0xd6, 0xc6);
    QColor m_accent = QColor(0x2c, 0x6a, 0xa3);
    bool m_dark = false;

    chart::Spec m_spec;
    chart::Data m_chart;
    QString m_message;
    QString m_description;
    diagram::Scene m_scene;
    qreal m_laidOutWidth = -1;
};

} // namespace brulion::editor
