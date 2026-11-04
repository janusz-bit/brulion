#include "chartview.h"

#include "mermaidview.h"

#include <QPainter>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::editor {

ChartView::ChartView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    reread();
}

ChartView::~ChartView() = default;

void ChartView::setSource(const QString &source)
{
    if (m_source == source)
        return;
    m_source = source;
    Q_EMIT sourceChanged();
    reread();
}

void ChartView::setChartData(const QVariantMap &data)
{
    if (m_data == data)
        return;
    m_data = data;
    Q_EMIT chartDataChanged();
    reread();
}

void ChartView::setEnglish(bool english)
{
    if (m_english == english)
        return;
    m_english = english;
    Q_EMIT chartDataChanged();
    reread();
}

void ChartView::setFontFamily(const QString &family)
{
    if (m_fontFamily == family)
        return;
    m_fontFamily = family;
    Q_EMIT fontChanged();
    relayout();
}

void ChartView::setFontPixelSize(qreal size)
{
    if (qFuzzyCompare(m_fontPixelSize, size) || !(size > 0))
        return;
    m_fontPixelSize = size;
    Q_EMIT fontChanged();
    relayout();
}

#define BRULION_CHART_COLOR_SETTER(name, member)                                                                       \
    void ChartView::name(const QColor &c)                                                                              \
    {                                                                                                                  \
        if (member == c)                                                                                               \
            return;                                                                                                    \
        member = c;                                                                                                    \
        Q_EMIT colorsChanged();                                                                                        \
        update();                                                                                                      \
    }
BRULION_CHART_COLOR_SETTER(setTextColor, m_text)
BRULION_CHART_COLOR_SETTER(setMutedColor, m_muted)
BRULION_CHART_COLOR_SETTER(setBackgroundColor, m_background)
BRULION_CHART_COLOR_SETTER(setBorderColor, m_border)
BRULION_CHART_COLOR_SETTER(setAccentColor, m_accent)
#undef BRULION_CHART_COLOR_SETTER

void ChartView::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    Q_EMIT colorsChanged();
    update();
}

QString ChartView::status() const
{
    return m_message.isEmpty() ? u"ok"_s : u"error"_s;
}

QString ChartView::message() const
{
    return m_message;
}

QString ChartView::description() const
{
    return m_description;
}

QString ChartView::typeName() const
{
    return chart::typeName(m_spec.type);
}

chart::Data ChartView::dataFromMap(const QVariantMap &map)
{
    chart::Data out;
    out.labels = map.value(u"labels"_s).toStringList();
    out.labelsName = map.value(u"labelsName"_s).toString();
    const QVariantList series = map.value(u"series"_s).toList();
    for (qsizetype s = 0; s < series.size() && s < chart::limits::MaxSeries; ++s) {
        const QVariantMap m = series.at(s).toMap();
        chart::Series serie;
        serie.name = m.value(u"name"_s).toString();
        const QVariantList values = m.value(u"values"_s).toList();
        for (qsizetype i = 0; i < out.labels.size(); ++i) {
            const QVariant v = i < values.size() ? values.at(i) : QVariant();
            bool ok = false;
            const double d = v.isNull() ? 0 : v.toDouble(&ok);
            serie.values.append(ok && std::isfinite(d) ? d : std::numeric_limits<double>::quiet_NaN());
        }
        out.series.append(std::move(serie));
    }
    if (out.labels.size() > chart::limits::MaxPoints)
        return {};
    return out;
}

void ChartView::reread()
{
    const chart::SpecResult spec = chart::parseSpec(m_source);
    m_spec = spec.spec;
    m_chart = {};
    if (spec.error) {
        m_message = spec.error->message();
    } else if (!m_data.value(u"ok"_s).toBool()) {
        m_message = m_data.value(u"error"_s).toString();
        if (m_message.isEmpty())
            m_message = tr("The chart is being prepared.");
    } else {
        m_chart = dataFromMap(m_data);
        m_message = m_chart.series.isEmpty() ? tr("There are no numbers to draw.") : QString();
    }
    const calc::CalcLocale locale = m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    m_description = m_message.isEmpty() ? chart::describeChart(m_spec, m_chart, locale) : QString();
    m_laidOutWidth = -1;
    relayout();
    Q_EMIT resultChanged();
}

void ChartView::relayout()
{
    if (!m_message.isEmpty()) {
        m_scene = {};
        setImplicitHeight(0);
        update();
        return;
    }
    const qreal w = width() > 0 ? width() : 640;
    chart::LayoutOptions options;
    options.width = w;
    options.metrics = MermaidView::fontMetrics(m_fontFamily, m_fontPixelSize);
    options.locale = m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    m_scene = chart::layoutChart(m_spec, m_chart, options);
    m_laidOutWidth = w;
    setImplicitWidth(chart::limits::MinWidth);
    setImplicitHeight(
            std::ceil(m_scene.size.height() * std::min<qreal>(1, w / std::max<qreal>(1, m_scene.size.width()))));
    update();
}

void ChartView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.width() != oldGeometry.width() && !qFuzzyCompare(newGeometry.width(), m_laidOutWidth))
        relayout();
}

diagram::Palette ChartView::palette() const
{
    diagram::ThemeColors t;
    t.text = m_text.name(QColor::HexRgb);
    t.mutedText = m_muted.name(QColor::HexRgb);
    t.background = m_background.name(QColor::HexRgb);
    t.border = m_border.name(QColor::HexRgb);
    t.accent = m_accent.name(QColor::HexRgb);
    t.dark = m_dark;
    return diagram::themedPalette(t);
}

void ChartView::paint(QPainter *painter)
{
    const QSizeF s = m_scene.size;
    if (!m_message.isEmpty() || s.width() <= 0 || s.height() <= 0)
        return;
    // Laid out at the width (narrower than the chart's least: scaled down).
    const qreal k = std::min<qreal>(1, width() / s.width());
    painter->save();
    painter->scale(k, k);
    MermaidView::paintScene(painter, m_scene, palette(), m_fontFamily);
    painter->restore();
}

} // namespace brulion::editor
