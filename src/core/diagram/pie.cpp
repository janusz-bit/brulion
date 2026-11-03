// Pie charts: `pie [showData] [title …]`, `title …`, `"Label" : 42.5` (docs/SYNTAX.md §11 "Mermaid diagrams").
#include "diagram/pie_p.h"

#include <QRegularExpression>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::diagram::detail {

namespace {

using Code = Error::Code;

/// A header or `title`/`showData` statement's rest after `word`.
bool takeWord(QStringView &s, QStringView word)
{
    if (!s.startsWith(word) || (s.size() > word.size() && !s[word.size()].isSpace() && s[word.size()] != u':'))
        return false;
    s = s.sliced(word.size()).trimmed();
    return true;
}

QString formatValue(double v)
{
    return QString::number(v, 'g', 12);
}

} // namespace

Outcome<PieChart> parsePie(QStringView source)
{
    PieChart chart;
    const QList<Statement> st = statements(source, false, &chart.title);
    if (st.isEmpty())
        return makeError(Code::Empty, 0);
    QStringView head = st.front().text;
    if (!takeWord(head, u"pie"))
        return makeError(Code::UnknownType, st.front().line);
    if (takeWord(head, u"showData"))
        chart.showData = true;
    if (takeWord(head, u"title"))
        chart.title = cleanLabel(head);
    else if (!head.isEmpty())
        return makeError(Code::Syntax, st.front().line, excerpt(head));
    static const QRegularExpression entry(u"^\"([^\"]*)\"\\s*:\\s*(\\S+)\\s*;?$"_s);
    bool any = false;
    for (qsizetype i = 1; i < st.size(); ++i) {
        QStringView s = st[i].text;
        if (takeWord(s, u"title")) {
            if (s.startsWith(u':'))
                s = s.sliced(1);
            chart.title = cleanLabel(s);
            continue;
        }
        if (s == u"showData") {
            chart.showData = true;
            continue;
        }
        if (s.startsWith(u"accTitle") || s.startsWith(u"accDescr"))
            continue;
        const auto m = entry.matchView(s);
        if (!m.hasMatch())
            return makeError(Code::Syntax, st[i].line, excerpt(s));
        bool ok = false;
        const double v = m.capturedView(2).toDouble(&ok);
        if (!ok || !std::isfinite(v))
            return makeError(Code::InvalidValue, st[i].line, excerpt(m.capturedView(2)));
        any = true;
        if (v <= 0)
            continue;
        if (chart.slices.size() >= limits::MaxSlices)
            return makeError(Code::TooLarge, st[i].line, u"slices"_s);
        chart.slices.append({cleanLabel(m.capturedView(1)), v, st[i].line});
    }
    if (chart.slices.isEmpty())
        return makeError(Code::NoData, any ? 0 : st.front().line);
    return chart;
}

QList<qreal> pieAngles(const PieChart &chart)
{
    double total = 0;
    for (const PieSlice &s : chart.slices)
        total += s.value;
    QList<qreal> out;
    for (const PieSlice &s : chart.slices)
        out.append(total > 0 ? s.value / total * 360.0 : 0);
    return out;
}

Scene drawPie(const PieChart &chart, const Metrics &m)
{
    constexpr qreal pi = std::numbers::pi;
    Scene scene;
    scene.title = chart.title;
    const qreal fs = m.fontSize;
    const qreal margin = 8;
    const qreal R = fs * 9;
    qreal top = margin;
    TextBlock title;
    if (!chart.title.isEmpty()) {
        title = layoutText(m, chart.title, fs * 1.15, true, fs * 30);
        top += title.size.height() + 10;
    }
    const QPointF c(margin + R, top + R);
    const QList<qreal> angles = pieAngles(chart);
    double total = 0;
    for (const PieSlice &s : chart.slices)
        total += s.value;
    // Slices clockwise from the top.
    qreal a = -pi / 2;
    QList<Item> labels;
    for (qsizetype i = 0; i < chart.slices.size(); ++i) {
        const qreal span = angles[i] * pi / 180;
        QList<PathOp> p;
        if (chart.slices.size() == 1) {
            p = ellipsePath(c, R, R);
        } else {
            p.append({PathOp::Type::Move, c, {}, {}});
            p.append({PathOp::Type::Line, c + QPointF(R * std::cos(a), R * std::sin(a)), {}, {}});
            appendArc(p, c, R, R, a, a + span);
            p.append({PathOp::Type::Close, {}, {}, {}});
        }
        scene.items.append(pathItem(p, Paint{sliceRole(int(i)), {}}, Paint{Role::SliceStroke, {}}, 1.5));
        // The share on the slice when it has room for it.
        if (angles[i] >= 14) {
            const qreal mid = a + span / 2;
            const qreal share = chart.slices[i].value / total * 100;
            const QString text =
                    share >= 9.95 ? QString::number(std::lround(share)) + u'%' : QString::number(share, 'f', 1) + u'%';
            const TextBlock t = layoutText(m, text, fs * 0.9, true, 0);
            const QPointF at =
                    chart.slices.size() == 1 ? c : c + QPointF(R * 0.68 * std::cos(mid), R * 0.68 * std::sin(mid));
            labels.append(textItem(t, QRectF(at - QPointF(t.size.width() / 2, t.size.height() / 2), t.size),
                                   Paint{Role::SliceText, {}}));
        }
        a += span;
    }
    scene.items.append(labels);
    // The legend right of the pie.
    const qreal lx = margin + 2 * R + fs * 2;
    const qreal swatch = fs * 0.95;
    QList<TextBlock> rows;
    qreal rowH = 0, legendW = 0;
    for (const PieSlice &s : chart.slices) {
        const double share = s.value / total * 100;
        QString text = s.label;
        text += chart.showData ? u" ["_s + formatValue(s.value) + u"]"_s : QString();
        text += u" ("_s + QString::number(share, 'f', share >= 9.95 ? 0 : 1) + u"%)"_s;
        rows.append(layoutText(m, text, fs, false, fs * 22));
        rowH = std::max(rowH, rows.last().lineHeight);
        legendW = std::max(legendW, rows.last().size.width());
    }
    qreal legendH = 0;
    for (const TextBlock &t : rows)
        legendH += t.size.height() + fs * 0.45;
    qreal y = c.y() - legendH / 2;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const TextBlock &t = rows[i];
        const qreal sy = y + (t.lineHeight - swatch) / 2;
        scene.items.append(
                pathItem(roundedRectPath(QRectF(lx, sy, swatch, swatch), 2), Paint{sliceRole(int(i)), {}}, Paint{}, 0));
        scene.items.append(textItem(t, QRectF(lx + swatch + fs * 0.5, y, t.size.width(), t.size.height()),
                                    Paint{Role::LegendText, {}}, Qt::AlignLeft));
        y += t.size.height() + fs * 0.45;
    }
    const qreal width = std::max(lx + swatch + fs * 0.5 + legendW + margin, title.size.width() + 2 * margin);
    const qreal height = std::max(top + 2 * R, c.y() + legendH / 2) + margin;
    if (!chart.title.isEmpty())
        scene.items.prepend(textItem(title, QRectF(0, margin, width, title.size.height()), Paint{Role::Title, {}}));
    scene.size = QSizeF(width, height);
    return scene;
}

} // namespace brulion::diagram::detail
