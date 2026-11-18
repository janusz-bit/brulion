#include "markdown/renderedtext.h"

#include <algorithm>
#include <array>
#include <optional>

namespace brulion::markdown {

using namespace Qt::StringLiterals;

namespace {

using Tag = RenderedElement::Tag;

struct TagName {
    QStringView name;
    Tag tag;
};
constexpr std::array<TagName, 9> TagNames{{
        {u"b", Tag::Bold},
        {u"i", Tag::Italic},
        {u"s", Tag::Strike},
        {u"code", Tag::Code},
        {u"sup", Tag::Sup},
        {u"sub", Tag::Sub},
        {u"span", Tag::Span},
        {u"a", Tag::Anchor},
        {u"pre", Tag::Pre},
}};

QStringView tagName(Tag tag)
{
    for (const TagName &t : TagNames) {
        if (t.tag == tag)
            return t.name;
    }
    return u"span";
}

std::optional<Tag> tagFromName(QStringView name)
{
    for (const TagName &t : TagNames) {
        if (t.name == name)
            return t.tag;
    }
    return std::nullopt;
}

/// The attributes an element was written with (always in this order).
enum Attribute : quint8 { HrefAttribute = 1, NameAttribute = 2, StyleAttribute = 4 };

/// The default spelling of a character (`inPre`: inside <pre>, where a line break is itself).
QString defaultHtml(QChar c, bool inPre)
{
    switch (c.unicode()) {
    case u'&': return u"&amp;"_s;
    case u'<': return u"&lt;"_s;
    case u'>': return u"&gt;"_s;
    case u'"': return u"&quot;"_s;
    case u'\n': return inPre ? u"\n"_s : u"<br/>"_s;
    default: return QString(c);
    }
}

/// Decoded HTML attribute value or entity: the character an entity at `at` shows, `end` past its
/// ';'; 0 when there is none.
char32_t entityAt(QStringView html, qsizetype at, qsizetype &end)
{
    const qsizetype semicolon = html.sliced(at, std::min<qsizetype>(html.size() - at, 12)).indexOf(u';');
    if (semicolon < 2)
        return 0;
    const QStringView name = html.sliced(at + 1, semicolon - 1);
    struct Named {
        QStringView name;
        char32_t character;
    };
    static constexpr Named entities[] = {{u"amp", u'&'},   {u"lt", u'<'},    {u"gt", u'>'},
                                         {u"quot", u'"'},  {u"apos", u'\''}, {u"nbsp", 0xA0}};
    char32_t out = 0;
    if (const Named *e = std::ranges::find(entities, name, &Named::name); e != std::end(entities)) {
        out = e->character;
    } else if (name.startsWith(u'#')) {
        bool ok = false;
        const bool hex = name.size() > 1 && (name.at(1) == u'x' || name.at(1) == u'X');
        const uint code = (hex ? name.sliced(2) : name.sliced(1)).toUInt(&ok, hex ? 16 : 10);
        if (ok && code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF))
            out = code;
    }
    if (out)
        end = at + semicolon + 1;
    return out;
}

QString decodeAttribute(QStringView raw)
{
    QString out;
    out.reserve(raw.size());
    for (qsizetype i = 0; i < raw.size();) {
        qsizetype end = i;
        if (raw[i] == u'&') {
            if (const char32_t c = entityAt(raw, i, end)) {
                out += QString::fromUcs4(&c, 1);
                i = end;
                continue;
            }
        }
        out += raw[i++];
    }
    return out;
}

/// Qt's rich text collapses white space (any but the no-break space) like HTML.
bool collapsible(QChar c)
{
    return c.isSpace() && c.unicode() != 0xA0;
}

/// One CSS declaration list as the renderer writes it (`prop:value;prop:value`).
struct Css {
    QString color;
    QString background;
    bool decorationNone = false;
    bool underline = false;
    bool overline = false;
    bool monospace = false;
    bool sansSerif = false;
    bool smallCaps = false;
    bool unknown = false; ///< a declaration the item does not draw
};

Css parseCss(QStringView style)
{
    Css css;
    for (QStringView decl : style.split(u';', Qt::SkipEmptyParts)) {
        const qsizetype colon = decl.indexOf(u':');
        if (colon < 0) {
            css.unknown = true;
            continue;
        }
        const QStringView prop = decl.first(colon).trimmed();
        const QStringView value = decl.sliced(colon + 1).trimmed();
        if (prop == u"color") {
            css.color = value.toString();
        } else if (prop == u"background-color") {
            css.background = value.toString();
        } else if (prop == u"text-decoration") {
            css.decorationNone = value == u"none";
            css.underline = value == u"underline";
            css.overline = value == u"overline";
            if (!css.decorationNone && !css.underline && !css.overline)
                css.unknown = true;
        } else if (prop == u"font-family") {
            css.monospace = value == u"monospace";
            css.sansSerif = value == u"sans-serif";
            if (!css.monospace && !css.sansSerif)
                css.unknown = true;
        } else if (prop == u"font-variant" && value == u"small-caps") {
            css.smallCaps = true;
        } else {
            css.unknown = true;
        }
    }
    return css;
}

class Reader {
public:
    Reader(QStringView html, const QList<HtmlSourceAnchor> &anchors) : m_html(html), m_anchors(anchors)
    {
        if (!anchors.isEmpty())
            m_displayAt.resize(html.size() + 1, 0);
    }

    RenderedText read()
    {
        const qsizetype n = m_html.size();
        qsizetype i = 0;
        while (i < n) {
            mark(i);
            const QChar c = m_html[i];
            if (c == u'<') {
                const qsizetype end = tagEnd(i);
                if (end < 0) {
                    m_out.valid = false;
                    character(c, u"<");
                    ++i;
                    continue;
                }
                tag(m_html.sliced(i + 1, end - i - 1), m_html.sliced(i, end + 1 - i));
                markRange(i + 1, end + 1);
                i = end + 1;
                continue;
            }
            if (c == u'&') {
                qsizetype end = i;
                if (const char32_t e = entityAt(m_html, i, end)) {
                    const QStringView written = m_html.sliced(i, end - i);
                    if (QChar::requiresSurrogates(e)) {
                        const QChar pair[2] = {QChar(QChar::highSurrogate(e)), QChar(QChar::lowSurrogate(e))};
                        characters(QStringView(pair, 2), written);
                    } else {
                        character(QChar(char16_t(e)), written);
                    }
                    markRange(i + 1, end);
                    i = end;
                    continue;
                }
            }
            if (c.isHighSurrogate() && i + 1 < n && m_html[i + 1].isLowSurrogate()) {
                characters(m_html.sliced(i, 2), m_html.sliced(i, 2));
                markRange(i + 1, i + 2);
                i += 2;
                continue;
            }
            character(c, m_html.sliced(i, 1));
            ++i;
        }
        mark(n);
        if (!m_stack.isEmpty())
            m_out.valid = false;
        for (const qsizetype k : std::as_const(m_stack))
            m_out.elements[k].to = m_out.text.size();
        finish();
        return std::move(m_out);
    }

private:
    void mark(qsizetype at)
    {
        if (!m_displayAt.isEmpty())
            m_displayAt[at] = m_out.text.size();
    }
    /// Offsets inside a tag or an entity: the display position before it.
    void markRange(qsizetype from, qsizetype to)
    {
        if (m_displayAt.isEmpty())
            return;
        const qsizetype before = m_displayAt[from - 1];
        for (qsizetype k = from; k < to; ++k)
            m_displayAt[k] = before;
    }

    /// The '>' of the tag starting at `at` (quotes respected), −1 when there is none.
    qsizetype tagEnd(qsizetype at) const
    {
        QChar quote;
        for (qsizetype k = at + 1; k < m_html.size(); ++k) {
            const QChar t = m_html[k];
            if (!quote.isNull()) {
                if (t == quote)
                    quote = QChar();
            } else if (t == u'"') {
                quote = t;
            } else if (t == u'>') {
                return k;
            } else if (t == u'<') {
                return -1;
            }
        }
        return -1;
    }

    void spell(qsizetype at, qsizetype length, QStringView html)
    {
        // Empty spellings of one place and moment are one.
        if (length == 0 && !m_out.spellings.isEmpty()) {
            RenderedSpelling &last = m_out.spellings.last();
            if (last.length == 0 && last.at == at && last.events == m_events) {
                last.html += html;
                return;
            }
        }
        m_out.spellings.append({at, length, html.toString(), m_events});
    }

    void character(QChar c, QStringView written) { characters(QStringView(&c, 1), written); }

    void characters(QStringView chars, QStringView written)
    {
        const qsizetype at = m_out.text.size();
        if (m_pre == 0 && chars.size() == 1 && collapsible(chars[0])) {
            if (m_lineStart || m_lastSpace) {
                spell(at, 0, written);
                return;
            }
            m_out.text += u' ';
            if (written != u" ")
                spell(at, 1, written);
            m_lastSpace = true;
            return;
        }
        m_out.text += chars;
        QString expected;
        for (const QChar c : chars)
            expected += defaultHtml(c, m_pre > 0);
        if (expected != written)
            spell(at, chars.size(), written);
        m_lastSpace = false;
        m_lineStart = false;
    }

    void tag(QStringView inner, QStringView written)
    {
        if (inner == u"br/") {
            m_out.text += u'\n';
            if (m_pre > 0)
                spell(m_out.text.size() - 1, 1, written);
            m_lineStart = true;
            m_lastSpace = false;
            return;
        }
        if (inner == u"hr/") {
            m_out.objects.append({RenderedObject::Kind::Rule, m_out.text.size(), m_out.text.size(), {}, {}});
            spell(m_out.text.size(), 0, written);
            m_out.drawable = false;
            m_lineStart = true;
            m_lastSpace = false;
            return;
        }
        if (inner.startsWith(u'/')) {
            const auto t = tagFromName(inner.sliced(1));
            if (!t || m_stack.isEmpty() || m_out.elements[m_stack.last()].tag != *t) {
                m_out.valid = false;
                spell(m_out.text.size(), 0, written);
                return;
            }
            RenderedElement &e = m_out.elements[m_stack.takeLast()];
            e.to = m_out.text.size();
            if (e.tag == Tag::Pre)
                --m_pre;
            ++m_events;
            return;
        }
        const qsizetype space = inner.indexOf(u' ');
        const auto t = tagFromName(space < 0 ? inner : inner.first(space));
        if (!t) {
            m_out.valid = false;
            spell(m_out.text.size(), 0, written);
            return;
        }
        RenderedElement e;
        e.tag = *t;
        e.depth = int(m_stack.size());
        e.from = m_out.text.size();
        e.to = e.from;
        quint8 attributes = 0;
        qsizetype k = space < 0 ? inner.size() : space;
        while (k < inner.size()) {
            // ` key="value"`, in the order href, name, style.
            if (inner[k] != u' ') {
                m_out.valid = false;
                break;
            }
            const qsizetype eq = inner.indexOf(u"=\"", k + 1);
            const qsizetype close = eq < 0 ? -1 : inner.indexOf(u'"', eq + 2);
            if (close < 0) {
                m_out.valid = false;
                break;
            }
            const QStringView key = inner.sliced(k + 1, eq - k - 1);
            const QStringView raw = inner.sliced(eq + 2, close - eq - 2);
            const quint8 bit = key == u"href" ? HrefAttribute : key == u"name" ? NameAttribute
                             : key == u"style"                                 ? StyleAttribute
                                                                               : 0;
            if (bit == 0 || attributes >= bit)
                m_out.valid = false;
            attributes |= bit;
            if (bit == StyleAttribute) {
                e.style = raw.toString();
            } else if (bit != 0) {
                QString value = decodeAttribute(raw);
                if (value.toHtmlEscaped() != raw)
                    m_out.valid = false;
                (bit == HrefAttribute ? e.href : e.name) = std::move(value);
            }
            k = close + 1;
        }
        if (attributes != (attributes & (e.tag == Tag::Anchor ? 7 : 4)))
            m_out.valid = false;
        m_attributes.append(attributes);
        if (e.tag == Tag::Pre) {
            ++m_pre;
            m_out.drawable = false;
        }
        m_stack.append(m_out.elements.size());
        m_out.elements.append(std::move(e));
        ++m_events;
    }

    void finish()
    {
        flatten();
        if (!m_out.valid)
            m_out.drawable = false;
        // An element's attributes: kept for runsToHtml() (href="" differs from none).
        m_out.attributes = std::move(m_attributes);
        if (m_anchors.isEmpty())
            return;
        for (const HtmlSourceAnchor &a : m_anchors) {
            if (a.htmlFrom < 0 || a.htmlTo > m_html.size() || a.htmlFrom >= a.htmlTo)
                continue;
            const qsizetype from = m_displayAt[a.htmlFrom];
            const qsizetype to = m_displayAt[a.htmlTo];
            if (to > from)
                m_out.source.append({from, to, a.sourceFrom, a.sourceTo});
        }
        std::ranges::sort(m_out.source, {}, &RenderedSourceSegment::displayFrom);
    }

    void flatten()
    {
        const QList<RenderedElement> &els = m_out.elements;
        QList<qsizetype> bounds{0, m_out.text.size()};
        for (const RenderedElement &e : els) {
            bounds.append(e.from);
            bounds.append(e.to);
            if (e.tag == Tag::Anchor && e.name.startsWith(u"brulion-icon:")) {
                const Css css = parseCss(e.style);
                m_out.objects.append({RenderedObject::Kind::Icon, e.from, e.to,
                                      e.name.sliced(QStringView(u"brulion-icon:").size()), css.color});
            }
        }
        std::ranges::sort(bounds);
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
        std::ranges::sort(m_out.objects, {}, &RenderedObject::from);
        QList<qsizetype> active; // in nesting order
        qsizetype next = 0;      // the next element (pre-order) not yet opened
        for (qsizetype b = 0; b + 1 < bounds.size(); ++b) {
            const qsizetype from = bounds[b];
            const qsizetype to = bounds[b + 1];
            active.removeIf([&](qsizetype k) { return els[k].to <= from; });
            while (next < els.size() && els[next].from <= from) {
                if (els[next].to > from)
                    active.append(next);
                ++next;
            }
            RenderedRun run;
            run.from = from;
            run.to = to;
            bool foreground = false;
            for (const qsizetype k : std::as_const(active)) {
                const RenderedElement &e = els[k];
                const Css css = parseCss(e.style);
                if (css.unknown || css.overline || css.monospace || css.sansSerif || css.smallCaps)
                    m_out.drawable = false;
                switch (e.tag) {
                case Tag::Bold: run.style |= RenderedRun::Bold; break;
                case Tag::Italic: run.style |= RenderedRun::Italic; break;
                case Tag::Strike: run.style |= RenderedRun::Strike; break;
                case Tag::Code: run.style |= RenderedRun::Code; break;
                case Tag::Sup: run.style |= RenderedRun::Sup; break;
                case Tag::Sub: run.style |= RenderedRun::Sub; break;
                case Tag::Anchor:
                    run.href = e.href;
                    if (e.name.startsWith(u"brulion-icon:"))
                        run.icon = e.name.sliced(QStringView(u"brulion-icon:").size());
                    break;
                case Tag::Span:
                case Tag::Pre: break;
                }
                if (css.underline)
                    run.style |= RenderedRun::Underline;
                if (css.overline)
                    run.style |= RenderedRun::Overline;
                if (css.monospace)
                    run.style |= RenderedRun::Code;
                if (css.sansSerif)
                    run.style |= RenderedRun::SansSerif;
                if (css.smallCaps)
                    run.style |= RenderedRun::SmallCaps;
                if (!css.color.isEmpty()) {
                    run.foreground = css.color;
                    foreground = true;
                }
                if (!css.background.isEmpty()) {
                    run.backgroundColor = css.background;
                    run.tintElement = k;
                }
            }
            if (!run.backgroundColor.isEmpty()) {
                // RichTextPills' rule: a chip or a tag has its own text colour, inline code is
                // fixed-pitch, a highlight keeps the text's colour.
                const bool code = run.style & RenderedRun::Code;
                run.background = !code && foreground ? RenderedBackground::Pill : RenderedBackground::Box;
            }
            if (!run.icon.isEmpty())
                run.role = RenderedRole::Icon;
            else if (run.background == RenderedBackground::Pill)
                run.role = run.href.startsWith(u"tag:") ? RenderedRole::Tag : RenderedRole::Chip;
            else if (run.style & RenderedRun::Code)
                run.role = RenderedRole::Code;
            else if (run.background == RenderedBackground::Box)
                run.role = RenderedRole::Highlight;
            else if (!run.href.isEmpty())
                run.role = RenderedRole::Link;
            else if (foreground)
                run.role = RenderedRole::Muted;
            if (run.to > run.from)
                m_out.runs.append(std::move(run));
        }
        // Neighbours with one look are one run.
        QList<RenderedRun> merged;
        for (RenderedRun &r : m_out.runs) {
            if (!merged.isEmpty()) {
                RenderedRun &last = merged.last();
                RenderedRun probe = r;
                probe.from = last.from;
                probe.to = last.to;
                if (probe == last) {
                    last.to = r.to;
                    continue;
                }
            }
            merged.append(std::move(r));
        }
        m_out.runs = std::move(merged);
    }

    QStringView m_html;
    const QList<HtmlSourceAnchor> &m_anchors;
    QList<qsizetype> m_displayAt;
    RenderedText m_out;
    QList<qsizetype> m_stack;
    QList<quint8> m_attributes;
    qsizetype m_events = 0;
    int m_pre = 0;
    bool m_lineStart = true;
    bool m_lastSpace = false;
};

} // namespace

void TracedHtml::append(const TracedHtml &other, qsizetype sourceShift)
{
    const qsizetype base = html.size();
    html += other.html;
    anchors.reserve(anchors.size() + other.anchors.size());
    for (const HtmlSourceAnchor &a : other.anchors)
        anchors.append({a.htmlFrom + base, a.htmlTo + base, a.sourceFrom + sourceShift, a.sourceTo + sourceShift});
}

RenderedText renderedTextFromHtml(QStringView html, const QList<HtmlSourceAnchor> &anchors)
{
    return Reader(html, anchors).read();
}

QString runsToHtml(const RenderedText &t)
{
    QString out;
    out.reserve(t.text.size() * 2 + t.elements.size() * 24);
    qsizetype pos = 0;
    qsizetype events = 0;
    qsizetype sp = 0;
    int pre = 0;
    QList<qsizetype> stack;
    const auto flush = [&] {
        while (sp < t.spellings.size() && t.spellings[sp].length == 0 && t.spellings[sp].at == pos
               && t.spellings[sp].events == events)
            out += t.spellings[sp++].html;
    };
    const auto emitTo = [&](qsizetype target) {
        target = std::min(target, t.text.size());
        while (pos < target) {
            flush();
            if (sp < t.spellings.size() && t.spellings[sp].at == pos && t.spellings[sp].length > 0) {
                out += t.spellings[sp].html;
                pos += t.spellings[sp].length;
                ++sp;
                continue;
            }
            out += defaultHtml(t.text[pos], pre > 0);
            ++pos;
        }
        flush();
    };
    const auto close = [&] {
        const RenderedElement &e = t.elements[stack.last()];
        emitTo(e.to);
        out += u"</"_s + tagName(e.tag) + u'>';
        if (e.tag == Tag::Pre)
            --pre;
        ++events;
        stack.removeLast();
        flush();
    };
    for (qsizetype k = 0; k < t.elements.size(); ++k) {
        const RenderedElement &e = t.elements[k];
        while (stack.size() > e.depth)
            close();
        emitTo(e.from);
        const quint8 attributes = k < t.attributes.size() ? t.attributes[k]
                                                         : quint8((e.href.isEmpty() ? 0 : HrefAttribute)
                                                                  | (e.name.isEmpty() ? 0 : NameAttribute)
                                                                  | (e.style.isEmpty() ? 0 : StyleAttribute));
        out += u'<' + tagName(e.tag);
        if (attributes & HrefAttribute)
            out += u" href=\""_s + e.href.toHtmlEscaped() + u'"';
        if (attributes & NameAttribute)
            out += u" name=\""_s + e.name.toHtmlEscaped() + u'"';
        if (attributes & StyleAttribute)
            out += u" style=\""_s + e.style + u'"';
        out += u'>';
        if (e.tag == Tag::Pre)
            ++pre;
        ++events;
        stack.append(k);
        flush();
    }
    while (!stack.isEmpty())
        close();
    emitTo(t.text.size());
    while (sp < t.spellings.size())
        out += t.spellings[sp++].html;
    return out;
}

qsizetype RenderedText::sourceAt(qsizetype display) const
{
    if (source.isEmpty())
        return -1;
    qsizetype best = source.first().sourceFrom;
    for (const RenderedSourceSegment &s : source) {
        if (display < s.displayFrom)
            break;
        if (display >= s.displayTo) {
            best = s.sourceTo;
            continue;
        }
        if (s.linear())
            return s.sourceFrom + (display - s.displayFrom);
        return s.sourceFrom;
    }
    return best;
}

qsizetype RenderedText::displayAt(qsizetype src) const
{
    if (source.isEmpty())
        return -1;
    qsizetype best = -1;
    for (const RenderedSourceSegment &s : source) {
        if (src >= s.sourceFrom && src < s.sourceTo)
            return s.linear() ? s.displayFrom + (src - s.sourceFrom) : s.displayFrom;
        if (src == s.sourceTo)
            best = s.displayTo;
        else if (best < 0 && src < s.sourceFrom)
            best = s.displayFrom;
    }
    return best < 0 ? text.size() : best;
}

std::pair<qsizetype, qsizetype> RenderedText::displayRange(qsizetype from, qsizetype to) const
{
    qsizetype a = -1;
    qsizetype b = -1;
    for (const RenderedSourceSegment &s : source) {
        if (s.sourceTo <= from || s.sourceFrom >= to) {
            if (!(from == to && s.sourceFrom <= from && from < s.sourceTo))
                continue;
        }
        qsizetype da = s.displayFrom;
        qsizetype db = s.displayTo;
        if (s.linear()) {
            da = s.displayFrom + std::max<qsizetype>(0, from - s.sourceFrom);
            db = s.displayFrom + std::min(s.sourceTo, to) - s.sourceFrom;
            db = std::max(db, da);
        }
        a = a < 0 ? da : std::min(a, da);
        b = std::max(b, db);
    }
    return {a, b};
}

const RenderedRun *RenderedText::runAt(qsizetype display) const
{
    const auto it = std::ranges::upper_bound(runs, display, {}, &RenderedRun::from);
    if (it == runs.begin())
        return nullptr;
    const RenderedRun &r = *std::prev(it);
    return display < r.to ? &r : nullptr;
}

} // namespace brulion::markdown
