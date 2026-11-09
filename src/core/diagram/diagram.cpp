#include "diagram/diagram.h"

#include "diagram/common_p.h"
#include "diagram/flowchart_p.h"
#include "diagram/pie_p.h"
#include "diagram/sequence_p.h"

#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::diagram {

namespace detail {

namespace {

/// Is `line` (trimmed) a `%%` comment or a `%%{…}%%` directive?
bool isComment(QStringView line)
{
    return line.startsWith(u"%%");
}

/// Splits `line` at `;` outside quotes and brackets.
QList<QStringView> splitAtSemicolons(QStringView line)
{
    QList<QStringView> out;
    int depth = 0;
    bool quoted = false;
    qsizetype from = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == u'"') {
            quoted = !quoted;
        } else if (quoted) {
            continue;
        } else if (c == u'[' || c == u'(' || c == u'{') {
            ++depth;
        } else if ((c == u']' || c == u')' || c == u'}') && depth > 0) {
            --depth;
        } else if (c == u';' && depth == 0) {
            // `#quot;`-style entities end in ';' too.
            qsizetype j = i - 1;
            while (j >= from && line[j].isLetterOrNumber())
                --j;
            if (j >= from && line[j] == u'#' && j < i - 1)
                continue;
            out.append(line.sliced(from, i - from));
            from = i + 1;
        }
    }
    out.append(line.sliced(from));
    return out;
}

} // namespace

QList<Statement> statements(QStringView source, bool splitSemicolons, QString *title)
{
    QList<Statement> out;
    const QList<QStringView> lines = source.split(u'\n');
    qsizetype i = 0;
    // Front matter: `---` on the first non-empty line, up to the next `---`.
    while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
    if (i < lines.size() && lines[i].trimmed() == u"---") {
        qsizetype j = i + 1;
        while (j < lines.size() && lines[j].trimmed() != u"---")
            ++j;
        if (j < lines.size()) {
            for (qsizetype k = i + 1; k < j; ++k) {
                const QStringView l = lines[k].trimmed();
                if (title && l.startsWith(u"title:") && lines[k].front() != u' ')
                    *title = cleanLabel(l.sliced(6).trimmed());
            }
            i = j + 1;
        }
    }
    bool inDirective = false;
    for (; i < lines.size(); ++i) {
        QStringView l = lines[i].trimmed();
        // A directive over several lines: `%%{init: {` … `}}%%`.
        if (inDirective) {
            if (l.endsWith(u"}%%"))
                inDirective = false;
            continue;
        }
        if (l.startsWith(u"%%{") && !l.endsWith(u"}%%")) {
            inDirective = true;
            continue;
        }
        if (l.isEmpty() || isComment(l))
            continue;
        if (!splitSemicolons) {
            out.append({l.toString(), int(i + 1)});
            continue;
        }
        for (const QList<QStringView> parts = splitAtSemicolons(l); QStringView part : parts) {
            part = part.trimmed();
            if (!part.isEmpty())
                out.append({part.toString(), int(i + 1)});
        }
    }
    return out;
}

namespace {

/// `#quot;`, `#35;`, `#x23;` and a few named entities (Mermaid's entity codes), and the HTML ones.
QString decodeEntities(QStringView s)
{
    static const QHash<QString, QString> named = {
            {u"quot"_s, u"\""_s}, {u"amp"_s, u"&"_s},    {u"lt"_s, u"<"_s},     {u"gt"_s, u">"_s},
            {u"apos"_s, u"'"_s},  {u"nbsp"_s, u" "_s},   {u"hash"_s, u"#"_s},   {u"num"_s, u"#"_s},
            {u"semi"_s, u";"_s},  {u"colon"_s, u":"_s},  {u"lpar"_s, u"("_s},   {u"rpar"_s, u")"_s},
            {u"lsqb"_s, u"["_s},  {u"rsqb"_s, u"]"_s},   {u"lbrace"_s, u"{"_s}, {u"rbrace"_s, u"}"_s},
            {u"vert"_s, u"|"_s},  {u"hellip"_s, u"…"_s}, {u"ndash"_s, u"–"_s},  {u"mdash"_s, u"—"_s},
            {u"copy"_s, u"©"_s},  {u"deg"_s, u"°"_s},
    };
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if ((c == u'#' || c == u'&') && i + 2 < s.size()) {
            qsizetype j = i + 1;
            if (c == u'&' && s[j] == u'#')
                ++j;
            const bool numeric = s[j].isDigit()
                                 || ((s[j] == u'x' || s[j] == u'X') && j + 1 < s.size()
                                     && isxdigit(s[j + 1].unicode() < 128 ? int(s[j + 1].unicode()) : 0));
            const bool decimal = s[j].isDigit();
            if (numeric && !decimal)
                ++j;
            const qsizetype start = j;
            while (j < s.size() && j - start < 10 && s[j].isLetterOrNumber())
                ++j;
            if (j < s.size() && s[j] == u';' && j > start) {
                const QStringView name = s.sliced(start, j - start);
                if (numeric) {
                    bool ok = false;
                    const uint code = name.toUInt(&ok, decimal ? 10 : 16);
                    // Characters XML forbids (controls, surrogates) stay out.
                    if (ok && code >= 0x20 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff) && code != 0xfffe
                        && code != 0xffff) {
                        out += QChar::fromUcs4(code);
                        i = j;
                        continue;
                    }
                } else if (const auto it = named.constFind(name.toString().toLower()); it != named.cend()) {
                    out += *it;
                    i = j;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

/// The first `n` characters of `text` and "…" (a surrogate pair is never split).
QString cutTo(QStringView text, qsizetype n)
{
    if (text[n - 1].isHighSurrogate())
        --n;
    return text.first(n) + u'…';
}

} // namespace

QString cleanLabel(QStringView raw)
{
    QStringView s = raw.trimmed();
    bool markdownString = false;
    if (s.size() >= 2 && s.front() == u'"' && s.back() == u'"') {
        s = s.sliced(1, s.size() - 2);
        if (s.size() >= 2 && s.front() == u'`' && s.back() == u'`') {
            s = s.sliced(1, s.size() - 2);
            markdownString = true;
        }
    }
    QString t = s.toString();
    // <br>, <br/>, <br /> — line breaks; \n in a markdown string too.
    static const QRegularExpression br(u"<br\\s*/?>"_s, QRegularExpression::CaseInsensitiveOption);
    t.replace(br, u"\n"_s);
    if (markdownString) {
        t.replace(u"\\n"_s, u"\n"_s);
        static const QRegularExpression marks(u"\\*\\*|__|(?<![\\w*])\\*(?!\\s)|(?<!\\s)\\*(?![\\w*])"_s);
        t.remove(marks);
    }
    // Simple formatting tags are shown as their text (Brulion draws no HTML).
    static const QRegularExpression tags(u"</?(?:b|i|u|s|em|strong|small|big|sub|sup|code|span|p|div)\\b[^<>]*>"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    t.remove(tags);
    t = decodeEntities(t);
    // Lines trimmed, tabs as spaces.
    t.replace(u'\t', u' ');
    t.remove(u'\r');
    QStringList lines = t.split(u'\n');
    for (QString &l : lines)
        l = l.trimmed();
    return capLength(lines.join(u'\n'));
}

QString capLength(const QString &text)
{
    return text.size() <= limits::MaxLabelLength ? text : cutTo(text, limits::MaxLabelLength - 1);
}

qreal lineHeightOf(const Metrics &m, qreal pixelSize)
{
    const QSizeF s = m.measure(u"Ag", pixelSize, false);
    return std::max(s.height(), pixelSize);
}

TextBlock layoutText(const Metrics &m, const QString &text, qreal pixelSize, bool bold, qreal maxWidth)
{
    TextBlock out;
    out.pixelSize = pixelSize;
    out.bold = bold;
    out.lineHeight = lineHeightOf(m, pixelSize);
    const qreal space = m.measure(u" ", pixelSize, bold).width();
    qreal width = 0;
    const auto push = [&](const QString &line, qreal w) {
        out.lines.append(line);
        width = std::max(width, w);
    };
    bool cut = false;
    for (const QStringList paras = text.split(u'\n'); const QString &para : paras) {
        if (out.lines.size() >= limits::MaxLabelLines) {
            cut = true;
            break;
        }
        if (maxWidth <= 0) {
            push(para, m.measure(para, pixelSize, bold).width());
            continue;
        }
        QString line;
        qreal lineW = 0;
        const QList<QStringView> words = QStringView(para).split(u' ', Qt::SkipEmptyParts);
        if (words.isEmpty())
            push(QString(), 0);
        for (QStringView word : words) {
            if (out.lines.size() >= limits::MaxLabelLines) {
                cut = true;
                break;
            }
            const qreal w = m.measure(word, pixelSize, bold).width();
            if (!line.isEmpty() && lineW + space + w <= maxWidth) {
                line += u' ';
                line += word;
                lineW += space + w;
                continue;
            }
            if (!line.isEmpty()) {
                push(line, lineW);
                line.clear();
                lineW = 0;
            }
            if (w <= maxWidth) {
                line = word.toString();
                lineW = w;
                continue;
            }
            // A word wider than a line: broken between characters (the last piece stays in `line`).
            for (qsizetype i = 0; i < word.size(); ++i) {
                const qsizetype n = word[i].isHighSurrogate() && i + 1 < word.size() ? 2 : 1;
                const QStringView ch = word.sliced(i, n);
                const qreal cw = m.measure(ch, pixelSize, bold).width();
                if (!line.isEmpty() && lineW + cw > maxWidth) {
                    if (out.lines.size() >= limits::MaxLabelLines) {
                        cut = true;
                        break;
                    }
                    push(line, lineW);
                    line.clear();
                    lineW = 0;
                }
                line += ch;
                lineW += cw;
                i += n - 1;
            }
        }
        if (!line.isEmpty() && out.lines.size() < limits::MaxLabelLines)
            push(line, lineW);
        else if (!line.isEmpty())
            cut = true;
    }
    if (cut && !out.lines.isEmpty()) {
        QString &last = out.lines.last();
        last += u'…';
        width = std::max(width, m.measure(last, pixelSize, bold).width());
    }
    if (out.lines.isEmpty())
        out.lines.append(QString());
    out.size = QSizeF(width, out.lineHeight * out.lines.size());
    return out;
}

Item textItem(const TextBlock &t, const QRectF &box, Paint color, Qt::Alignment align)
{
    Item it;
    it.type = Item::Type::Text;
    it.box = box;
    it.lines = t.lines;
    it.align = align;
    it.fontSize = t.pixelSize;
    it.lineHeight = t.lineHeight;
    it.bold = t.bold;
    it.color = std::move(color);
    return it;
}

QList<PathOp> rectPath(const QRectF &r)
{
    return polygonPath({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
}

QList<PathOp> polygonPath(const QList<QPointF> &points)
{
    QList<PathOp> p;
    p.reserve(points.size() + 1);
    for (qsizetype i = 0; i < points.size(); ++i)
        p.append({i == 0 ? PathOp::Type::Move : PathOp::Type::Line, points[i], {}, {}});
    if (!points.isEmpty())
        p.append({PathOp::Type::Close, {}, {}, {}});
    return p;
}

void appendArc(QList<PathOp> &path, const QPointF &c, qreal rx, qreal ry, qreal a0, qreal a1)
{
    const qreal span = a1 - a0;
    const int n = std::max(1, int(std::ceil(std::abs(span) / (std::numbers::pi / 2) - 1e-9)));
    const qreal step = span / n;
    // The control distance of a cubic for an arc of `step`.
    const qreal k = 4.0 / 3.0 * std::tan(step / 4);
    for (int i = 0; i < n; ++i) {
        const qreal t0 = a0 + i * step, t1 = t0 + step;
        const QPointF p0(c.x() + rx * std::cos(t0), c.y() + ry * std::sin(t0));
        const QPointF p1(c.x() + rx * std::cos(t1), c.y() + ry * std::sin(t1));
        const QPointF c1 = p0 + QPointF(-rx * std::sin(t0) * k, ry * std::cos(t0) * k);
        const QPointF c2 = p1 - QPointF(-rx * std::sin(t1) * k, ry * std::cos(t1) * k);
        path.append({PathOp::Type::Cubic, p1, c1, c2});
    }
}

QList<PathOp> ellipsePath(const QPointF &c, qreal rx, qreal ry)
{
    QList<PathOp> p;
    p.append({PathOp::Type::Move, QPointF(c.x() + rx, c.y()), {}, {}});
    appendArc(p, c, rx, ry, 0, 2 * std::numbers::pi);
    p.append({PathOp::Type::Close, {}, {}, {}});
    return p;
}

QList<PathOp> roundedRectPath(const QRectF &r, qreal radius)
{
    const qreal rr = std::min({radius, r.width() / 2, r.height() / 2});
    if (rr <= 0.01)
        return rectPath(r);
    constexpr qreal pi = std::numbers::pi;
    QList<PathOp> p;
    p.append({PathOp::Type::Move, QPointF(r.left() + rr, r.top()), {}, {}});
    p.append({PathOp::Type::Line, QPointF(r.right() - rr, r.top()), {}, {}});
    appendArc(p, QPointF(r.right() - rr, r.top() + rr), rr, rr, -pi / 2, 0);
    p.append({PathOp::Type::Line, QPointF(r.right(), r.bottom() - rr), {}, {}});
    appendArc(p, QPointF(r.right() - rr, r.bottom() - rr), rr, rr, 0, pi / 2);
    p.append({PathOp::Type::Line, QPointF(r.left() + rr, r.bottom()), {}, {}});
    appendArc(p, QPointF(r.left() + rr, r.bottom() - rr), rr, rr, pi / 2, pi);
    p.append({PathOp::Type::Line, QPointF(r.left(), r.top() + rr), {}, {}});
    appendArc(p, QPointF(r.left() + rr, r.top() + rr), rr, rr, pi, 3 * pi / 2);
    p.append({PathOp::Type::Close, {}, {}, {}});
    return p;
}

Item pathItem(QList<PathOp> path, Paint fill, Paint stroke, qreal width, Dash dash)
{
    Item it;
    it.type = Item::Type::Path;
    it.path = std::move(path);
    it.fill = std::move(fill);
    it.stroke = std::move(stroke);
    it.strokeWidth = width;
    it.dash = dash;
    return it;
}

namespace {

qreal headSize(qreal lineWidth)
{
    return 7 + 1.6 * std::max<qreal>(0, lineWidth - 1);
}

} // namespace

qreal headInset(Head h, qreal lineWidth)
{
    switch (h) {
    case Head::Arrow: return headSize(lineWidth) * 1.3 - 0.5; // the line runs under the head's base
    case Head::Circle: return headSize(lineWidth) / 2;
    case Head::Open:
    case Head::Cross:
    case Head::None: break;
    }
    return 0;
}

QPointF unitVector(QPointF a, QPointF b, QPointF fallback)
{
    const QPointF d = b - a;
    const qreal len = std::hypot(d.x(), d.y());
    return len > 1e-9 ? d / len : fallback;
}

void addHead(QList<Item> &items, Head h, const QPointF &tip, const QPointF &dir, Paint color, qreal lineWidth)
{
    const QPointF d = unitVector(QPointF(), dir);
    const QPointF n(-d.y(), d.x());
    const qreal s = headSize(lineWidth);
    const QPointF base = tip - d * s * 1.3, wing = n * s * 0.5; // an arrow's base and half its width
    switch (h) {
    case Head::Arrow:
        items.append(pathItem(polygonPath({tip, base + wing, base - wing}), std::move(color), Paint{}, 0));
        return;
    case Head::Open:
        items.append(pathItem({{PathOp::Type::Move, base + wing, {}, {}},
                               {PathOp::Type::Line, tip, {}, {}},
                               {PathOp::Type::Line, base - wing, {}, {}}},
                              Paint{}, std::move(color), std::max<qreal>(1, lineWidth)));
        return;
    case Head::Circle: {
        const qreal r = s / 2;
        items.append(pathItem(ellipsePath(tip - d * r, r, r), std::move(color), Paint{}, 0));
        return;
    }
    case Head::Cross: {
        const qreal r = s * 0.55;
        const QPointF c = tip - d * r, a = (d + n) * r * 0.7071, b = (d - n) * r * 0.7071;
        items.append(pathItem({{PathOp::Type::Move, c - a, {}, {}},
                               {PathOp::Type::Line, c + a, {}, {}},
                               {PathOp::Type::Move, c - b, {}, {}},
                               {PathOp::Type::Line, c + b, {}, {}}},
                              Paint{}, std::move(color), std::max<qreal>(1.5, lineWidth)));
        return;
    }
    case Head::None: break;
    }
}

void Style::merge(const Style &other)
{
    if (!other.fill.isEmpty())
        fill = other.fill;
    if (!other.stroke.isEmpty())
        stroke = other.stroke;
    if (!other.color.isEmpty())
        color = other.color;
    if (other.strokeWidth >= 0)
        strokeWidth = other.strokeWidth;
    if (other.dash)
        dash = other.dash;
    bold = bold || other.bold;
}

Style parseStyle(QStringView text)
{
    Style s;
    // Commas separate properties, but not inside rgb(…).
    QList<QStringView> parts;
    int depth = 0;
    qsizetype from = 0;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        const QChar c = i < text.size() ? text[i] : u',';
        if (c == u'(')
            ++depth;
        else if (c == u')' && depth > 0)
            --depth;
        else if ((c == u',' || c == u';') && depth == 0) {
            parts.append(text.sliced(from, i - from));
            from = i + 1;
        }
    }
    for (QStringView part : parts) {
        const qsizetype colon = part.indexOf(u':');
        if (colon < 0)
            continue;
        const QString key = part.first(colon).trimmed().toString().toLower();
        QStringView value = part.sliced(colon + 1).trimmed();
        if (value.endsWith(u"!important"))
            value = value.chopped(10).trimmed();
        if (key == u"fill" || key == u"background" || key == u"background-color") {
            s.fill = parseCssColor(value);
        } else if (key == u"stroke" || key == u"border-color") {
            s.stroke = parseCssColor(value);
        } else if (key == u"color") {
            s.color = parseCssColor(value);
        } else if (key == u"stroke-width") {
            QStringView v = value;
            if (v.endsWith(u"px"))
                v.chop(2);
            bool ok = false;
            const double w = v.trimmed().toDouble(&ok);
            if (ok && std::isfinite(w) && w >= 0)
                s.strokeWidth = std::min(w, 12.0);
        } else if (key == u"stroke-dasharray") {
            if (value == u"0" || value == u"none")
                s.dash = Dash::Solid;
            else
                s.dash = value.split(u' ', Qt::SkipEmptyParts).value(0).toDouble() <= 3 ? Dash::Dotted : Dash::Dashed;
        } else if (key == u"font-weight") {
            s.bold = value == u"bold" || value.toInt() >= 600;
        }
    }
    return s;
}

std::optional<Rgba> parseHex(QStringView hex)
{
    // "#rgb" and "#rgba" (one digit a channel, doubled), "#rrggbb" and "#rrggbbaa".
    const qsizetype n = hex.size() - 1;
    if (!hex.startsWith(u'#') || (n != 3 && n != 4 && n != 6 && n != 8))
        return std::nullopt;
    std::array<int, 8> d = {};
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t u = hex[i + 1].unicode();
        if (u >= u'0' && u <= u'9')
            d[i] = u - u'0';
        else if (u >= u'a' && u <= u'f')
            d[i] = u - u'a' + 10;
        else if (u >= u'A' && u <= u'F')
            d[i] = u - u'A' + 10;
        else
            return std::nullopt;
    }
    const auto channel = [&](int k) { return (n <= 4 ? d[k] * 17 : d[2 * k] * 16 + d[2 * k + 1]) / 255.0; };
    return Rgba{channel(0), channel(1), channel(2), n == 4 || n == 8 ? channel(3) : 1};
}

QString toHex(const Rgba &c, bool withAlpha)
{
    const auto byte = [](double v) { return std::clamp(int(std::lround(v * 255)), 0, 255); };
    QString out = u"#"_s;
    const auto put = [&](double v) { out += QString::number(byte(v), 16).rightJustified(2, u'0'); };
    put(c.r);
    put(c.g);
    put(c.b);
    if (withAlpha)
        put(c.a);
    return out;
}

QString mix(const QString &a, const QString &b, double t)
{
    const Rgba x = parseHex(a).value_or(Rgba{}), y = parseHex(b).value_or(Rgba{});
    return toHex({x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, 1});
}

double luminance(const QString &hex)
{
    const Rgba c = parseHex(hex).value_or(Rgba{});
    const auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

QString textOn(const QString &fill, const QString &dark, const QString &light)
{
    const double l = luminance(fill);
    const auto contrast = [](double a, double b) { return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05); };
    return contrast(l, luminance(dark)) >= contrast(l, luminance(light)) ? dark : light;
}

QString excerpt(QStringView text)
{
    constexpr qsizetype Max = 60;
    return text.size() <= Max ? text.toString() : cutTo(text, Max - 1);
}

} // namespace detail

QString parseCssColor(QStringView text)
{
    const QString t = text.trimmed().toString().toLower();
    if (t.isEmpty())
        return {};
    if (t.startsWith(u'#')) {
        const auto c = detail::parseHex(t);
        return c ? detail::toHex(*c, c->a < 1) : QString();
    }
    if (t == u"transparent" || t == u"none")
        return u"#00000000"_s;
    static const QRegularExpression rgb(
            u"^rgba?\\(\\s*(\\d{1,3})\\s*[, ]\\s*(\\d{1,3})\\s*[, ]\\s*(\\d{1,3})\\s*(?:[,/]\\s*([0-9.]+)(%?)\\s*)?\\)$"_s);
    if (const auto m = rgb.match(t); m.hasMatch()) {
        const auto channel = [&m](int n) { return std::min(255, m.capturedView(n).toInt()) / 255.0; };
        detail::Rgba c{channel(1), channel(2), channel(3), 1};
        if (const QStringView alpha = m.capturedView(4); !alpha.isEmpty()) {
            bool ok = false;
            double a = alpha.toDouble(&ok);
            if (!ok || !std::isfinite(a))
                return {};
            if (!m.capturedView(5).isEmpty())
                a /= 100;
            c.a = std::clamp(a, 0.0, 1.0);
        }
        return detail::toHex(c, c.a < 1);
    }
    static const QHash<QString, QString> names = {
            {u"black"_s, u"#000000"_s},      {u"white"_s, u"#ffffff"_s},       {u"red"_s, u"#ff0000"_s},
            {u"green"_s, u"#008000"_s},      {u"blue"_s, u"#0000ff"_s},        {u"yellow"_s, u"#ffff00"_s},
            {u"orange"_s, u"#ffa500"_s},     {u"purple"_s, u"#800080"_s},      {u"pink"_s, u"#ffc0cb"_s},
            {u"gray"_s, u"#808080"_s},       {u"grey"_s, u"#808080"_s},        {u"silver"_s, u"#c0c0c0"_s},
            {u"maroon"_s, u"#800000"_s},     {u"olive"_s, u"#808000"_s},       {u"lime"_s, u"#00ff00"_s},
            {u"aqua"_s, u"#00ffff"_s},       {u"cyan"_s, u"#00ffff"_s},        {u"teal"_s, u"#008080"_s},
            {u"navy"_s, u"#000080"_s},       {u"fuchsia"_s, u"#ff00ff"_s},     {u"magenta"_s, u"#ff00ff"_s},
            {u"brown"_s, u"#a52a2a"_s},      {u"gold"_s, u"#ffd700"_s},        {u"beige"_s, u"#f5f5dc"_s},
            {u"coral"_s, u"#ff7f50"_s},      {u"crimson"_s, u"#dc143c"_s},     {u"salmon"_s, u"#fa8072"_s},
            {u"tomato"_s, u"#ff6347"_s},     {u"violet"_s, u"#ee82ee"_s},      {u"indigo"_s, u"#4b0082"_s},
            {u"khaki"_s, u"#f0e68c"_s},      {u"lavender"_s, u"#e6e6fa"_s},    {u"plum"_s, u"#dda0dd"_s},
            {u"orchid"_s, u"#da70d6"_s},     {u"tan"_s, u"#d2b48c"_s},         {u"chocolate"_s, u"#d2691e"_s},
            {u"skyblue"_s, u"#87ceeb"_s},    {u"lightblue"_s, u"#add8e6"_s},   {u"lightgreen"_s, u"#90ee90"_s},
            {u"lightgray"_s, u"#d3d3d3"_s},  {u"lightgrey"_s, u"#d3d3d3"_s},   {u"darkgray"_s, u"#a9a9a9"_s},
            {u"darkgrey"_s, u"#a9a9a9"_s},   {u"darkblue"_s, u"#00008b"_s},    {u"darkgreen"_s, u"#006400"_s},
            {u"darkred"_s, u"#8b0000"_s},    {u"darkorange"_s, u"#ff8c00"_s},  {u"lightyellow"_s, u"#ffffe0"_s},
            {u"lightpink"_s, u"#ffb6c1"_s},  {u"steelblue"_s, u"#4682b4"_s},   {u"royalblue"_s, u"#4169e1"_s},
            {u"seagreen"_s, u"#2e8b57"_s},   {u"forestgreen"_s, u"#228b22"_s}, {u"slategray"_s, u"#708090"_s},
            {u"slategrey"_s, u"#708090"_s},  {u"whitesmoke"_s, u"#f5f5f5"_s},  {u"ivory"_s, u"#fffff0"_s},
            {u"wheat"_s, u"#f5deb3"_s},      {u"turquoise"_s, u"#40e0d0"_s},   {u"mintcream"_s, u"#f5fffa"_s},
            {u"aliceblue"_s, u"#f0f8ff"_s},  {u"honeydew"_s, u"#f0fff0"_s},    {u"mistyrose"_s, u"#ffe4e1"_s},
            {u"linen"_s, u"#faf0e6"_s},      {u"peachpuff"_s, u"#ffdab9"_s},   {u"gainsboro"_s, u"#dcdcdc"_s},
            {u"dimgray"_s, u"#696969"_s},    {u"dimgrey"_s, u"#696969"_s},     {u"firebrick"_s, u"#b22222"_s},
            {u"goldenrod"_s, u"#daa520"_s},  {u"hotpink"_s, u"#ff69b4"_s},     {u"deepskyblue"_s, u"#00bfff"_s},
            {u"dodgerblue"_s, u"#1e90ff"_s}, {u"limegreen"_s, u"#32cd32"_s},   {u"yellowgreen"_s, u"#9acd32"_s},
    };
    return names.value(t);
}

Metrics approximateMetrics(qreal fontSize)
{
    Metrics m;
    m.fontSize = fontSize;
    m.measure = [](QStringView line, qreal px, bool bold) {
        qreal w = 0;
        for (qsizetype i = 0; i < line.size(); ++i) {
            char32_t c = line[i].unicode();
            if (line[i].isHighSurrogate() && i + 1 < line.size()) {
                c = QChar::surrogateToUcs4(line[i], line[i + 1]);
                ++i;
            }
            if (QChar::category(c) == QChar::Mark_NonSpacing)
                continue;
            // East Asian wide characters and emoji take a whole em.
            const bool wide = (c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf)
                              || (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff)
                              || (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xff60)
                              || (c >= 0x1f300 && c <= 0x1faff) || (c >= 0x20000 && c <= 0x3fffd);
            const bool narrow = c == u'i' || c == u'l' || c == u'j' || c == u'.' || c == u',' || c == u'\'' || c == u'|'
                                || c == u'!' || c == u':' || c == u';' || c == u'ł' || c == u'í';
            const bool broad = c == u'm' || c == u'w' || c == u'M' || c == u'W' || c == u'@';
            w += wide ? 1.0 : narrow ? 0.3 : broad ? 0.85 : (c >= u'A' && c <= u'Z') ? 0.66 : c == u' ' ? 0.3 : 0.56;
        }
        return QSizeF(w * px * (bold ? 1.06 : 1.0), px * 1.25);
    };
    return m;
}

Kind detectKind(QStringView source, QString *typeName)
{
    const QList<detail::Statement> st = detail::statements(source, false);
    if (typeName)
        typeName->clear();
    if (st.isEmpty())
        return Kind::Unknown;
    const QString &first = st.front().text;
    qsizetype end = 0;
    while (end < first.size() && (first[end].isLetterOrNumber() || first[end] == u'-' || first[end] == u'_'))
        ++end;
    const QString word = first.left(end);
    if (typeName)
        *typeName = word;
    if (word == u"graph" || word == u"flowchart" || word == u"flowchart-elk")
        return Kind::Flowchart;
    if (word == u"sequenceDiagram")
        return Kind::Sequence;
    if (word == u"pie")
        return Kind::Pie;
    static const QStringList others = {
            u"classDiagram"_s,       u"classDiagram-v2"_s, u"stateDiagram"_s, u"stateDiagram-v2"_s,
            u"erDiagram"_s,          u"journey"_s,         u"gantt"_s,        u"quadrantChart"_s,
            u"requirementDiagram"_s, u"gitGraph"_s,        u"C4Context"_s,    u"C4Container"_s,
            u"C4Component"_s,        u"C4Dynamic"_s,       u"C4Deployment"_s, u"mindmap"_s,
            u"timeline"_s,           u"zenuml"_s,          u"sankey-beta"_s,  u"sankey"_s,
            u"xychart-beta"_s,       u"xychart"_s,         u"block-beta"_s,   u"block"_s,
            u"packet-beta"_s,        u"packet"_s,          u"kanban"_s,       u"architecture-beta"_s,
            u"architecture"_s,       u"radar-beta"_s,      u"treemap-beta"_s, u"treemap"_s,
    };
    if (others.contains(word))
        return Kind::Unsupported;
    if (typeName)
        typeName->clear();
    return Kind::Unknown;
}

Result render(QStringView source, const Metrics &metricsIn)
{
    Result r;
    const Metrics metrics = metricsIn.measure ? metricsIn : approximateMetrics(metricsIn.fontSize);
    if (source.size() > limits::MaxSourceBytes) {
        r.error = detail::makeError(Error::Code::TooLarge, 0, u"source"_s);
        return r;
    }
    r.kind = detectKind(source, &r.typeName);
    switch (r.kind) {
    case Kind::Unknown: {
        const QList<detail::Statement> st = detail::statements(source, false);
        r.error = detail::makeError(st.isEmpty() ? Error::Code::Empty : Error::Code::UnknownType, st.value(0).line);
        return r;
    }
    case Kind::Unsupported: r.error = detail::makeError(Error::Code::Unsupported, 0, r.typeName); return r;
    case Kind::Flowchart: {
        auto parsed = detail::parseFlowchart(source);
        if (!parsed) {
            r.error = parsed.error();
            return r;
        }
        auto laid = detail::layoutFlowchart(*parsed, metrics);
        if (!laid) {
            r.error = laid.error();
            return r;
        }
        r.scene = detail::drawFlowchart(*parsed, *laid, metrics);
        break;
    }
    case Kind::Sequence: {
        auto parsed = detail::parseSequence(source);
        if (!parsed) {
            r.error = parsed.error();
            return r;
        }
        r.scene = detail::drawSequence(*parsed, metrics);
        break;
    }
    case Kind::Pie: {
        auto parsed = detail::parsePie(source);
        if (!parsed) {
            r.error = parsed.error();
            return r;
        }
        r.scene = detail::drawPie(*parsed, metrics);
        break;
    }
    }
    // A drawing beyond any screen (a chain of long links, thousands of `else`): reported, not drawn.
    if (!(r.scene.size.width() <= limits::MaxSceneExtent && r.scene.size.height() <= limits::MaxSceneExtent)) {
        r.scene = Scene{};
        r.error = detail::makeError(Error::Code::TooLarge, 0, u"size"_s);
    }
    return r;
}

} // namespace brulion::diagram
