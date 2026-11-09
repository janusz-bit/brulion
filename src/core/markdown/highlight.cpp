#include "markdown/highlight.h"

#include "markdown/inline.h"
#include "markdown/lines_p.h"

#include <QStringTokenizer>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

using namespace detail;

namespace {

struct Entry {
    HighlightColor color;
    QStringView name;
    QStringView written; ///< Highlightr's default (what Brulion writes)
    QStringView tint;    ///< what the app draws
};

// Highlightr's default highlighters (its settings: Pink … Grey, "inline-css" style).
constexpr std::array<Entry, HighlightColorCount> Entries{{
        {HighlightColor::Default, u"default", u"", u""},
        {HighlightColor::Red, u"red", u"#FF5582A6", u"#5ce5484d"},
        {HighlightColor::Orange, u"orange", u"#FFB86CA6", u"#5cf2913d"},
        {HighlightColor::Yellow, u"yellow", u"#FFF3A3A6", u"#6bf2d23d"},
        {HighlightColor::Green, u"green", u"#BBFABBA6", u"#5c46b85a"},
        {HighlightColor::Cyan, u"cyan", u"#ABF7F7A6", u"#5c2fb8c4"},
        {HighlightColor::Blue, u"blue", u"#ADCCFFA6", u"#5c4a8ff0"},
        {HighlightColor::Purple, u"purple", u"#D2B3FFA6", u"#5c9b6cf0"},
        {HighlightColor::Pink, u"pink", u"#FFB8EBA6", u"#5ce86fc0"},
        {HighlightColor::Grey, u"grey", u"#CACFD9A6", u"#5c8a93a3"},
}};

const Entry &entryOf(HighlightColor color)
{
    return Entries[std::size_t(color)];
}

int hexValue(QChar c)
{
    if (isAsciiDigit(c))
        return c.unicode() - u'0';
    if (c >= u'a' && c <= u'f')
        return c.unicode() - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c.unicode() - u'A' + 10;
    return -1;
}

/// The first position from `i` of `s` that is no blank (or the end).
qsizetype skipBlanks(QStringView s, qsizetype i)
{
    while (i < s.size() && isSpaceOrTab(s[i]))
        ++i;
    return i;
}

struct Rgb {
    double r = 0; ///< 0 … 255
    double g = 0;
    double b = 0;
};

/// CSS colour names a highlight may be given, by the palette colour they read as.
std::optional<HighlightColor> namedColor(QStringView name)
{
    static const QList<std::pair<QStringView, HighlightColor>> names{
            {u"red", HighlightColor::Red},          {u"darkred", HighlightColor::Red},
            {u"crimson", HighlightColor::Red},      {u"tomato", HighlightColor::Red},
            {u"salmon", HighlightColor::Red},       {u"firebrick", HighlightColor::Red},
            {u"orange", HighlightColor::Orange},    {u"darkorange", HighlightColor::Orange},
            {u"coral", HighlightColor::Orange},     {u"peachpuff", HighlightColor::Orange},
            {u"yellow", HighlightColor::Yellow},    {u"gold", HighlightColor::Yellow},
            {u"khaki", HighlightColor::Yellow},     {u"lightyellow", HighlightColor::Yellow},
            {u"green", HighlightColor::Green},      {u"lime", HighlightColor::Green},
            {u"lightgreen", HighlightColor::Green}, {u"darkgreen", HighlightColor::Green},
            {u"limegreen", HighlightColor::Green},  {u"palegreen", HighlightColor::Green},
            {u"olive", HighlightColor::Green},      {u"seagreen", HighlightColor::Green},
            {u"cyan", HighlightColor::Cyan},        {u"aqua", HighlightColor::Cyan},
            {u"teal", HighlightColor::Cyan},        {u"turquoise", HighlightColor::Cyan},
            {u"lightcyan", HighlightColor::Cyan},   {u"aquamarine", HighlightColor::Cyan},
            {u"blue", HighlightColor::Blue},        {u"navy", HighlightColor::Blue},
            {u"lightblue", HighlightColor::Blue},   {u"skyblue", HighlightColor::Blue},
            {u"royalblue", HighlightColor::Blue},   {u"dodgerblue", HighlightColor::Blue},
            {u"purple", HighlightColor::Purple},    {u"violet", HighlightColor::Purple},
            {u"indigo", HighlightColor::Purple},    {u"lavender", HighlightColor::Purple},
            {u"plum", HighlightColor::Purple},      {u"orchid", HighlightColor::Purple},
            {u"pink", HighlightColor::Pink},        {u"hotpink", HighlightColor::Pink},
            {u"lightpink", HighlightColor::Pink},   {u"magenta", HighlightColor::Pink},
            {u"fuchsia", HighlightColor::Pink},     {u"deeppink", HighlightColor::Pink},
            {u"gray", HighlightColor::Grey},        {u"grey", HighlightColor::Grey},
            {u"silver", HighlightColor::Grey},      {u"lightgray", HighlightColor::Grey},
            {u"lightgrey", HighlightColor::Grey},   {u"darkgray", HighlightColor::Grey},
            {u"darkgrey", HighlightColor::Grey},    {u"gainsboro", HighlightColor::Grey},
            {u"black", HighlightColor::Grey},       {u"white", HighlightColor::Grey},
    };
    for (const auto &[n, color] : names) {
        if (name.compare(n, Qt::CaseInsensitive) == 0)
            return color;
    }
    return std::nullopt;
}

/// The palette colour nearest to `rgb` by hue (grey when barely coloured).
HighlightColor nearestColor(const Rgb &rgb)
{
    const double r = rgb.r / 255.0;
    const double g = rgb.g / 255.0;
    const double b = rgb.b / 255.0;
    const double max = std::max({r, g, b});
    const double min = std::min({r, g, b});
    const double delta = max - min;
    const double light = (max + min) / 2;
    const double saturation = delta <= 0 ? 0 : delta / (1 - std::abs(2 * light - 1));
    if (delta < 0.04 || saturation < 0.2 || light < 0.08 || light > 0.97)
        return HighlightColor::Grey;
    double hue = 0;
    if (max == r)
        hue = 60 * std::fmod((g - b) / delta, 6.0);
    else if (max == g)
        hue = 60 * ((b - r) / delta + 2);
    else
        hue = 60 * ((r - g) / delta + 4);
    if (hue < 0)
        hue += 360;
    if (hue >= 335 || hue < 15)
        return HighlightColor::Red;
    if (hue < 42)
        return HighlightColor::Orange;
    if (hue < 70)
        return HighlightColor::Yellow;
    if (hue < 165)
        return HighlightColor::Green;
    if (hue < 200)
        return HighlightColor::Cyan;
    if (hue < 255)
        return HighlightColor::Blue;
    if (hue < 290)
        return HighlightColor::Purple;
    return HighlightColor::Pink;
}

/// A colour value starting at `i` of `v` (#hex, rgb()/rgba(), a name); `end` past it.
std::optional<HighlightColor> colorAt(QStringView v, qsizetype i, qsizetype &end)
{
    if (v[i] == u'#') {
        qsizetype j = i + 1;
        while (j < v.size() && hexValue(v[j]) >= 0)
            ++j;
        end = j;
        const qsizetype n = j - i - 1;
        if (n != 3 && n != 4 && n != 6 && n != 8)
            return std::nullopt;
        // Highlightr's own colours (what Brulion writes) exactly, with or without their alpha.
        const QStringView hex = v.sliced(i, j - i);
        for (const Entry &e : Entries) {
            if (!e.written.isEmpty()
                && (hex.compare(e.written, Qt::CaseInsensitive) == 0
                    || hex.compare(e.written.first(7), Qt::CaseInsensitive) == 0))
                return e.color;
        }
        Rgb rgb;
        if (n == 3 || n == 4) {
            rgb = {hexValue(v[i + 1]) * 17.0, hexValue(v[i + 2]) * 17.0, hexValue(v[i + 3]) * 17.0};
        } else {
            rgb = {hexValue(v[i + 1]) * 16.0 + hexValue(v[i + 2]), hexValue(v[i + 3]) * 16.0 + hexValue(v[i + 4]),
                   hexValue(v[i + 5]) * 16.0 + hexValue(v[i + 6])};
        }
        return nearestColor(rgb);
    }
    qsizetype j = i;
    while (j < v.size() && (isAsciiLetter(v[j]) || v[j] == u'-'))
        ++j;
    end = std::max(j, i + 1);
    const QStringView word = v.sliced(i, j - i);
    if (word.compare(u"rgb", Qt::CaseInsensitive) == 0 || word.compare(u"rgba", Qt::CaseInsensitive) == 0) {
        if (j >= v.size() || v[j] != u'(')
            return std::nullopt;
        const qsizetype close = v.indexOf(u')', j);
        if (close < 0 || close - j > 80)
            return std::nullopt;
        end = close + 1;
        std::array<double, 3> c{};
        qsizetype k = j + 1;
        for (double &channel : c) {
            while (k < close && (isSpaceOrTab(v[k]) || v[k] == u','))
                ++k;
            qsizetype d = k;
            while (d < close && (isAsciiDigit(v[d]) || v[d] == u'.'))
                ++d;
            if (d == k)
                return std::nullopt;
            bool ok = false;
            double value = v.sliced(k, d - k).toDouble(&ok);
            if (!ok)
                return std::nullopt;
            if (d < close && v[d] == u'%') {
                value *= 2.55;
                ++d;
            }
            channel = std::clamp(value, 0.0, 255.0);
            k = d;
        }
        return nearestColor({c[0], c[1], c[2]});
    }
    return word.isEmpty() ? std::nullopt : namedColor(word);
}

/// The first colour of a CSS value (`linear-gradient(…, #FF5582A6 …)` too).
std::optional<HighlightColor> firstColor(QStringView value)
{
    for (qsizetype i = 0; i < value.size();) {
        const QChar c = value[i];
        if (c == u'#' || isAsciiLetter(c)) {
            qsizetype end = i + 1;
            if (const auto color = colorAt(value, i, end))
                return color;
            i = end;
        } else {
            ++i;
        }
    }
    return std::nullopt;
}

/// A run of exactly two '=' at [i, i + 2) of `s`: {can it open, can it close} a highlight
/// (CommonMark's flanking rules, as the renderer applies them).
std::pair<bool, bool> flanking(QStringView s, qsizetype i)
{
    const QChar prev = i > 0 ? s[i - 1] : QChar(u' ');
    const QChar next = i + 2 < s.size() ? s[i + 2] : QChar(u' ');
    const auto punct = [](QChar c) { return c.isPunct() || c.isSymbol(); };
    return {!next.isSpace() && (!punct(next) || prev.isSpace() || punct(prev)),
            !prev.isSpace() && (!punct(prev) || next.isSpace() || punct(next))};
}

} // namespace

QString highlightColorName(HighlightColor color)
{
    return entryOf(color).name.toString();
}

std::optional<HighlightColor> parseHighlightColorName(QStringView name)
{
    if (name.compare(u"gray", Qt::CaseInsensitive) == 0)
        return HighlightColor::Grey;
    for (const Entry &e : Entries) {
        if (name.compare(e.name, Qt::CaseInsensitive) == 0)
            return e.color;
    }
    return std::nullopt;
}

QString highlightWrittenColor(HighlightColor color)
{
    return entryOf(color).written.toString();
}

QString highlightTint(HighlightColor color)
{
    return entryOf(color).tint.toString();
}

HighlightColor highlightColorFromStyle(QStringView style)
{
    // The last background declaration wins, as in CSS.
    std::optional<HighlightColor> found;
    for (QStringView decl : style.tokenize(u';')) {
        const qsizetype colon = decl.indexOf(u':');
        if (colon < 0)
            continue;
        const QStringView property = decl.first(colon).trimmed();
        if (property.compare(u"background", Qt::CaseInsensitive) != 0
            && property.compare(u"background-color", Qt::CaseInsensitive) != 0)
            continue;
        if (const auto color = firstColor(decl.sliced(colon + 1)))
            found = color;
    }
    return found.value_or(HighlightColor::Default);
}

HighlightColor highlightColorFromClass(QStringView classes)
{
    for (QStringView cls : classes.tokenize(u' ', Qt::SkipEmptyParts)) {
        if (cls.startsWith(u"hltr-", Qt::CaseInsensitive)) {
            if (const auto color = parseHighlightColorName(cls.sliced(5)))
                return *color;
        }
    }
    return HighlightColor::Default;
}

std::optional<MarkTag> markTagAt(QStringView text, qsizetype at)
{
    if (at < 0 || at >= text.size() || text[at] != u'<')
        return std::nullopt;
    constexpr qsizetype MaxTag = 512;
    const QStringView s = text.first(std::min(text.size(), at + MaxTag)); // where the tag has to end
    MarkTag tag;
    tag.from = at;
    tag.closing = charAt(s, at + 1) == u'/';
    qsizetype i = tag.closing ? at + 2 : at + 1;
    if (i + 4 > s.size() || s.sliced(i, 4).compare(u"mark", Qt::CaseInsensitive) != 0)
        return std::nullopt;
    i += 4;
    if (tag.closing) {
        i = skipBlanks(s, i);
        if (charAt(s, i) != u'>')
            return std::nullopt;
        tag.to = i + 1;
        return tag;
    }
    std::optional<QStringView> style;
    std::optional<QStringView> classes;
    while (true) {
        const qsizetype wsStart = i;
        i = skipBlanks(s, i);
        if (i >= s.size())
            return std::nullopt;
        if (s[i] == u'>')
            break;
        // An attribute needs a blank before it (`<markx>` is another tag); no self-closing mark.
        if (i == wsStart || !(isAsciiLetter(s[i]) || s[i] == u'_' || s[i] == u':'))
            return std::nullopt;
        const qsizetype nameFrom = i;
        while (i < s.size()
               && (isAsciiLetter(s[i]) || isAsciiDigit(s[i]) || s[i] == u'_' || s[i] == u'.' || s[i] == u':'
                   || s[i] == u'-'))
            ++i;
        const QStringView name = s.sliced(nameFrom, i - nameFrom);
        qsizetype j = skipBlanks(s, i);
        if (charAt(s, j) != u'=')
            continue; // an attribute without a value
        j = skipBlanks(s, j + 1);
        if (j >= s.size())
            return std::nullopt;
        QStringView value;
        if (const QChar quote = s[j]; quote == u'"' || quote == u'\'') {
            qsizetype k = j + 1;
            while (k < s.size() && s[k] != quote && !isLineBreak(s[k]))
                ++k;
            if (charAt(s, k) != quote)
                return std::nullopt;
            value = s.sliced(j + 1, k - j - 1);
            i = k + 1;
        } else {
            constexpr QStringView forbidden = u"\"'=<>`";
            i = j;
            while (i < s.size() && !s[i].isSpace() && !forbidden.contains(s[i]))
                ++i;
            if (i == j)
                return std::nullopt;
            value = s.sliced(j, i - j);
        }
        if (!style && name.compare(u"style", Qt::CaseInsensitive) == 0)
            style = value;
        else if (!classes && name.compare(u"class", Qt::CaseInsensitive) == 0)
            classes = value;
    }
    tag.to = i + 1;
    if (style)
        tag.color = highlightColorFromStyle(*style);
    if (tag.color == HighlightColor::Default && classes)
        tag.color = highlightColorFromClass(*classes);
    return tag;
}

QList<MarkPair> findMarkPairs(QStringView masked)
{
    QList<MarkPair> pairs;
    QList<MarkTag> open;
    for (qsizetype i = masked.indexOf(u'<'); i >= 0 && i < masked.size(); i = masked.indexOf(u'<', i)) {
        const auto tag = markTagAt(masked, i);
        if (!tag) {
            ++i;
            continue;
        }
        if (!tag->closing) {
            open.append(*tag);
        } else if (!open.isEmpty()) {
            pairs.append({open.takeLast(), *tag});
        }
        i = tag->to;
    }
    std::ranges::sort(pairs, [](const MarkPair &a, const MarkPair &b) { return a.open.from < b.open.from; });
    return pairs;
}

QString markOpenTag(HighlightColor color)
{
    if (color == HighlightColor::Default)
        return {};
    return u"<mark style=\"background: "_s + highlightWrittenColor(color) + u";\">"_s;
}

QList<HighlightSpan> findHighlights(QStringView text)
{
    const QString masked = maskInlineCode(text);
    QList<HighlightSpan> out;
    QList<Range> tags; // of the `<mark>` pairs: no `==` inside them
    for (const QList<MarkPair> pairs = findMarkPairs(masked); const MarkPair &p : pairs) {
        out.append({p.open.from, p.open.to, p.close.from, p.close.to, true, p.open.color});
        tags.append({p.open.from, p.open.to});
        tags.append({p.close.from, p.close.to});
    }
    // The tags never overlap: sorted, a cursor that only moves forward finds the one around `i`
    // (asked for in increasing order).
    std::ranges::sort(tags);
    qsizetype tag = 0;
    const auto inTag = [&](qsizetype i) {
        while (tag < tags.size() && tags.at(tag).second <= i)
            ++tag;
        return tag < tags.size() && tags.at(tag).first <= i;
    };
    // `==` runs (exactly two), outside mark tags.
    QList<qsizetype> openers;
    for (qsizetype i = masked.indexOf(u'='); i >= 0;) {
        qsizetype j = i + 1;
        while (j < masked.size() && masked[j] == u'=')
            ++j;
        if (j - i == 2 && !inTag(i) && (i == 0 || masked[i - 1] != u'\\')) {
            const auto [canOpen, canClose] = flanking(masked, i);
            if (canClose && !openers.isEmpty()) {
                const qsizetype o = openers.takeLast();
                out.append({o, o + 2, i, i + 2, false, HighlightColor::Default});
            } else if (canOpen) {
                openers.append(i);
            }
        }
        i = masked.indexOf(u'=', j);
    }
    std::ranges::sort(out, {}, &HighlightSpan::from);
    return out;
}

namespace {

/// `text` with [from, to) wrapped in the markers of `color` (Default: `==`, or `<mark>` where `==`
/// would not pair there — after a letter before punctuation, "a==(b)=="), the wrapped text selected.
HighlightEdit wrapped(const QString &text, qsizetype from, qsizetype to, HighlightColor color)
{
    const QStringView content = QStringView(text).sliced(from, to - from);
    const auto build = [&](QStringView open, QStringView close) {
        const qsizetype contentFrom = from + open.size();
        return HighlightEdit{text.left(from) + open + content + close + text.sliced(to), contentFrom,
                             contentFrom + content.size(), true};
    };
    if (color != HighlightColor::Default)
        return build(markOpenTag(color), u"</mark>");
    HighlightEdit eq = build(u"==", u"==");
    const auto pairs = [&eq](const HighlightSpan &s) {
        return !s.html && s.contentFrom == eq.from && s.contentTo == eq.to;
    };
    if (std::ranges::any_of(findHighlights(eq.text), pairs))
        return eq;
    return build(u"<mark>", u"</mark>");
}

} // namespace

HighlightEdit setHighlight(const QString &text, qsizetype from, qsizetype to, std::optional<HighlightColor> color)
{
    from = std::clamp<qsizetype>(from, 0, text.size());
    to = std::clamp<qsizetype>(to, 0, text.size());
    if (from > to)
        std::swap(from, to);
    const QList<HighlightSpan> spans = findHighlights(text);

    // The innermost highlight the selection is in (or exactly covers): the last one, `spans` being
    // ordered by `from`.
    const HighlightSpan *around = nullptr;
    for (const HighlightSpan &s : spans) {
        if ((from >= s.contentFrom && to <= s.contentTo) || (from == s.from && to == s.to))
            around = &s;
    }
    if (around) {
        const HighlightSpan &s = *around;
        if (color && s.color == *color && (s.html || *color == HighlightColor::Default))
            return {text, s.contentFrom, s.contentTo};
        // The markers removed, or replaced (recoloured); the text between them kept.
        const QStringView content = QStringView(text).sliced(s.contentFrom, s.contentTo - s.contentFrom);
        const QString unwrapped = text.left(s.from) + content + text.sliced(s.to);
        if (!color)
            return {unwrapped, s.from, s.from + content.size(), true};
        HighlightEdit edit = wrapped(unwrapped, s.from, s.from + content.size(), *color);
        edit.changed = edit.text != text;
        return edit;
    }
    HighlightEdit out{text, from, to, false};
    if (!color)
        return out;
    const QStringView selected = QStringView(text).sliced(from, to - from);
    if (selected.contains(u'\n') || selected.contains(u'\r')) {
        out.refused = true;
        return out;
    }
    while (from < to && text[from].isSpace())
        ++from;
    while (to > from && text[to - 1].isSpace())
        --to;
    // A highlight cut by the selection, or a caret inside a marker: refused. Highlights inside the
    // selection lose their markers (one colour for the whole text).
    QList<Range> markers;
    for (const HighlightSpan &s : spans) {
        const bool overlaps = s.from < to && from < s.to;
        const bool contained = s.from >= from && s.to <= to;
        if ((from == to && ((from > s.from && from < s.contentFrom) || (from > s.contentTo && from < s.to)))
            || (overlaps && !contained)) {
            out.refused = true;
            return out;
        }
        if (overlaps && contained) {
            markers.append({s.from, s.contentFrom});
            markers.append({s.contentTo, s.to});
        }
    }
    // The markers never overlap: removed from the last one back, the earlier positions hold.
    std::ranges::sort(markers);
    QString base = text;
    for (const auto &[markerFrom, markerTo] : std::as_const(markers) | std::views::reverse) {
        base.remove(markerFrom, markerTo - markerFrom);
        to -= markerTo - markerFrom;
    }
    return wrapped(base, from, to, *color);
}

} // namespace brulion::markdown
