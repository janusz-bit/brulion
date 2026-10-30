#include "uimetrics.h"

#include "themepalette.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::uibench {

namespace {

ui::Rgb rgb(const QColor &c)
{
    return { c.red(), c.green(), c.blue() };
}

QColor qcolor(const ui::Rgb &c)
{
    return QColor(c.r, c.g, c.b);
}

/// `fg` at opacity `alpha` over the opaque `bg`.
QColor overColor(const QColor &fg, double alpha, const QColor &bg)
{
    return qcolor(ui::over(rgb(fg), std::clamp(alpha, 0.0, 1.0), rgb(bg)));
}

double contrastOf(const QColor &a, const QColor &b)
{
    return ui::contrast(rgb(a), rgb(b));
}

bool hasLetterOrDigit(QStringView s)
{
    return std::any_of(s.begin(), s.end(), [](QChar c) { return c.isLetterOrNumber(); });
}

double round3(double v)
{
    return std::round(v * 1000.0) / 1000.0;
}

QJsonArray rectJson(const QRectF &r)
{
    return { round3(r.x()), round3(r.y()), round3(r.width()), round3(r.height()) };
}

bool sameRect(const QRectF &a, const QRectF &b, double tolerance = 1.0)
{
    return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance
        && std::abs(a.right() - b.right()) <= tolerance && std::abs(a.bottom() - b.bottom()) <= tolerance;
}

bool containsRect(const QRectF &outer, const QRectF &inner, double tolerance = 0.5)
{
    return inner.left() >= outer.left() - tolerance && inner.top() >= outer.top() - tolerance
        && inner.right() <= outer.right() + tolerance && inner.bottom() <= outer.bottom() + tolerance;
}

/// An image whose pixels scanLine() can read as 32-bit RGB.
QImage rgb32(const QImage &png)
{
    if (png.format() == QImage::Format_RGB32 || png.format() == QImage::Format_ARGB32)
        return png;
    return png.convertToFormat(QImage::Format_ARGB32);
}

double halfStep(double px)
{
    return std::round(px * 2.0) / 2.0;
}

// ---- Parsing --------------------------------------------------------------------------------

struct Parser {
    QString error;

    bool fail(const QString &where, const QString &what)
    {
        if (error.isEmpty())
            error = where + u": "_s + what;
        return false;
    }

    bool rect(const QJsonObject &o, const QString &where, QRectF *out)
    {
        const QJsonValue v = o.value(u"rect"_s);
        if (!v.isArray() || v.toArray().size() != 4)
            return fail(where + u".rect"_s, u"expected [x, y, w, h]"_s);
        const QJsonArray a = v.toArray();
        double n[4];
        for (int i = 0; i < 4; ++i) {
            if (!a.at(i).isDouble() || !std::isfinite(a.at(i).toDouble()))
                return fail(where + u".rect"_s, u"not a finite number"_s);
            n[i] = a.at(i).toDouble();
        }
        if (n[2] < 0 || n[3] < 0)
            return fail(where + u".rect"_s, u"negative size"_s);
        *out = QRectF(n[0], n[1], n[2], n[3]);
        return true;
    }

    bool color(const QJsonObject &o, const QString &key, const QString &where, QColor *out, bool required = true)
    {
        const QJsonValue v = o.value(key);
        if (v.isUndefined() || v.isNull() || (v.isString() && v.toString().isEmpty())) {
            if (required)
                return fail(where + u'.' + key, u"missing"_s);
            *out = QColor();
            return true;
        }
        const QColor c = parseHex(v.toString());
        if (!v.isString() || !c.isValid())
            return fail(where + u'.' + key, u"not a #rrggbb(aa) colour: "_s + v.toVariant().toString());
        *out = c;
        return true;
    }

    bool number(const QJsonObject &o, const QString &key, const QString &where, double *out, bool required,
                double fallback)
    {
        const QJsonValue v = o.value(key);
        if (v.isUndefined()) {
            if (required)
                return fail(where + u'.' + key, u"missing"_s);
            *out = fallback;
            return true;
        }
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            return fail(where + u'.' + key, u"not a number"_s);
        *out = v.toDouble();
        return true;
    }
};

} // namespace

QColor parseHex(const QString &text)
{
    static const QRegularExpression re(u"^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$"_s);
    const auto m = re.match(text);
    if (!m.hasMatch())
        return {};
    const uint rgbValue = m.captured(1).toUInt(nullptr, 16);
    const int alpha = m.captured(2).isEmpty() ? 255 : m.captured(2).toInt(nullptr, 16);
    return QColor(int((rgbValue >> 16) & 0xff), int((rgbValue >> 8) & 0xff), int(rgbValue & 0xff), alpha);
}

QString toHex(const QColor &c)
{
    if (!c.isValid())
        return {};
    return QString::asprintf("#%02x%02x%02x%02x", c.red(), c.green(), c.blue(), c.alpha());
}

std::optional<LayoutDump> parseDump(const QByteArray &json, QString *error)
{
    auto failWith = [error](const QString &message) -> std::optional<LayoutDump> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return failWith(u"not a JSON object: "_s + parseError.errorString());
    const QJsonObject root = doc.object();
    if (root.value(u"schema"_s).toInt(-1) != 1)
        return failWith(u"schema: expected 1"_s);
    const QJsonValue viewport = root.value(u"viewport"_s);
    if (!viewport.isObject())
        return failWith(u"viewport: missing"_s);
    LayoutDump dump;
    dump.app = root.value(u"app"_s).toString();
    const QJsonObject vp = viewport.toObject();
    const double w = vp.value(u"w"_s).toDouble(-1), h = vp.value(u"h"_s).toDouble(-1);
    dump.dpr = vp.value(u"dpr"_s).toDouble(1);
    if (!(w > 0) || !(h > 0) || !(dump.dpr > 0) || !std::isfinite(w) || !std::isfinite(h) || !std::isfinite(dump.dpr))
        return failWith(u"viewport: needs w, h and dpr > 0"_s);
    dump.viewport = QSizeF(w, h);

    Parser p;
    for (const auto &key : { u"texts"_s, u"targets"_s, u"boxes"_s }) {
        if (!root.value(key).isArray())
            return failWith(key + u": not an array"_s);
    }
    const QJsonArray texts = root.value(u"texts"_s).toArray();
    dump.texts.reserve(texts.size());
    for (qsizetype i = 0; i < texts.size(); ++i) {
        const QString where = u"texts[%1]"_s.arg(i);
        if (!texts.at(i).isObject())
            return failWith(where + u": not an object"_s);
        const QJsonObject o = texts.at(i).toObject();
        TextItem t;
        double weight = 400, lines = 1, chars = 0;
        if (!p.rect(o, where, &t.rect) || !p.color(o, u"color"_s, where, &t.color)
            || !p.number(o, u"alpha"_s, where, &t.alpha, false, 1.0) || !p.number(o, u"px"_s, where, &t.px, true, 0)
            || !p.number(o, u"weight"_s, where, &weight, false, 400) || !p.number(o, u"lines"_s, where, &lines, false, 1))
            return failWith(p.error);
        t.type = o.value(u"type"_s).toString();
        t.name = o.value(u"name"_s).toString();
        t.text = o.value(u"text"_s).toString();
        t.family = o.value(u"family"_s).toString();
        if (!p.number(o, u"chars"_s, where, &chars, false, double(t.text.size())))
            return failWith(p.error);
        t.chars = int(chars);
        t.weight = int(weight);
        t.lines = std::max(1, int(lines));
        t.enabled = o.value(u"enabled"_s).toBool(true);
        t.occluded = o.value(u"occluded"_s).toBool(false);
        t.placeholder = o.value(u"placeholder"_s).toBool(false);
        const QJsonValue fragments = o.value(u"fragments"_s);
        if (!fragments.isUndefined() && !fragments.isNull()) {
            if (!fragments.isArray())
                return failWith(where + u".fragments: not an array"_s);
            const QJsonArray fa = fragments.toArray();
            for (qsizetype j = 0; j < fa.size(); ++j) {
                const QString fw = where + u".fragments[%1]"_s.arg(j);
                const QJsonObject fo = fa.at(j).toObject();
                Fragment f;
                QColor bg;
                double fchars = 0;
                f.text = fo.value(u"text"_s).toString();
                if (!p.color(fo, u"color"_s, fw, &f.color) || !p.color(fo, u"bg"_s, fw, &bg, false)
                    || !p.number(fo, u"chars"_s, fw, &fchars, false, double(f.text.size())))
                    return failWith(p.error);
                f.chars = int(fchars);
                if (bg.isValid())
                    f.background = bg;
                t.fragments.append(f);
            }
        }
        dump.texts.append(t);
    }
    const QJsonArray targets = root.value(u"targets"_s).toArray();
    for (qsizetype i = 0; i < targets.size(); ++i) {
        const QString where = u"targets[%1]"_s.arg(i);
        if (!targets.at(i).isObject())
            return failWith(where + u": not an object"_s);
        const QJsonObject o = targets.at(i).toObject();
        Target t;
        if (!p.rect(o, where, &t.rect))
            return failWith(p.error);
        t.type = o.value(u"type"_s).toString();
        t.name = o.value(u"name"_s).toString();
        t.kind = o.value(u"kind"_s).toString();
        t.label = o.value(u"label"_s).toString();
        t.enabled = o.value(u"enabled"_s).toBool(true);
        t.inlineTarget = o.value(u"inline"_s).toBool(false);
        t.occluded = o.value(u"occluded"_s).toBool(false);
        dump.targets.append(t);
    }
    const QJsonArray boxes = root.value(u"boxes"_s).toArray();
    for (qsizetype i = 0; i < boxes.size(); ++i) {
        const QString where = u"boxes[%1]"_s.arg(i);
        if (!boxes.at(i).isObject())
            return failWith(where + u": not an object"_s);
        const QJsonObject o = boxes.at(i).toObject();
        Box b;
        double paint = double(i);
        if (!p.rect(o, where, &b.rect) || !p.color(o, u"color"_s, where, &b.color)
            || !p.number(o, u"alpha"_s, where, &b.alpha, false, 1.0) || !p.number(o, u"border"_s, where, &b.border, false, 0)
            || !p.number(o, u"radius"_s, where, &b.radius, false, 0) || !p.number(o, u"paint"_s, where, &paint, false, double(i)))
            return failWith(p.error);
        b.paint = int(paint);
        dump.boxes.append(b);
    }
    const QJsonValue tokens = root.value(u"tokens"_s);
    if (tokens.isObject()) {
        const QJsonObject to = tokens.toObject();
        for (auto it = to.begin(); it != to.end(); ++it) {
            QColor c;
            if (!p.color(to, it.key(), u"tokens"_s, &c))
                return failWith(p.error);
            dump.tokens.insert(it.key(), c);
        }
    }
    return dump;
}

// ---- Contrast --------------------------------------------------------------------------------

QColor sampleBackground(const QImage &png, const QRectF &logical, double dpr)
{
    if (png.isNull() || logical.isEmpty())
        return {};
    const QImage img = rgb32(png);
    const double ring = 2.0 * dpr;
    const QRect area = QRectF(logical.x() * dpr - ring, logical.y() * dpr - ring, logical.width() * dpr + 2 * ring,
                              logical.height() * dpr + 2 * ring)
                           .toAlignedRect()
                           .intersected(img.rect());
    if (area.isEmpty())
        return {};
    const double pixels = double(area.width()) * area.height();
    const int step = std::max(1, int(std::ceil(std::sqrt(pixels / 20000.0))));
    QHash<int, int> bins;
    QHash<QRgb, int> exact;
    for (int y = area.top(); y <= area.bottom(); y += step) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = area.left(); x <= area.right(); x += step) {
            const QRgb px = line[x] | 0xff000000u;
            ++bins[((qRed(px) >> 3) << 10) | ((qGreen(px) >> 3) << 5) | (qBlue(px) >> 3)];
            ++exact[px];
        }
    }
    int bestBin = -1, bestCount = -1;
    for (auto it = bins.cbegin(); it != bins.cend(); ++it) {
        if (it.value() > bestCount || (it.value() == bestCount && it.key() < bestBin)) {
            bestBin = it.key();
            bestCount = it.value();
        }
    }
    QRgb best = 0;
    int bestExact = -1;
    for (auto it = exact.cbegin(); it != exact.cend(); ++it) {
        const QRgb px = it.key();
        const int bin = ((qRed(px) >> 3) << 10) | ((qGreen(px) >> 3) << 5) | (qBlue(px) >> 3);
        if (bin != bestBin)
            continue;
        if (it.value() > bestExact || (it.value() == bestExact && px < best)) {
            best = px;
            bestExact = it.value();
        }
    }
    return QColor(qRed(best), qGreen(best), qBlue(best));
}

QList<TextContrast> textContrast(const LayoutDump &dump, const QImage &png)
{
    const QImage img = rgb32(png);
    QList<TextContrast> out;
    for (const TextItem &t : dump.texts) {
        if ((!t.enabled && !t.placeholder) || t.occluded || t.rect.isEmpty())
            continue;
        const QColor bg = sampleBackground(img, t.rect, dump.dpr);
        if (!bg.isValid())
            continue;
        QList<Fragment> fragments = t.fragments;
        const bool whole = fragments.isEmpty();
        if (whole)
            fragments.append(Fragment{ t.text, t.chars, t.color, std::nullopt });
        double minRatio = std::numeric_limits<double>::infinity();
        QColor minFg, minBg;
        for (const Fragment &f : std::as_const(fragments)) {
            if (!whole && f.chars < 3)
                continue;
            if (!hasLetterOrDigit(f.text))
                continue;
            const QColor base = f.background ? overColor(*f.background, f.background->alphaF(), bg) : bg;
            const QColor fg = overColor(f.color, f.color.alphaF() * t.alpha, base);
            const double ratio = contrastOf(fg, base);
            if (ratio < minRatio) {
                minRatio = ratio;
                minFg = fg;
                minBg = base;
            }
        }
        if (!std::isfinite(minRatio))
            continue;
        TextContrast c;
        c.text = t.text;
        c.type = t.type;
        c.name = t.name;
        c.chars = std::max(1, t.chars);
        c.px = t.px;
        c.ratio = minRatio;
        c.large = t.px >= 24.0 - 1e-6 || (t.px >= 18.66 - 1e-6 && t.weight >= 700);
        c.required = c.large ? 3.0 : 4.5;
        c.pass = minRatio + 1e-9 >= c.required;
        c.fg = minFg;
        c.bg = minBg;
        out.append(c);
    }
    return out;
}

// ---- Targets ---------------------------------------------------------------------------------

namespace {

double distanceToRect(const QPointF &p, const QRectF &r)
{
    const double dx = std::max({ r.left() - p.x(), 0.0, p.x() - r.right() });
    const double dy = std::max({ r.top() - p.y(), 0.0, p.y() - r.bottom() });
    return std::hypot(dx, dy);
}

} // namespace

QList<TargetCheck> targetSizes(const LayoutDump &dump, Device device)
{
    QList<const Target *> visible;
    for (const Target &t : dump.targets) {
        if (!t.occluded && !t.rect.isEmpty())
            visible.append(&t);
    }
    auto undersized = [](const Target *t) { return t->rect.width() < 24.0 - 1e-6 || t->rect.height() < 24.0 - 1e-6; };
    QList<TargetCheck> out;
    for (const Target *t : std::as_const(visible)) {
        if (!t->enabled || t->inlineTarget)
            continue;
        TargetCheck c;
        c.type = t->type;
        c.label = t->label.isEmpty() ? t->name : t->label;
        c.kind = t->kind;
        c.rect = t->rect;
        if (device == Device::Phone) {
            c.pass = t->rect.width() >= 44.0 - 0.5 && t->rect.height() >= 44.0 - 0.5;
            c.material = t->rect.width() >= 48.0 - 0.5 && t->rect.height() >= 48.0 - 0.5;
            if (!c.pass)
                c.reason = u"%1×%2 below 44×44"_s.arg(t->rect.width(), 0, 'f', 0).arg(t->rect.height(), 0, 'f', 0);
        } else if (!undersized(t)) {
            c.pass = true;
        } else {
            // WCAG 2.5.8 spacing exception: a 24-px circle on its centre clear of other targets and
            // of other undersized targets' circles. A target containing it (a row around its
            // button) is the same place, not a neighbour.
            const QPointF centre = t->rect.center();
            c.pass = true;
            for (const Target *o : std::as_const(visible)) {
                if (o == t || containsRect(o->rect, t->rect))
                    continue;
                if (distanceToRect(centre, o->rect) < 12.0 - 1e-6) {
                    c.pass = false;
                    c.reason = u"%1×%2, its 24-px circle touches %3"_s.arg(t->rect.width(), 0, 'f', 0)
                                   .arg(t->rect.height(), 0, 'f', 0)
                                   .arg(o->label.isEmpty() ? o->type : o->label);
                    break;
                }
                if (undersized(o) && QLineF(centre, o->rect.center()).length() < 24.0 - 1e-6) {
                    c.pass = false;
                    c.reason = u"%1×%2, its 24-px circle meets the circle of %3"_s.arg(t->rect.width(), 0, 'f', 0)
                                   .arg(t->rect.height(), 0, 'f', 0)
                                   .arg(o->label.isEmpty() ? o->type : o->label);
                    break;
                }
            }
        }
        out.append(c);
    }
    return out;
}

// ---- Spacing, type, alignment, line length ------------------------------------------------------

namespace {

/// Groups the sorted `values` into clusters of single linkage within `gap`; returns cluster sizes
/// per item (same order).
QList<int> clusterSizes(const QList<double> &sorted, double gap)
{
    QList<int> sizes(sorted.size(), 0);
    qsizetype start = 0;
    for (qsizetype i = 1; i <= sorted.size(); ++i) {
        if (i == sorted.size() || sorted.at(i) - sorted.at(i - 1) > gap) {
            for (qsizetype j = start; j < i; ++j)
                sizes[j] = int(i - start);
            start = i;
        }
    }
    return sizes;
}

} // namespace

double spacingGridShare(const LayoutDump &dump, int grid, double tolerance, QList<double> *distances)
{
    QList<double> d;
    auto inset = [&](const QRectF &r) {
        const Box *best = nullptr;
        for (const Box &b : dump.boxes) {
            if (!containsRect(b.rect, r) || sameRect(b.rect, r))
                continue;
            if (!best || b.rect.width() * b.rect.height() < best->rect.width() * best->rect.height())
                best = &b;
        }
        if (best) {
            d.append(std::max(0.0, r.left() - best->rect.left()));
            d.append(std::max(0.0, r.top() - best->rect.top()));
        }
    };
    QList<const TextItem *> texts;
    for (const TextItem &t : dump.texts) {
        if (t.occluded || t.rect.isEmpty())
            continue;
        texts.append(&t);
        inset(t.rect);
    }
    for (const Target &t : dump.targets) {
        if (!t.occluded && !t.rect.isEmpty())
            inset(t.rect);
    }
    // Vertical gaps between consecutive texts sharing a left edge.
    std::sort(texts.begin(), texts.end(), [](const TextItem *a, const TextItem *b) { return a->rect.left() < b->rect.left(); });
    qsizetype start = 0;
    for (qsizetype i = 1; i <= texts.size(); ++i) {
        if (i < texts.size() && texts.at(i)->rect.left() - texts.at(i - 1)->rect.left() <= 2.0)
            continue;
        QList<const TextItem *> column(texts.begin() + start, texts.begin() + i);
        std::sort(column.begin(), column.end(), [](const TextItem *a, const TextItem *b) { return a->rect.top() < b->rect.top(); });
        for (qsizetype j = 1; j < column.size(); ++j) {
            const double gap = column.at(j)->rect.top() - column.at(j - 1)->rect.bottom();
            if (gap > 0 && gap < 64)
                d.append(gap);
        }
        start = i;
    }
    if (distances)
        *distances = d;
    if (d.isEmpty())
        return 1.0;
    int on = 0;
    for (const double v : std::as_const(d)) {
        const double r = std::fmod(v, double(grid));
        if (r <= tolerance + 1e-9 || grid - r <= tolerance + 1e-9)
            ++on;
    }
    return double(on) / d.size();
}

double typeScaleScore(int n, int d, int w)
{
    return std::clamp(10.0 - std::max(0, n - 6) - std::max(0, 3 - n) - 1.5 * d - std::max(0, w - 3), 0.0, 10.0);
}

TypeScale typeScale(const LayoutDump &dump)
{
    QMap<double, double> bySize;
    QMap<int, double> byWeight;
    double total = 0;
    for (const TextItem &t : dump.texts) {
        if (t.occluded || t.rect.isEmpty() || t.chars <= 0 || t.px <= 0)
            continue;
        bySize[halfStep(t.px)] += t.chars;
        byWeight[t.weight] += t.chars;
        total += t.chars;
    }
    TypeScale s;
    if (total <= 0) {
        s.score = 10;
        return s;
    }
    for (auto it = bySize.cbegin(); it != bySize.cend(); ++it) {
        if (it.value() / total >= 0.02)
            s.sizes.append(it.key());
    }
    for (auto it = byWeight.cbegin(); it != byWeight.cend(); ++it) {
        if (it.value() / total >= 0.02)
            s.weights.append(it.key());
    }
    for (qsizetype i = 1; i < s.sizes.size(); ++i) {
        if (s.sizes.at(i) / s.sizes.at(i - 1) < 1.08)
            ++s.nearDuplicates;
    }
    s.score = typeScaleScore(int(s.sizes.size()), s.nearDuplicates, int(s.weights.size()));
    return s;
}

namespace {

struct AlignmentParts {
    double a = 1, r = 1;
    int items = 0, rows = 0, alignedRows = 0;
};

AlignmentParts alignmentParts(const LayoutDump &dump)
{
    AlignmentParts parts;
    QList<double> lefts;
    struct RowItem {
        double top, bottom;
    };
    QList<RowItem> rowItems;
    for (const TextItem &t : dump.texts) {
        if (t.occluded || t.rect.isEmpty())
            continue;
        lefts.append(t.rect.left());
        if (t.lines == 1)
            rowItems.append({ t.rect.top(), t.rect.bottom() });
    }
    for (const Target &t : dump.targets) {
        if (t.occluded || t.rect.isEmpty())
            continue;
        lefts.append(t.rect.left());
        rowItems.append({ t.rect.top(), t.rect.bottom() });
    }
    parts.items = int(lefts.size());
    if (!lefts.isEmpty()) {
        std::sort(lefts.begin(), lefts.end());
        const QList<int> sizes = clusterSizes(lefts, 1.5);
        parts.a = double(std::count_if(sizes.cbegin(), sizes.cend(), [](int n) { return n >= 3; })) / lefts.size();
    }
    std::sort(rowItems.begin(), rowItems.end(), [](const RowItem &x, const RowItem &y) {
        return (x.top + x.bottom) < (y.top + y.bottom);
    });
    qsizetype i = 0;
    while (i < rowItems.size()) {
        const RowItem first = rowItems.at(i);
        double lo = (first.top + first.bottom) / 2, hi = lo;
        qsizetype j = i + 1;
        for (; j < rowItems.size(); ++j) {
            const RowItem &o = rowItems.at(j);
            const double overlap = std::min(first.bottom, o.bottom) - std::max(first.top, o.top);
            const double smaller = std::min(first.bottom - first.top, o.bottom - o.top);
            if (smaller <= 0 || overlap < 0.5 * smaller)
                break;
            const double c = (o.top + o.bottom) / 2;
            lo = std::min(lo, c);
            hi = std::max(hi, c);
        }
        if (j - i >= 2) {
            ++parts.rows;
            if (hi - lo <= 1.5 + 1e-9)
                ++parts.alignedRows;
        }
        i = j;
    }
    if (parts.rows > 0)
        parts.r = double(parts.alignedRows) / parts.rows;
    return parts;
}

} // namespace

double alignmentScore(const LayoutDump &dump)
{
    const AlignmentParts p = alignmentParts(dump);
    return 10.0 * (0.6 * p.a + 0.4 * p.r);
}

namespace {

struct LineLengthParts {
    std::optional<double> share;
    double bodyPx = 0;
    int bodyTexts = 0;
    QList<double> cpl;
};

LineLengthParts lineLengthParts(const LayoutDump &dump, Device device)
{
    LineLengthParts parts;
    if (device != Device::Desktop)
        return parts;
    QMap<double, double> bySize;
    for (const TextItem &t : dump.texts) {
        if (!t.occluded && !t.rect.isEmpty() && t.chars > 0 && t.px > 0)
            bySize[halfStep(t.px)] += t.chars;
    }
    if (bySize.isEmpty())
        return parts;
    double mode = 0, most = -1;
    for (auto it = bySize.cbegin(); it != bySize.cend(); ++it) {
        if (it.value() > most) {
            most = it.value();
            mode = it.key();
        }
    }
    parts.bodyPx = mode;
    double body = 0, good = 0;
    for (const TextItem &t : dump.texts) {
        if (t.occluded || t.rect.isEmpty() || std::abs(t.px - mode) > 1.0 + 1e-9 || t.chars < 120 || t.lines < 2)
            continue;
        const double cpl = double(t.chars) / t.lines;
        parts.cpl.append(cpl);
        ++parts.bodyTexts;
        body += t.chars;
        if (cpl >= 45 && cpl <= 90)
            good += t.chars;
    }
    if (body > 0)
        parts.share = good / body;
    return parts;
}

} // namespace

std::optional<double> lineLengthShare(const LayoutDump &dump, Device device)
{
    return lineLengthParts(dump, device).share;
}

std::optional<double> tokenContrast(const LayoutDump &dump)
{
    static const QStringList fgs{ u"fg"_s, u"fgMuted"_s, u"fgFaint"_s };
    static const QStringList bgs{ u"bg"_s, u"bgSunken"_s };
    double sum = 0;
    int n = 0;
    for (const QString &b : bgs) {
        if (!dump.tokens.contains(b))
            return std::nullopt;
        const QColor bg = overColor(dump.tokens.value(b), dump.tokens.value(b).alphaF(), Qt::white);
        for (const QString &f : fgs) {
            if (!dump.tokens.contains(f))
                return std::nullopt;
            const QColor fg = overColor(dump.tokens.value(f), dump.tokens.value(f).alphaF(), bg);
            sum += std::min(1.0, contrastOf(fg, bg) / 4.5);
            ++n;
        }
    }
    return n ? std::optional<double>(10.0 * sum / n) : std::nullopt;
}

bool lineLengthScreen(const QString &screen)
{
    static const QStringList screens{ u"doc"_s, u"doc-edit"_s, u"daily"_s, u"outline"_s, u"backlinks"_s };
    return screens.contains(screen);
}

double objectiveScore(const Objective &o)
{
    double sum = 0.25 * o.C + 0.20 * o.T + 0.15 * o.Y + 0.15 * o.A + 0.10 * o.S;
    double weight = 0.85;
    if (o.K) {
        sum += 0.10 * *o.K;
        weight += 0.10;
    }
    if (o.L) {
        sum += 0.05 * *o.L;
        weight += 0.05;
    }
    return sum / weight;
}

namespace {

double contrastScore(const QList<TextContrast> &items)
{
    double num = 0, den = 0;
    for (const TextContrast &c : items) {
        num += c.chars * std::min(1.0, c.ratio / c.required);
        den += c.chars;
    }
    return den > 0 ? 10.0 * num / den : 10.0;
}

double targetScore(const QList<TargetCheck> &items)
{
    if (items.isEmpty())
        return 10.0;
    return 10.0 * double(std::count_if(items.cbegin(), items.cend(), [](const TargetCheck &c) { return c.pass; }))
        / items.size();
}

} // namespace

Objective objective(const LayoutDump &dump, const QImage &png, Device device, const QString &screen)
{
    const QImage img = rgb32(png);
    Objective o;
    o.C = contrastScore(textContrast(dump, img));
    o.T = targetScore(targetSizes(dump, device));
    o.S = 10.0 * spacingGridShare(dump);
    o.Y = typeScale(dump).score;
    o.A = alignmentScore(dump);
    if (device == Device::Desktop && lineLengthScreen(screen)) {
        if (const auto share = lineLengthShare(dump, device))
            o.L = 10.0 * *share;
    }
    o.K = tokenContrast(dump);
    o.score = objectiveScore(o);
    return o;
}

// ---- Extras ----------------------------------------------------------------------------------

double edgeDensity(const QImage &png)
{
    const QImage img = rgb32(png);
    const int w = img.width(), h = img.height();
    if (w < 3 || h < 3)
        return 0;
    QList<float> lum(qsizetype(w) * h);
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
            lum[qsizetype(y) * w + x] = float(0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) + 0.0722 * qBlue(line[x])) / 255.0f;
    }
    qint64 edges = 0;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            auto L = [&](int dx, int dy) { return lum[qsizetype(y + dy) * w + (x + dx)]; };
            const float gx = -L(-1, -1) - 2 * L(-1, 0) - L(-1, 1) + L(1, -1) + 2 * L(1, 0) + L(1, 1);
            const float gy = -L(-1, -1) - 2 * L(0, -1) - L(1, -1) + L(-1, 1) + 2 * L(0, 1) + L(1, 1);
            if (gx * gx + gy * gy > 0.01f)
                ++edges;
        }
    }
    return double(edges) / (double(w - 2) * (h - 2));
}

int distinctColours(const QImage &png)
{
    const QImage img = rgb32(png);
    QHash<QRgb, int> counts;
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            ++counts[line[x] | 0xff000000u];
    }
    const double threshold = 0.0005 * img.width() * img.height();
    return int(std::count_if(counts.cbegin(), counts.cend(), [threshold](int n) { return n >= threshold; }));
}

double nonTextContrast(const LayoutDump &dump, const QImage &png)
{
    const QImage img = rgb32(png);
    int n = 0, pass = 0;
    for (const Target &t : dump.targets) {
        if (!t.enabled || t.occluded || t.inlineTarget || t.rect.isEmpty())
            continue;
        const QColor around = sampleBackground(img, t.rect, dump.dpr);
        const QRect area = QRectF(t.rect.x() * dump.dpr, t.rect.y() * dump.dpr, t.rect.width() * dump.dpr,
                                  t.rect.height() * dump.dpr)
                               .toAlignedRect()
                               .intersected(img.rect());
        if (!around.isValid() || area.isEmpty())
            continue;
        const int step = std::max(1, int(std::ceil(std::sqrt(double(area.width()) * area.height() / 20000.0))));
        QHash<int, int> bins;
        QHash<int, QRgb> sample;
        int total = 0;
        for (int y = area.top(); y <= area.bottom(); y += step) {
            const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = area.left(); x <= area.right(); x += step) {
                const QRgb px = line[x];
                const int bin = ((qRed(px) >> 3) << 10) | ((qGreen(px) >> 3) << 5) | (qBlue(px) >> 3);
                ++bins[bin];
                sample.insert(bin, px);
                ++total;
            }
        }
        double best = 1;
        for (auto it = bins.cbegin(); it != bins.cend(); ++it) {
            if (it.value() < 0.02 * total)
                continue;
            const QRgb px = sample.value(it.key());
            best = std::max(best, contrastOf(QColor(qRed(px), qGreen(px), qBlue(px)), around));
        }
        ++n;
        if (best >= 3.0)
            ++pass;
    }
    return n ? double(pass) / n : 1.0;
}

// ---- Reports ---------------------------------------------------------------------------------

namespace {

QJsonValue optionalNumber(const std::optional<double> &v, int decimals = 3)
{
    if (!v)
        return QJsonValue::Null;
    const double f = std::pow(10.0, decimals);
    return std::round(*v * f) / f;
}

QJsonObject scoresJson(const Objective &o)
{
    return {
        { u"C"_s, round3(o.C) }, { u"T"_s, round3(o.T) }, { u"S"_s, round3(o.S) },
        { u"Y"_s, round3(o.Y) }, { u"A"_s, round3(o.A) }, { u"L"_s, optionalNumber(o.L) },
        { u"K"_s, optionalNumber(o.K) }, { u"O"_s, round3(o.score) },
    };
}

double percentile(QList<double> values, double q)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    const double pos = q * (values.size() - 1);
    const auto lo = qsizetype(std::floor(pos));
    const auto hi = qsizetype(std::ceil(pos));
    return values.at(lo) + (values.at(hi) - values.at(lo)) * (pos - lo);
}

} // namespace

QJsonObject measureJson(const LayoutDump &dump, const QImage &png, Device device, const QString &screen,
                        const QJsonObject &meta)
{
    const QImage img = rgb32(png);
    QJsonObject out = meta;
    out.insert(u"schema"_s, 1);
    out.insert(u"app"_s, dump.app);
    out.insert(u"screen"_s, screen);
    out.insert(u"device"_s, device == Device::Desktop ? u"desktop"_s : u"phone"_s);
    out.insert(u"viewport"_s, QJsonObject{ { u"w"_s, dump.viewport.width() }, { u"h"_s, dump.viewport.height() }, { u"dpr"_s, dump.dpr } });

    const Objective o = objective(dump, img, device, screen);
    out.insert(u"scores"_s, scoresJson(o));

    QJsonObject details;
    // C
    QList<TextContrast> contrast = textContrast(dump, img);
    {
        QList<double> ratios;
        double chars = 0, passChars = 0;
        int passCount = 0;
        for (const TextContrast &c : std::as_const(contrast)) {
            ratios.append(c.ratio);
            chars += c.chars;
            if (c.pass) {
                passChars += c.chars;
                ++passCount;
            }
        }
        std::stable_sort(contrast.begin(), contrast.end(), [](const TextContrast &a, const TextContrast &b) {
            return a.ratio / a.required < b.ratio / b.required;
        });
        QJsonArray worst;
        for (const TextContrast &c : std::as_const(contrast)) {
            if (worst.size() >= 30 || c.pass)
                break;
            worst.append(QJsonObject{ { u"text"_s, c.text }, { u"type"_s, c.type }, { u"name"_s, c.name },
                                      { u"px"_s, c.px }, { u"ratio"_s, round3(c.ratio) }, { u"required"_s, c.required },
                                      { u"fg"_s, toHex(c.fg) }, { u"bg"_s, toHex(c.bg) } });
        }
        details.insert(u"contrast"_s, QJsonObject{
            { u"texts"_s, int(contrast.size()) },
            { u"passShare"_s, round3(contrast.isEmpty() ? 1.0 : double(passCount) / contrast.size()) },
            { u"passShareChars"_s, round3(chars > 0 ? passChars / chars : 1.0) },
            { u"min"_s, round3(ratios.isEmpty() ? 0 : *std::min_element(ratios.cbegin(), ratios.cend())) },
            { u"p10"_s, round3(percentile(ratios, 0.10)) },
            { u"failing"_s, worst },
        });
    }
    // T
    {
        const QList<TargetCheck> targets = targetSizes(dump, device);
        int pass = 0, material = 0;
        QJsonArray failing;
        for (const TargetCheck &c : targets) {
            pass += c.pass ? 1 : 0;
            material += c.material ? 1 : 0;
            if (!c.pass && failing.size() < 30)
                failing.append(QJsonObject{ { u"type"_s, c.type }, { u"kind"_s, c.kind }, { u"label"_s, c.label },
                                            { u"rect"_s, rectJson(c.rect) }, { u"reason"_s, c.reason } });
        }
        QJsonObject t{ { u"targets"_s, int(targets.size()) }, { u"pass"_s, pass }, { u"failing"_s, failing } };
        if (device == Device::Phone)
            t.insert(u"material48Share"_s, round3(targets.isEmpty() ? 1.0 : double(material) / targets.size()));
        details.insert(u"targets"_s, t);
    }
    // S
    {
        QList<double> distances;
        const double share = spacingGridShare(dump, 4, 1.0, &distances);
        QMap<int, int> offGrid;
        for (const double v : std::as_const(distances)) {
            const double r = std::fmod(v, 4.0);
            if (!(r <= 1.0 + 1e-9 || 4.0 - r <= 1.0 + 1e-9))
                ++offGrid[int(std::round(v))];
        }
        QJsonObject off;
        for (auto it = offGrid.cbegin(); it != offGrid.cend(); ++it)
            off.insert(QString::number(it.key()), it.value());
        details.insert(u"spacing"_s, QJsonObject{ { u"distances"_s, int(distances.size()) }, { u"onGridShare"_s, round3(share) },
                                                  { u"offGrid"_s, off } });
    }
    // Y
    {
        const TypeScale s = typeScale(dump);
        QJsonArray sizes, weights;
        for (const double v : s.sizes)
            sizes.append(v);
        for (const int v : s.weights)
            weights.append(v);
        details.insert(u"typeScale"_s, QJsonObject{ { u"sizes"_s, sizes }, { u"weights"_s, weights },
                                                    { u"nearDuplicates"_s, s.nearDuplicates } });
    }
    // A
    {
        const AlignmentParts p = alignmentParts(dump);
        details.insert(u"alignment"_s, QJsonObject{ { u"items"_s, p.items }, { u"sharedLeftShare"_s, round3(p.a) },
                                                    { u"rows"_s, p.rows }, { u"alignedRows"_s, p.alignedRows } });
    }
    // L
    {
        const LineLengthParts p = lineLengthParts(dump, device);
        QJsonArray cpl;
        for (const double v : p.cpl)
            cpl.append(std::round(v * 10) / 10);
        details.insert(u"lineLength"_s, QJsonObject{ { u"counted"_s, device == Device::Desktop && lineLengthScreen(screen) },
                                                     { u"share"_s, optionalNumber(p.share) },
                                                     { u"bodyPx"_s, p.bodyPx }, { u"bodyTexts"_s, p.bodyTexts },
                                                     { u"charsPerLine"_s, cpl } });
    }
    // K
    {
        QJsonObject tokens;
        for (auto it = dump.tokens.cbegin(); it != dump.tokens.cend(); ++it)
            tokens.insert(it.key(), toHex(it.value()));
        details.insert(u"tokens"_s, tokens);
    }
    details.insert(u"extras"_s, QJsonObject{ { u"edgeDensity"_s, round3(edgeDensity(img)) },
                                             { u"distinctColours"_s, distinctColours(img) },
                                             { u"nonTextContrast"_s, round3(nonTextContrast(dump, img)) } });
    out.insert(u"details"_s, details);
    return out;
}

QJsonObject summarize(const QList<QJsonObject> &measurements)
{
    static const QStringList keys{ u"C"_s, u"T"_s, u"S"_s, u"Y"_s, u"A"_s, u"L"_s, u"K"_s, u"O"_s };
    struct Shot {
        QString app, screen, size, theme;
        QJsonObject scores;
    };
    QList<Shot> shots;
    QJsonArray shotsJson;
    for (const QJsonObject &m : measurements) {
        Shot s{ m.value(u"app"_s).toString(), m.value(u"screen"_s).toString(), m.value(u"size"_s).toString(),
                m.value(u"theme"_s).toString(), m.value(u"scores"_s).toObject() };
        if (s.app.isEmpty() || s.screen.isEmpty())
            continue;
        shots.append(s);
        QJsonObject j = s.scores;
        j.insert(u"app"_s, s.app);
        j.insert(u"screen"_s, s.screen);
        j.insert(u"size"_s, s.size);
        j.insert(u"theme"_s, s.theme);
        shotsJson.append(j);
    }
    auto pairKey = [](const Shot &s) { return s.screen + u'|' + s.size + u'|' + s.theme; };
    QMap<QString, QHash<QString, QJsonObject>> byPair; // key → app → scores
    QStringList apps;
    for (const Shot &s : std::as_const(shots)) {
        byPair[pairKey(s)].insert(s.app, s.scores);
        if (!apps.contains(s.app))
            apps.append(s.app);
    }
    std::sort(apps.begin(), apps.end());
    auto means = [&](const QString &app, bool pairedOnly) {
        QJsonObject out;
        int count = 0;
        for (const QString &k : keys) {
            double sum = 0;
            int n = 0;
            for (auto it = byPair.cbegin(); it != byPair.cend(); ++it) {
                if (!it.value().contains(app) || (pairedOnly && it.value().size() < 2))
                    continue;
                const QJsonValue v = it.value().value(app).value(k);
                if (v.isDouble()) {
                    sum += v.toDouble();
                    ++n;
                }
            }
            out.insert(k, n ? QJsonValue(round3(sum / n)) : QJsonValue(QJsonValue::Null));
            if (k == u"O")
                count = n;
        }
        out.insert(u"shots"_s, count);
        return out;
    };
    QJsonArray pairs;
    for (auto it = byPair.cbegin(); it != byPair.cend(); ++it) {
        const QStringList parts = it.key().split(u'|');
        QJsonObject p{ { u"screen"_s, parts.value(0) }, { u"size"_s, parts.value(1) }, { u"theme"_s, parts.value(2) } };
        for (auto a = it.value().cbegin(); a != it.value().cend(); ++a)
            p.insert(a.key(), a.value());
        if (it.value().contains(u"brulion"_s) && it.value().contains(u"obsidian"_s)) {
            const double ob = it.value().value(u"brulion"_s).value(u"O"_s).toDouble();
            const double oo = it.value().value(u"obsidian"_s).value(u"O"_s).toDouble();
            p.insert(u"deltaO"_s, round3(ob - oo));
        }
        pairs.append(p);
    }
    QJsonObject meansJson;
    for (const QString &app : std::as_const(apps))
        meansJson.insert(app, QJsonObject{ { u"all"_s, means(app, false) }, { u"paired"_s, means(app, true) } });
    return {
        { u"schema"_s, 1 },
        { u"shots"_s, shotsJson },
        { u"pairs"_s, pairs },
        { u"means"_s, meansJson },
    };
}

// ---- screens.json ----------------------------------------------------------------------------

QStringList checkManifest(const QByteArray &json)
{
    // Verb → number of arguments (obsidian-shots.mjs STEPS).
    static const QHash<QString, int> verbs{
        { u"open"_s, 1 }, { u"blur"_s, 0 }, { u"cursorText"_s, 2 }, { u"caretSteady"_s, 0 },
        { u"command"_s, 1 }, { u"type"_s, 1 }, { u"key"_s, 1 }, { u"leftSidebar"_s, 1 },
        { u"rightSidebar"_s, 1 }, { u"sidePane"_s, 1 }, { u"search"_s, 1 }, { u"settings"_s, 1 },
        { u"wait"_s, 1 }, { u"eval"_s, 1 }, { u"scroll"_s, 1 },
    };
    QStringList errors;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return { u"not a JSON object: "_s + parseError.errorString() };
    const QJsonObject root = doc.object();
    if (root.value(u"schema"_s).toInt(-1) != 1)
        errors.append(u"schema: expected 1"_s);
    const QJsonObject sizes = root.value(u"sizes"_s).toObject();
    if (sizes.isEmpty())
        errors.append(u"sizes: missing"_s);
    for (auto it = sizes.begin(); it != sizes.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        if (!(s.value(u"width"_s).toDouble() > 0) || !(s.value(u"height"_s).toDouble() > 0)
            || !(s.value(u"scale"_s).toDouble() > 0) || !s.value(u"touch"_s).isBool())
            errors.append(u"sizes.%1: needs width, height, scale > 0 and touch"_s.arg(it.key()));
    }
    if (!root.value(u"themes"_s).isArray() || root.value(u"themes"_s).toArray().isEmpty())
        errors.append(u"themes: missing"_s);
    if (!root.value(u"screens"_s).isArray()) {
        errors.append(u"screens: not an array"_s);
        return errors;
    }
    static const QRegularExpression idRe(u"^[a-z0-9][a-z0-9-]*$"_s);
    QSet<QString> ids;
    const QJsonArray screens = root.value(u"screens"_s).toArray();
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const QJsonObject s = screens.at(i).toObject();
        const QString id = s.value(u"id"_s).toString();
        const QString where = id.isEmpty() ? u"screens[%1]"_s.arg(i) : id;
        if (!idRe.match(id).hasMatch())
            errors.append(where + u": bad id"_s);
        else if (ids.contains(id))
            errors.append(where + u": duplicate id"_s);
        ids.insert(id);
        if (!s.value(u"context"_s).isString() || s.value(u"context"_s).toString().isEmpty())
            errors.append(where + u": context missing"_s);
        const QJsonValue screenSizes = s.value(u"sizes"_s);
        if (!screenSizes.isArray() || screenSizes.toArray().isEmpty())
            errors.append(where + u": sizes missing"_s);
        for (const QJsonValue &v : screenSizes.toArray()) {
            if (!sizes.contains(v.toString()))
                errors.append(where + u": unknown size "_s + v.toVariant().toString());
        }
        const QJsonObject brulion = s.value(u"brulion"_s).toObject();
        if (!brulion.value(u"args"_s).isArray())
            errors.append(where + u": brulion.args not a list"_s);
        for (const QJsonValue &v : brulion.value(u"args"_s).toArray()) {
            if (!v.isString())
                errors.append(where + u": brulion.args has a non-string"_s);
        }
        if (!brulion.value(u"delayMs"_s).isDouble())
            errors.append(where + u": brulion.delayMs missing"_s);
        const QJsonObject obsidian = s.value(u"obsidian"_s).toObject();
        if (!obsidian.value(u"steps"_s).isArray())
            errors.append(where + u": obsidian.steps not a list"_s);
        for (const QJsonValue &step : obsidian.value(u"steps"_s).toArray()) {
            const QJsonArray a = step.toArray();
            const QString verb = a.isEmpty() ? QString() : a.at(0).toString();
            if (!verbs.contains(verb)) {
                errors.append(where + u": unknown verb "_s + (verb.isEmpty() ? u"(none)"_s : verb));
                continue;
            }
            if (a.size() - 1 != verbs.value(verb))
                errors.append(u"%1: %2 takes %3 argument(s), got %4"_s.arg(where, verb).arg(verbs.value(verb)).arg(a.size() - 1));
        }
        if (obsidian.contains(u"settleMs"_s) && !obsidian.value(u"settleMs"_s).isDouble())
            errors.append(where + u": obsidian.settleMs not a number"_s);
    }
    return errors;
}

// ---- Lint ------------------------------------------------------------------------------------

QList<LintFailure> lint(const LayoutDump &dump, const QImage &png, Device device, const QString &screen,
                        const QByteArray &allow, QString *error)
{
    struct Allowed {
        QString screen, type, text, name;
    };
    QList<Allowed> allowed;
    if (!allow.trimmed().isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(allow, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            if (error)
                *error = u"allow list: not a JSON array"_s;
            return {};
        }
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            if (o.value(u"reason"_s).toString().isEmpty()) {
                if (error)
                    *error = u"allow list: every entry needs a reason"_s;
                return {};
            }
            allowed.append({ o.value(u"screen"_s).toString(), o.value(u"type"_s).toString(), o.value(u"text"_s).toString(),
                             o.value(u"name"_s).toString() });
        }
    }
    auto excused = [&](const QString &type, const QString &text, const QString &name) {
        return std::any_of(allowed.cbegin(), allowed.cend(), [&](const Allowed &a) {
            return (a.screen.isEmpty() || a.screen == u"*" || a.screen == screen) && (a.type.isEmpty() || a.type == type)
                && (a.text.isEmpty() || text.startsWith(a.text)) && (a.name.isEmpty() || a.name == name)
                && !(a.text.isEmpty() && a.name.isEmpty() && a.type.isEmpty());
        });
    };
    QList<LintFailure> out;
    for (const TextContrast &c : textContrast(dump, png)) {
        if (c.pass || excused(c.type, c.text, c.name))
            continue;
        out.append({ u"contrast"_s, c.type, c.text, c.name,
                     u"%1:1 < %2:1 (%3 on %4, %5 px)"_s.arg(c.ratio, 0, 'f', 2).arg(c.required, 0, 'f', 1)
                         .arg(toHex(c.fg), toHex(c.bg)).arg(c.px) });
    }
    for (const TargetCheck &t : targetSizes(dump, device)) {
        if (t.pass || excused(t.type, t.label, t.label))
            continue;
        out.append({ u"target"_s, t.type, t.label, QString(), t.reason });
    }
    return out;
}

} // namespace brulion::uibench
