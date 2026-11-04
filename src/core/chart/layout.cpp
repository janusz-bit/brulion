// A chart laid out as a diagram::Scene: bars, lines or a pie with axes, grid, labels and a legend.
// Every size is bounded: the width is clamped, labels are cut to the room they have (elided with
// "…"), labels that do not fit are thinned out, values are finite (data.cpp made the rest gaps).
#include "chart/chart.h"

#include "calc/numbers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::chart {

namespace {

using diagram::Dash;
using diagram::Item;
using diagram::Paint;
using diagram::PathOp;
using diagram::Role;

constexpr qreal Margin = 8;

Paint role(Role r)
{
    return Paint{r, {}};
}

/// Measures with `metrics` (diagram::approximateMetrics() when it has no function).
struct Measure {
    diagram::Metrics metrics;
    explicit Measure(const diagram::Metrics &m) : metrics(m.measure ? m : diagram::approximateMetrics(m.fontSize))
    {
        if (!(metrics.fontSize > 0) || metrics.fontSize > 200)
            metrics.fontSize = 13;
    }
    qreal width(QStringView text, qreal px, bool bold = false) const
    {
        return text.isEmpty() ? 0 : metrics.measure(text, px, bold).width();
    }
    qreal fs() const { return metrics.fontSize; }
    qreal lineHeight(qreal px) const { return std::ceil(px * 1.3); }

    /// `text` cut with "…" to `maxWidth` (by halving: a label of any length costs a few
    /// measurements).
    QString elide(const QString &text, qreal px, bool bold, qreal maxWidth) const
    {
        if (maxWidth <= 0)
            return {};
        if (width(text, px, bold) <= maxWidth)
            return text;
        qsizetype lo = 0, hi = text.size();
        while (lo < hi) {
            const qsizetype mid = (lo + hi + 1) / 2;
            if (width(QString(text.left(mid) + u'…'), px, bold) <= maxWidth)
                lo = mid;
            else
                hi = mid - 1;
        }
        if (lo > 0 && lo < text.size() && text.at(lo - 1).isHighSurrogate())
            --lo;
        return lo == 0 ? QString() : text.left(lo).trimmed() + u'…';
    }
};

Item textItem(const QRectF &box, const QString &text, qreal px, bool bold, Paint color,
              Qt::Alignment align = Qt::AlignHCenter)
{
    Item it;
    it.type = Item::Type::Text;
    it.box = box;
    it.lines = {text};
    it.align = align;
    it.fontSize = px;
    it.lineHeight = box.height();
    it.bold = bold;
    it.color = color;
    return it;
}

Item pathItem(QList<PathOp> path, Paint fill, Paint stroke, qreal width = 1, Dash dash = Dash::Solid)
{
    Item it;
    it.type = Item::Type::Path;
    it.path = std::move(path);
    it.fill = fill;
    it.stroke = stroke;
    it.strokeWidth = width;
    it.dash = dash;
    return it;
}

QList<PathOp> rectPath(const QRectF &r)
{
    return {{PathOp::Type::Move, r.topLeft(), {}, {}},
            {PathOp::Type::Line, r.topRight(), {}, {}},
            {PathOp::Type::Line, r.bottomRight(), {}, {}},
            {PathOp::Type::Line, r.bottomLeft(), {}, {}},
            {PathOp::Type::Close, {}, {}, {}}};
}

QList<PathOp> linePath(const QPointF &a, const QPointF &b)
{
    return {{PathOp::Type::Move, a, {}, {}}, {PathOp::Type::Line, b, {}, {}}};
}

/// An arc from angle `a0` to `a1` (radians, y down) as cubics (no Move).
void appendArc(QList<PathOp> &path, const QPointF &c, qreal r, qreal a0, qreal a1)
{
    const qreal span = a1 - a0;
    const int n = std::max(1, int(std::ceil(std::abs(span) / (std::numbers::pi / 2) - 1e-9)));
    const qreal step = span / n;
    const qreal k = 4.0 / 3.0 * std::tan(step / 4);
    for (int i = 0; i < n; ++i) {
        const qreal t0 = a0 + i * step, t1 = t0 + step;
        const QPointF p0(c.x() + r * std::cos(t0), c.y() + r * std::sin(t0));
        const QPointF p1(c.x() + r * std::cos(t1), c.y() + r * std::sin(t1));
        const QPointF c1 = p0 + QPointF(-r * std::sin(t0) * k, r * std::cos(t0) * k);
        const QPointF c2 = p1 - QPointF(-r * std::sin(t1) * k, r * std::cos(t1) * k);
        path.append({PathOp::Type::Cubic, p1, c1, c2});
    }
}

QList<PathOp> circlePath(const QPointF &c, qreal r)
{
    QList<PathOp> p{{PathOp::Type::Move, QPointF(c.x() + r, c.y()), {}, {}}};
    appendArc(p, c, r, 0, 2 * std::numbers::pi);
    p.append({PathOp::Type::Close, {}, {}, {}});
    return p;
}

/// "Nice" axis ticks over [lo, hi]: steps of 1, 2 or 5 × 10^k, about five of them.
struct Ticks {
    double lo = 0, hi = 1, step = 1;
};
Ticks niceTicks(double lo, double hi)
{
    if (!(hi > lo)) {
        const double pad = lo == 0 ? 1 : std::abs(lo) * 0.1;
        lo -= pad;
        hi += pad;
    }
    const double rough = (hi - lo) / 5;
    const double mag = std::pow(10.0, std::floor(std::log10(rough)));
    const double norm = rough / mag;
    const double step = (norm < 1.5 ? 1 : norm < 3 ? 2 : norm < 7 ? 5 : 10) * mag;
    Ticks t;
    t.step = step > 0 && std::isfinite(step) ? step : 1;
    t.lo = std::floor(lo / t.step) * t.step;
    t.hi = std::ceil(hi / t.step) * t.step;
    if (!(t.hi > t.lo))
        t.hi = t.lo + t.step;
    return t;
}

/// A legend row of series names with their colour, flowing in `width`. Returns its height.
qreal addLegend(QList<Item> &items, const Measure &m, const QStringList &names, qreal top, qreal width, bool bars)
{
    const qreal px = m.fs() * 0.9;
    const qreal lh = m.lineHeight(px);
    const qreal swatch = std::round(px * 0.8);
    qreal x = Margin, y = top;
    for (qsizetype i = 0; i < names.size(); ++i) {
        const QString name = m.elide(names.at(i).isEmpty() ? u"—"_s : names.at(i), px, false, width / 3);
        const qreal w = swatch + 6 + m.width(name, px) + 16;
        if (x > Margin && x + w > width - Margin) {
            x = Margin;
            y += lh;
        }
        const QRectF sw(x, y + (lh - swatch) / 2, swatch, swatch);
        if (bars)
            items.append(pathItem(rectPath(sw), role(diagram::sliceRole(int(i))), role(diagram::seriesRole(int(i)))));
        else
            items.append(pathItem(linePath(QPointF(sw.left(), sw.center().y()), QPointF(sw.right(), sw.center().y())),
                                  Paint{}, role(diagram::seriesRole(int(i))), 2.5));
        items.append(textItem(QRectF(x + swatch + 6, y, w - swatch - 6, lh), name, px, false, role(Role::LegendText),
                              Qt::AlignLeft));
        x += w;
    }
    return y + lh - top;
}

/// Bars and lines: a value axis on the left, the labels under the plot.
void layoutAxes(diagram::Scene &scene, const Spec &spec, const Data &data, const Measure &m, qreal width, qreal top,
                calc::CalcLocale locale)
{
    const qsizetype n = data.labels.size();
    const qsizetype seriesCount = data.series.size();
    const bool bars = spec.type == Type::Bar;
    QList<Item> &items = scene.items;
    if (seriesCount > 1) {
        QStringList names;
        for (const Series &s : data.series)
            names.append(s.name);
        top += addLegend(items, m, names, top, width, bars) + 6;
    }
    // The values' range (bars from zero).
    double lo = bars ? 0 : std::numeric_limits<double>::infinity();
    double hi = bars ? 0 : -std::numeric_limits<double>::infinity();
    for (const Series &s : data.series) {
        for (const double v : s.values) {
            if (std::isfinite(v)) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
    }
    if (!std::isfinite(lo) || !std::isfinite(hi))
        lo = hi = 0;
    const Ticks ticks = niceTicks(lo, hi);
    const int tickCount = std::clamp(int(std::llround((ticks.hi - ticks.lo) / ticks.step)), 1, 20);
    const qreal small = m.fs() * 0.85;
    const qreal lh = m.lineHeight(small);
    QStringList tickLabels;
    qreal labelWidth = 0;
    for (int i = 0; i <= tickCount; ++i) {
        const double v = ticks.lo + (ticks.hi - ticks.lo) * i / tickCount;
        tickLabels.append(calc::formatNumber(std::abs(v) < ticks.step * 1e-9 ? 0 : v, locale));
        labelWidth = std::max(labelWidth, m.width(tickLabels.last(), small));
    }
    labelWidth = std::min(labelWidth, width / 3);
    const qreal plotLeft = Margin + labelWidth + 6;
    const qreal plotRight = width - Margin;
    const qreal plotTop = top + lh / 2;
    const qreal plotHeight = std::clamp(width * 0.42, 140.0, 360.0);
    const qreal plotBottom = plotTop + plotHeight;
    const qreal plotWidth = std::max<qreal>(1, plotRight - plotLeft);
    const auto yOf = [&](double v) { return plotBottom - (v - ticks.lo) / (ticks.hi - ticks.lo) * plotHeight; };

    // Grid and the value labels.
    for (int i = 0; i <= tickCount; ++i) {
        const qreal y = std::round(plotBottom - plotHeight * i / tickCount) + 0.5;
        items.append(pathItem(linePath(QPointF(plotLeft, y), QPointF(plotRight, y)), Paint{}, role(Role::Grid), 1));
        items.append(textItem(QRectF(Margin, y - lh / 2, labelWidth, lh),
                              m.elide(tickLabels.at(i), small, false, labelWidth), small, false,
                              role(Role::ClusterText), Qt::AlignRight));
    }
    // The labels under the plot, thinned out to what fits.
    const qreal slot = plotWidth / std::max<qsizetype>(1, n);
    qreal widest = 0;
    for (const QString &l : data.labels)
        widest = std::max(widest, std::min(m.width(l, small), m.fs() * 12));
    const int every = std::max(1, int(std::ceil((widest + 8) / std::max<qreal>(slot, 0.5))));
    for (qsizetype i = 0; i < n; i += every) {
        const qreal room = slot * every - 6;
        const qreal cx = plotLeft + slot * (i + 0.5);
        items.append(textItem(QRectF(cx - room / 2, plotBottom + 4, room, lh),
                              m.elide(data.labels.at(i), small, false, room), small, false, role(Role::ClusterText)));
    }
    // The data.
    const qreal base = std::round(yOf(std::clamp(0.0, ticks.lo, ticks.hi))) + 0.5;
    if (bars) {
        const qreal group = slot * 0.76;
        const qreal bar = group / qreal(seriesCount);
        for (qsizetype s = 0; s < seriesCount; ++s) {
            const QList<double> &values = data.series.at(s).values;
            for (qsizetype i = 0; i < n && i < values.size(); ++i) {
                const double v = values.at(i);
                if (!std::isfinite(v))
                    continue;
                const qreal x = plotLeft + slot * i + (slot - group) / 2 + bar * s;
                const qreal y = yOf(v);
                QRectF r(x, std::min(y, base), bar, std::abs(base - y));
                if (bar > 3)
                    r.adjust(0.5, 0, -0.5, 0);
                items.append(pathItem(rectPath(r), role(diagram::sliceRole(int(s))),
                                      bar > 2 ? role(diagram::seriesRole(int(s))) : Paint{}, 1));
            }
        }
    } else {
        const bool markers = n <= 60;
        for (qsizetype s = 0; s < seriesCount; ++s) {
            const QList<double> &values = data.series.at(s).values;
            QList<PathOp> path;
            QList<QPointF> points;
            bool open = false;
            for (qsizetype i = 0; i < n && i < values.size(); ++i) {
                const double v = values.at(i);
                if (!std::isfinite(v)) {
                    open = false;
                    continue;
                }
                const QPointF p(plotLeft + slot * (i + 0.5), yOf(v));
                const bool alone = (i == 0 || !std::isfinite(values.at(i - 1)))
                                   && (i + 1 >= values.size() || !std::isfinite(values.at(i + 1)));
                path.append({open ? PathOp::Type::Line : PathOp::Type::Move, p, {}, {}});
                open = true;
                if (markers || alone)
                    points.append(p);
            }
            if (!path.isEmpty())
                items.append(pathItem(path, Paint{}, role(diagram::seriesRole(int(s))), 2.25));
            for (const QPointF &p : std::as_const(points))
                items.append(
                        pathItem(circlePath(p, 3.25), role(diagram::seriesRole(int(s))), role(Role::Background), 1.25));
        }
    }
    // The zero line (or the bottom) over the bars.
    items.append(pathItem(linePath(QPointF(plotLeft, base), QPointF(plotRight, base)), Paint{}, role(Role::Line), 1));
    scene.size = QSizeF(width, std::ceil(plotBottom + 4 + lh + Margin));
}

void layoutPie(diagram::Scene &scene, const Data &data, const Measure &m, qreal width, qreal top,
               calc::CalcLocale locale)
{
    constexpr qreal pi = std::numbers::pi;
    const QList<double> &values = data.series.front().values;
    QList<qsizetype> shown;
    double total = 0;
    for (qsizetype i = 0; i < values.size() && i < data.labels.size(); ++i) {
        if (values.at(i) > 0) {
            shown.append(i);
            total += values.at(i);
        }
    }
    if (shown.isEmpty() || !(total > 0))
        return;
    const qreal px = m.fs() * 0.9;
    const qreal lh = m.lineHeight(px);
    const bool beside = width >= 440;
    const qreal R = beside ? std::clamp(width * 0.2, 60.0, 150.0) : std::clamp(width / 2 - Margin * 2, 40.0, 140.0);
    const QPointF c(beside ? Margin + R + 4 : width / 2, top + R + 4);
    qreal a = -pi / 2;
    QList<Item> labels;
    for (qsizetype k = 0; k < shown.size(); ++k) {
        const double share = values.at(shown.at(k)) / total;
        const qreal span = share * 2 * pi;
        QList<PathOp> p;
        if (shown.size() == 1) {
            p = circlePath(c, R);
        } else {
            p.append({PathOp::Type::Move, c, {}, {}});
            p.append({PathOp::Type::Line, c + QPointF(R * std::cos(a), R * std::sin(a)), {}, {}});
            appendArc(p, c, R, a, a + span);
            p.append({PathOp::Type::Close, {}, {}, {}});
        }
        scene.items.append(pathItem(p, role(diagram::sliceRole(int(k))), role(Role::SliceStroke), 1.5));
        if (span >= 14 * pi / 180) {
            const qreal mid = a + span / 2;
            const QPointF at = shown.size() == 1 ? c : c + QPointF(R * 0.66 * std::cos(mid), R * 0.66 * std::sin(mid));
            const QString text =
                    calc::formatNumber(std::round(share * (share >= 0.0995 ? 100 : 1000)) / (share >= 0.0995 ? 1 : 10),
                                       locale)
                    + u'%';
            const qreal w = m.width(text, px, true) + 4;
            labels.append(
                    textItem(QRectF(at.x() - w / 2, at.y() - lh / 2, w, lh), text, px, true, role(Role::SliceText)));
        }
        a += span;
    }
    scene.items.append(labels);
    // The legend: beside the pie, or under it.
    const qreal swatch = std::round(px * 0.8);
    const qreal lx = beside ? c.x() + R + 24 : Margin;
    const qreal room = width - Margin - lx - swatch - 6;
    const qreal legendHeight = shown.size() * (lh + 2);
    qreal y = beside ? std::max(top, c.y() - legendHeight / 2) : c.y() + R + 12;
    for (qsizetype k = 0; k < shown.size(); ++k) {
        const qsizetype i = shown.at(k);
        const double v = values.at(i);
        const QString suffix = u" — "_s + calc::formatNumber(v, locale) + u" ("_s
                               + calc::formatNumber(std::round(v / total * 1000) / 10, locale) + u"%)"_s;
        const qreal suffixWidth = m.width(suffix, px);
        const QString text =
                m.elide(data.labels.at(i), px, false, std::max<qreal>(room - suffixWidth, room / 3)) + suffix;
        scene.items.append(pathItem(rectPath(QRectF(lx, y + (lh - swatch) / 2, swatch, swatch)),
                                    role(diagram::sliceRole(int(k))), Paint{}, 0));
        scene.items.append(textItem(QRectF(lx + swatch + 6, y, room, lh), m.elide(text, px, false, room), px, false,
                                    role(Role::LegendText), Qt::AlignLeft));
        y += lh + 2;
    }
    const qreal bottom = std::max(c.y() + R + 4, y);
    scene.size = QSizeF(width, std::ceil(bottom + Margin));
}

} // namespace

diagram::Scene layoutChart(const Spec &spec, const Data &data, const LayoutOptions &options)
{
    diagram::Scene scene;
    const Measure m(options.metrics);
    const qreal width =
            std::clamp(std::isfinite(options.width) ? options.width : 640, limits::MinWidth, limits::MaxWidth);
    scene.title = spec.title.isEmpty() ? typeName(spec.type) : spec.title;
    scene.size = QSizeF(width, Margin * 2);
    qreal top = Margin;
    if (!spec.title.isEmpty()) {
        const qreal px = m.fs() * 1.1;
        const qreal lh = m.lineHeight(px);
        scene.items.append(textItem(QRectF(Margin, top, width - 2 * Margin, lh),
                                    m.elide(spec.title, px, true, width - 2 * Margin), px, true, role(Role::Title)));
        top += lh + 6;
    }
    if (data.series.isEmpty() || data.labels.isEmpty())
        return scene;
    if (spec.type == Type::Pie)
        layoutPie(scene, data, m, width, top, options.locale);
    else
        layoutAxes(scene, spec, data, m, width, top, options.locale);
    return scene;
}

} // namespace brulion::chart
