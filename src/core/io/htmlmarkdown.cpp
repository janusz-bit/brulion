#include "io/htmlmarkdown.h"

#include "io/htmlutil_p.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

#include <algorithm>

namespace brulion::io {

using namespace Qt::StringLiterals;

namespace {

// Private-use characters carrying information through QTextDocument.
constexpr char16_t UncheckedBox = 0xE000;
constexpr char16_t CheckedBox = 0xE001;
constexpr char16_t LangOpen = 0xE002;
constexpr char16_t LangClose = 0xE003;
/// First character of the number prefix that marks the list formats of the parts of a list
/// split by ListSplitter (followed by a nonce and the number of the split list).
constexpr char16_t ListPartMarker = 0xE004;
const QString ImageScheme = u"brulion-img:"_s;
/// Lists with more items are split before QTextDocument parses them (see splitLongLists()).
constexpr int MaxListItems = 50;
/// Right margin given to every <blockquote>; QTextDocument adds the margins of nested quotes.
constexpr int QuoteMargin = 37;

/// QTextDocument that never loads a resource (no local files, no network).
class SafeDocument : public QTextDocument {
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

// --- pre-processing ----------------------------------------------------------------------------

QString decodeEntities(const QString &s)
{
    if (!s.contains(u'&'))
        return s;
    static const QRegularExpression re(u"&(#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|amp|lt|gt|quot|apos|nbsp);"_s);
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(s).sliced(last, m.capturedStart() - last);
        const QString e = m.captured(1);
        if (e.startsWith(u'#')) {
            const bool hex = e.size() > 1 && (e[1] == u'x' || e[1] == u'X');
            bool ok = false;
            const uint cp = e.sliced(hex ? 2 : 1).toUInt(&ok, hex ? 16 : 10);
            if (ok && cp > 0 && cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF)) {
                const char32_t c = cp;
                out += QString::fromUcs4(&c, 1);
            }
        } else {
            static const QHash<QString, QString> named = {{u"amp"_s, u"&"_s},  {u"lt"_s, u"<"_s},
                                                          {u"gt"_s, u">"_s},   {u"quot"_s, u"\""_s},
                                                          {u"apos"_s, u"'"_s}, {u"nbsp"_s, u"\u00A0"_s}};
            out += named.value(e);
        }
        last = m.capturedEnd();
    }
    out += QStringView(s).sliced(last);
    return out;
}

/// Value of an attribute in the attribute text of a tag (entities decoded; "" for a name without
/// value), or nullopt. Attributes are read in order, so a name inside another attribute's quoted
/// value (`alt="x src=y"`) is not taken for an attribute; the first occurrence wins, as in
/// browsers.
std::optional<QString> attribute(const QString &attrs, const QString &name)
{
    static const QRegularExpression re(u"([^\\s\"'>/=]+)(?:\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+)))?"_s);
    for (auto it = re.globalMatch(attrs); it.hasNext();) {
        const auto m = it.next();
        if (m.captured(1).compare(name, Qt::CaseInsensitive) != 0)
            continue;
        for (int g = 2; g <= 4; ++g) {
            if (m.capturedLength(g) > 0)
                return decodeEntities(m.captured(g));
        }
        return QString();
    }
    return std::nullopt;
}

template<typename F>
QString replaceAll(const QString &s, const QRegularExpression &re, F &&replacement)
{
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(s).sliced(last, m.capturedStart() - last);
        out += replacement(m);
        last = m.capturedEnd();
    }
    if (last == 0)
        return s;
    out += QStringView(s).sliced(last);
    return out;
}

/// Is `c` part of a tag name (`<img`, `<my-element`)?
bool isTagNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'-' || c == u':' || c == u'_';
}

/// Index of the '>' ending the tag whose attributes begin at `from`: quoted values may contain
/// '>'; a quote that is never closed ends at the first '>'. -1 when the tag never ends.
qsizetype startTagEnd(const QString &html, qsizetype from)
{
    QChar quote;
    for (qsizetype i = from; i < html.size(); ++i) {
        const QChar c = html[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
        } else if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'>') {
            return i;
        }
    }
    return quote.isNull() ? -1 : html.indexOf(u'>', from);
}

/// Replace every start tag `<name …>` / `<name …/>` by `fn(attributeText)`. Quoted attribute
/// values may contain '>' (`alt="a > b"`, inline SVG data URIs); a quote that is never closed
/// does not swallow the rest: the first '>' ends that tag, as for an unquoted value. Linear time
/// (no regular expression rescanning the document from every '<').
template<typename F>
QString replaceTags(const QString &html, QStringView name, F &&fn)
{
    QString out;
    qsizetype last = 0;
    qsizetype pos = 0;
    const QString open = u'<' + name.toString();
    while (pos < html.size()) {
        const qsizetype start = html.indexOf(open, pos, Qt::CaseInsensitive);
        if (start < 0)
            break;
        const qsizetype attrs = start + open.size();
        if (attrs < html.size() && isTagNameChar(html[attrs])) {
            pos = attrs; // "<imgx", not "<img"
            continue;
        }
        const qsizetype end = startTagEnd(html, attrs);
        if (end < 0)
            break; // no tag can end any more
        QStringView text = QStringView(html).sliced(attrs, end - attrs);
        if (text.endsWith(u'/'))
            text.chop(1);
        out += QStringView(html).sliced(last, start - last);
        out += fn(text.toString());
        last = end + 1;
        pos = end + 1;
    }
    if (last == 0)
        return html;
    out += QStringView(html).sliced(last);
    return out;
}

struct Prepared {
    QString html;
    QList<HtmlImage> images;
};

Prepared prepare(const QString &html)
{
    Prepared p;
    static const QRegularExpression markClose(u"</mark\\s*>"_s, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression preCode(u"<pre\\b[^>]*>\\s*<code\\b([^>]*)>"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression language(u"\\b(?:language|lang)-([A-Za-z0-9_+#.-]+)"_s);

    QString s = html;
    // The marker characters must only ever come from us (icon fonts use private-use code points).
    static_assert(CheckedBox == UncheckedBox + 1 && LangOpen == UncheckedBox + 2 && LangClose == UncheckedBox + 3);
    s.removeIf([](QChar c) { return c.unicode() >= UncheckedBox && c.unicode() <= LangClose; });
    s = detail::removeElements(s, {u"script"_s, u"style"_s, u"noscript"_s, u"template"_s, u"iframe"_s, u"object"_s,
                                   u"embed"_s, u"svg"_s, u"math"_s, u"canvas"_s, u"video"_s, u"audio"_s});
    s = detail::limitNesting(s);
    for (const QStringView dropped : {u"link", u"meta", u"base", u"script", u"style", u"iframe", u"object", u"embed",
                                      u"source", u"track", u"param"})
        s = replaceTags(s, dropped, [](const QString &) { return QString(); });
    s = replaceTags(s, u"input", [](const QString &attrs) -> QString {
        if (attribute(attrs, u"type"_s).value_or(QString()).compare(u"checkbox"_s, Qt::CaseInsensitive) != 0)
            return QString();
        return QString(QChar(attribute(attrs, u"checked"_s) ? CheckedBox : UncheckedBox)) + u' ';
    });
    s = replaceTags(s, u"img", [&](const QString &attrs) -> QString {
        p.images.append(HtmlImage{attribute(attrs, u"src"_s).value_or(QString()).trimmed(),
                                  attribute(attrs, u"alt"_s).value_or(QString()).trimmed()});
        return u"<img src=\""_s + ImageScheme + QString::number(p.images.size() - 1) + u"\">"_s;
    });
    // Quote levels are read back from the accumulated right margin (see quoteLevel()).
    const QString quoteTag = u"<blockquote style=\"margin-right:"_s + QString::number(QuoteMargin) + u"px\">"_s;
    s = replaceTags(s, u"blockquote", [&](const QString &) { return quoteTag; });
    s = replaceTags(s, u"mark", [](const QString &) { return u"<span style=\"background-color:#fff59d\">"_s; });
    s.replace(markClose, u"</span>"_s);
    s = replaceAll(s, preCode, [](const QRegularExpressionMatch &m) -> QString {
        const QString cls = attribute(m.captured(1), u"class"_s).value_or(QString());
        const auto lm = language.match(cls);
        QString out = u"<pre><code>"_s;
        if (lm.hasMatch())
            out += QChar(LangOpen) + lm.captured(1) + QChar(LangClose);
        return out;
    });
    p.html = s;
    return p;
}

// --- long lists --------------------------------------------------------------------------------
//
// QTextDocument (Qt 6.11) needs time quadratic in the number of items of one list: every item
// added marks all blocks of its list dirty (QTextBlockGroupPrivate::markBlocksDirty) and
// QTextList::itemNumber() is a linear search. Long lists are therefore split into consecutive
// lists before parsing. To split exactly where QTextDocument would see a new item of the same
// list, ListSplitter follows the tokenizer and the tree building of QTextHtmlParser (qtbase
// src/gui/text/qtexthtmlparser.cpp: parse(), parseTag(), parseCloseTag(), parseWord(),
// parseAttributes(), resolveParent()); CSS cannot change the structure there (displayMode comes
// only from the element table). Whatever it does not follow exactly makes it leave the HTML
// alone.

enum class Element : quint8 {
    Other,
    P,
    Li,
    Ul,
    Ol,
    Td,
    Th,
    Tr,
    Table,
    Thead,
    Tbody,
    Tfoot,
    Caption,
    Dd,
    Dt,
    Dl,
    Body,
    Head
};

struct ElementInfo {
    Element element = Element::Other;
    bool block = false; ///< QTextHtmlElement::DisplayBlock
    bool none = false;  ///< QTextHtmlElement::DisplayNone
    bool empty = false; ///< QTextHtmlParserNode::mayNotHaveChildren()
};

/// QTextHtmlParser's element table (only what decides the structure).
ElementInfo elementInfo(const QString &name)
{
    using E = Element;
    static const QHash<QString, ElementInfo> table = {
            {u"p"_s, {E::P, true}},
            {u"li"_s, {E::Li, true}},
            {u"ul"_s, {E::Ul, true}},
            {u"ol"_s, {E::Ol, true}},
            {u"td"_s, {E::Td, true}},
            {u"th"_s, {E::Th, true}},
            {u"tr"_s, {E::Tr}},
            {u"table"_s, {E::Table}},
            {u"thead"_s, {E::Thead}},
            {u"tbody"_s, {E::Tbody}},
            {u"tfoot"_s, {E::Tfoot}},
            {u"caption"_s, {E::Caption, true}},
            {u"dd"_s, {E::Dd, true}},
            {u"dt"_s, {E::Dt, true}},
            {u"dl"_s, {E::Dl, true}},
            {u"body"_s, {E::Body, true}},
            {u"qt"_s, {E::Body, true}},
            {u"head"_s, {E::Head, false, true}},
            {u"blockquote"_s, {E::Other, true}},
            {u"center"_s, {E::Other, true}},
            {u"div"_s, {E::Other, true}},
            {u"h1"_s, {E::Other, true}},
            {u"h2"_s, {E::Other, true}},
            {u"h3"_s, {E::Other, true}},
            {u"h4"_s, {E::Other, true}},
            {u"h5"_s, {E::Other, true}},
            {u"h6"_s, {E::Other, true}},
            {u"pre"_s, {E::Other, true}},
            {u"hr"_s, {E::Other, true, false, true}},
            {u"img"_s, {E::Other, false, false, true}},
            {u"br"_s, {E::Other, false, false, true}},
            {u"meta"_s, {E::Other, false, true, true}},
            {u"link"_s, {E::Other, false, true}},
            {u"script"_s, {E::Other, false, true}},
            {u"style"_s, {E::Other, false, true}},
            {u"title"_s, {E::Other, false, true}},
    };
    return table.value(name);
}

/// QTextHtmlParserNode::allowedInContext()
bool allowedIn(Element element, Element parent)
{
    using E = Element;
    switch (element) {
    case E::Dd:
    case E::Dt: return parent == E::Dl;
    case E::Tr: return parent == E::Table || parent == E::Thead || parent == E::Tbody || parent == E::Tfoot;
    case E::Th:
    case E::Td: return parent == E::Tr;
    case E::Thead:
    case E::Tbody:
    case E::Tfoot:
    case E::Caption: return parent == E::Table;
    case E::Body: return parent != E::Head;
    default: return true;
    }
}

class ListSplitter {
public:
    explicit ListSplitter(const QString &html) : m_txt(html), m_len(html.size()) { }

    struct Result {
        QString html;
        int parts = 0;  ///< number of lists split
        QString prefix; ///< number prefix of the parts' list formats, followed by the list's number
    };

    /// The HTML with every list of more than `maxItems` items split.
    Result split(int maxItems)
    {
        Result r{m_txt, 0, {}};
        // <!--StartFragment-->: Qt parses only the fragment; qrichtext: every node keeps white space.
        if (maxItems < 1 || !mayHaveLongList(maxItems) || m_txt.contains(u"<!--StartFragment-->"_s)
            || m_txt.contains(u"qrichtext"_s, Qt::CaseInsensitive))
            return r;
        if (!scan())
            return r;
        struct Insert {
            qsizetype from;
            qsizetype to; ///< replaced range [from, to)
            QString text;
        };
        QList<Insert> inserts;
        // The nonce keeps a document from posing as a part of a split list (`-qt-list-number-prefix`
        // in its own style attributes); it is a hash of the document, so the result is reproducible.
        r.prefix = QChar(ListPartMarker) + QString::number(qHash(QStringView(m_txt), 0), 16) + u':';
        for (const List &l : std::as_const(m_lists)) {
            if (!l.clean || l.ownText || l.keepsWhiteSpace || l.items.size() <= maxItems)
                continue;
            const QString name = l.ordered ? u"ol"_s : u"ul"_s;
            const QStringView attrs = QStringView(m_txt).sliced(l.attrsStart, l.attrsEnd - l.attrsStart);
            const QString marker =
                    u" style=\"-qt-list-number-prefix:'"_s + r.prefix + QString::number(r.parts) + u"';"_s;
            ++r.parts;
            inserts.append(Insert{l.tagStart, l.tagEnd, u'<' + name + attrs + marker + u" margin-bottom:0px\">"_s});
            for (qsizetype i = maxItems; i < l.items.size(); i += maxItems) {
                const bool last = i + maxItems >= l.items.size();
                const qsizetype at = l.items[i];
                inserts.append(Insert{at, at,
                                      u"</"_s + name + u"><"_s + name + attrs + marker + u" margin-top:0px"_s
                                              + (last ? u"\">"_s : u"; margin-bottom:0px\">"_s)});
            }
        }
        if (inserts.isEmpty())
            return r;
        std::sort(inserts.begin(), inserts.end(), [](const Insert &a, const Insert &b) { return a.from < b.from; });
        QString out;
        qsizetype last = 0;
        for (const Insert &in : std::as_const(inserts)) {
            out += QStringView(m_txt).sliced(last, in.from - last);
            out += in.text;
            last = in.to;
        }
        out += QStringView(m_txt).sliced(last);
        r.html = std::move(out);
        return r;
    }

private:
    struct Open {
        QString name; ///< tag as QTextHtmlParser keeps it (lower case)
        Element element = Element::Other;
        bool block = false;
        bool none = false;
        bool pre = false; ///< white-space may be pre / pre-wrap (inherited)
        int list = -1;    ///< index into m_lists for <ul>/<ol>
    };
    struct List {
        bool ordered = false;
        bool clean = false;   ///< the start tag can be written again with more attributes
        bool ownText = false; ///< text right after the start tag: the list gets a block (with its margins)
        /// An element inside keeps white space (white-space: pre / pre-wrap): after it an empty
        /// block keeps QTextHtmlImporter's "preserve white space" state, which a new list resets.
        bool keepsWhiteSpace = false;
        qsizetype tagStart = 0;
        qsizetype attrsStart = 0;
        qsizetype attrsEnd = 0;
        qsizetype tagEnd = 0;
        QList<qsizetype> items; ///< offsets of the '<' of the <li> that are children of the list
    };

    /// Can any list have more than `maxItems` items? Counts "<li" (QTextHtmlParser allows
    /// spaces after '<'); cheap, so that ordinary documents are not scanned.
    bool mayHaveLongList(int maxItems) const
    {
        int count = 0;
        for (qsizetype i = m_txt.indexOf(u'<'); i >= 0; i = m_txt.indexOf(u'<', i + 1)) {
            qsizetype k = i + 1;
            while (k < m_len && m_txt[k].isSpace())
                ++k;
            if (k + 1 < m_len && (m_txt[k] == u'l' || m_txt[k] == u'L')
                && (m_txt[k + 1] == u'i' || m_txt[k + 1] == u'I') && ++count > maxItems)
                return true;
        }
        return false;
    }

    bool hasPrefix(QChar c, qsizetype lookahead = 0) const
    {
        return m_pos + lookahead < m_len && m_txt[m_pos + lookahead] == c;
    }

    void eatSpace()
    {
        while (m_pos < m_len && m_txt[m_pos].isSpace() && m_txt[m_pos] != QChar::ParagraphSeparator)
            ++m_pos;
    }

    /// QTextHtmlParser::parseWord(). Entities are not decoded (`entity` is set when the word has
    /// one); a decoded entity never ends a word there, so the word ends at the same place.
    QString parseWord(bool *entity)
    {
        QString word;
        if (hasPrefix(u'"')) {
            ++m_pos;
            while (m_pos < m_len) {
                const QChar c = m_txt[m_pos++];
                if (c == u'"')
                    break;
                *entity = *entity || c == u'&';
                word += c;
            }
        } else if (hasPrefix(u'\'')) {
            ++m_pos;
            while (m_pos < m_len) {
                const QChar c = m_txt[m_pos++];
                if (c == u'\'' && m_txt[m_pos - 2] != u'\\')
                    break;
                word += c;
            }
        } else {
            while (m_pos < m_len) {
                const QChar c = m_txt[m_pos++];
                if (c == u'>' || (c == u'/' && hasPrefix(u'>')) || c == u'<' || c == u'=' || c.isSpace()) {
                    --m_pos;
                    break;
                }
                *entity = *entity || c == u'&';
                word += c;
            }
        }
        return word;
    }

    /// QTextHtmlParser::parseAttributes(); true when a `style` attribute may set `white-space`
    /// (or an attribute name has an entity: it may be `style`).
    bool parseAttributes()
    {
        bool whiteSpace = false;
        while (m_pos < m_len) {
            eatSpace();
            if (hasPrefix(u'>') || hasPrefix(u'/'))
                break;
            bool keyEntity = false;
            const QString key = parseWord(&keyEntity);
            if (key.isEmpty())
                break;
            QString value;
            eatSpace();
            if (hasPrefix(u'=')) {
                ++m_pos;
                eatSpace();
                bool valueEntity = false;
                value = parseWord(&valueEntity);
            }
            if (keyEntity
                || (key.compare(u"style"_s, Qt::CaseInsensitive) == 0
                    && (value.contains(u"white-space"_s, Qt::CaseInsensitive)
                        || value.contains(u"whitespace"_s, Qt::CaseInsensitive) || value.contains(u'\\')
                        || value.contains(u'&'))))
                whiteSpace = true;
        }
        return whiteSpace;
    }

    /// Follow the document; false when the structure cannot be followed exactly.
    bool scan()
    {
        while (m_pos < m_len) {
            qsizetype lt = m_txt.indexOf(u'<', m_pos);
            if (lt < 0)
                lt = m_len;
            // Text goes to the node created last: a list's own text when nothing but comments
            // came after its start tag (QTextHtmlImporter then gives the list a block).
            if (m_textList >= 0) {
                for (qsizetype i = m_pos; i < lt; ++i) {
                    if (!m_txt[i].isSpace()) {
                        m_lists[m_textList].ownText = true;
                        break;
                    }
                }
            }
            if (lt == m_len)
                break;
            m_pos = lt + 1;
            if (!parseTag(lt))
                return false;
        }
        return true;
    }

    bool parseTag(qsizetype lt)
    {
        eatSpace();
        if (hasPrefix(u'!')) {
            ++m_pos;
            if (hasPrefix(u'-') && hasPrefix(u'-', 1)) {
                m_pos += 2;
                const qsizetype end = m_txt.indexOf(u"-->"_s, m_pos);
                m_pos = end >= 0 ? end + 3 : m_len;
            } else {
                skipPast(u'>');
            }
            return true;
        }
        m_textList = -1; // a node is created (comments create none)
        bool entity = false;
        if (hasPrefix(u'/')) {
            ++m_pos;
            const QString name = parseWord(&entity).toLower().trimmed();
            skipPast(u'>');
            // A void element's end tag can close the element opened after it (parseCloseTag()
            // looks at the node created before the last one): not followed here.
            if (entity || name == u"br" || name == u"img" || name == u"hr" || name == u"meta")
                return false;
            if (name.isEmpty())
                return true;
            for (qsizetype i = qsizetype(m_open.size()) - 1; i >= 0; --i) {
                if (m_open[i].name == name) {
                    m_open.resize(i);
                    break;
                }
            }
            return true;
        }
        const QString name = parseWord(&entity).toLower();
        if (entity)
            return false; // `<u&#108;>` is a <ul> for QTextHtmlParser
        const qsizetype attrsStart = m_pos;
        const bool whiteSpaceStyle = m_pos < m_len && m_txt[m_pos].isSpace() && parseAttributes();
        const qsizetype attrsEnd = m_pos;
        bool closed = false;
        while (m_pos < m_len && m_txt[m_pos] != u'>') {
            closed = closed || m_txt[m_pos] == u'/';
            ++m_pos;
        }
        const bool ended = m_pos < m_len;
        ++m_pos;
        if (name.isEmpty())
            return true; // a node without a tag never becomes a parent
        const ElementInfo info = elementInfo(name);
        const qsizetype parent = resolveParent(info.element);
        if (info.element == Element::Li && parent >= 0 && m_open[parent].list >= 0)
            m_lists[m_open[parent].list].items.append(lt);
        m_open.resize(parent + 1);
        const bool parentNone = parent >= 0 && m_open[parent].none;
        // <pre> is white-space: pre; <br> is pre-wrap but always inserts a line separator.
        const bool pre = (parent >= 0 && m_open[parent].pre) || name == u"pre" || whiteSpaceStyle;
        if (pre) {
            // Innermost first: the lists around a marked list are marked already (linear time).
            for (auto o = m_open.crbegin(); o != m_open.crend(); ++o) {
                if (o->list < 0)
                    continue;
                if (m_lists[o->list].keepsWhiteSpace)
                    break;
                m_lists[o->list].keepsWhiteSpace = true;
            }
        }
        Open node{name, info.element, info.block && !parentNone, info.none || parentNone, pre, -1};
        if (info.empty || closed) // closed again at once
            return true;
        if (info.element == Element::Ul || info.element == Element::Ol) {
            static const QRegularExpression plainAttributes(
                    u"\\A(?:[ \\t\\n\\r\\f]+[A-Za-z_:][-A-Za-z0-9_:.]*(?:[ \\t\\n\\r\\f]*=[ \\t\\n\\r\\f]*"
                    u"(?:\"[^\"]*\"|'[^'\\\\]*'|[A-Za-z0-9_.:#%+,;-]+))?)*[ \\t\\n\\r\\f]*\\z"_s);
            List l;
            l.ordered = info.element == Element::Ol;
            l.keepsWhiteSpace = pre;
            l.tagStart = lt;
            l.attrsStart = attrsStart;
            l.attrsEnd = attrsEnd;
            l.tagEnd = m_pos;
            // Everything up to '>' must be attributes QTextHtmlParser reads to the end, so that
            // attributes appended to a copy of the tag are read too.
            l.clean = ended
                      && plainAttributes.matchView(QStringView(m_txt).sliced(attrsStart, m_pos - 1 - attrsStart))
                                 .hasMatch();
            node.list = int(m_lists.size());
            m_textList = node.list;
            m_lists.append(l);
        }
        m_open.push_back(std::move(node));
        return true;
    }

    void skipPast(QChar c)
    {
        while (m_pos < m_len) {
            if (m_txt[m_pos++] == c)
                break;
        }
    }

    /// QTextHtmlParser::resolveParent(): index in m_open of the parent of a new `element`
    /// (-1: the root); may insert the <table>/<tr> QTextHtmlParser adds for a lone <td>/<tr>.
    qsizetype resolveParent(Element element)
    {
        qsizetype p = qsizetype(m_open.size()) - 1;
        const auto hasAncestor = [&](Element e) {
            for (qsizetype n = p; n >= 0; --n) {
                if (m_open[n].element == e)
                    return true;
            }
            return false;
        };
        // The inserted nodes keep the default display (inline) of QTextHtmlParserNode (and are
        // taken to keep white space like their parent).
        const bool pre = p >= 0 && m_open[p].pre;
        if (element == Element::Td && !hasAncestor(Element::Tr)) {
            m_open.push_back(Open{u"table"_s, Element::Table, false, false, pre});
            m_open.push_back(Open{u"tr"_s, Element::Tr, false, false, pre});
            p += 2;
        }
        if (element == Element::Tr && !hasAncestor(Element::Table)) {
            m_open.push_back(Open{u"table"_s, Element::Table, false, false, pre});
            p += 1;
        }
        if (element == Element::P) {
            qsizetype n = p;
            while (n >= 0 && !m_open[n].block)
                --n;
            if (n >= 0 && m_open[n].element == Element::P)
                p = n;
        }
        if ((element == Element::P || element == Element::Li) && p >= 0 && m_open[p].element == element)
            --p;
        while (p >= 0 && !allowedIn(element, m_open[p].element))
            --p;
        return p;
    }

    const QString &m_txt;
    const qsizetype m_len;
    qsizetype m_pos = 0;
    std::vector<Open> m_open; ///< the open elements, outermost first (the root is not included)
    QList<List> m_lists;
    int m_textList = -1; ///< the list whose start tag was the last tag (comments aside)
};

/// Number of the first item of each part of a list that ListSplitter split (`split`): the parts
/// continue the numbering of the list as QTextDocument counts its items.
QHash<const QTextList *, qint64> listPartStarts(const QTextDocument &doc, const ListSplitter::Result &split)
{
    QHash<int, qint64> next; // split list -> number of its next item
    QHash<const QTextList *, qint64> starts;
    QSet<const QTextList *> seen;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextList *list = b.textList();
        if (!list || seen.contains(list))
            continue;
        seen.insert(list);
        const QTextListFormat lf = list->format();
        const QString prefix = lf.numberPrefix();
        if (!prefix.startsWith(split.prefix))
            continue;
        bool ok = false;
        const int id = QStringView(prefix).sliced(split.prefix.size()).toInt(&ok);
        if (!ok || id < 0 || id >= split.parts)
            continue;
        const auto it = next.constFind(id);
        const qint64 start = it != next.cend() ? *it : (lf.hasProperty(QTextFormat::ListStart) ? lf.start() : 1);
        starts.insert(list, start);
        next.insert(id, start + list->count());
    }
    return starts;
}

// --- inline ------------------------------------------------------------------------------------

bool isMonospaceFamily(const QString &family)
{
    static const QStringList mono = {
            u"monospace"_s,       u"courier"_s,        u"courier new"_s,     u"consolas"_s,
            u"menlo"_s,           u"monaco"_s,         u"source code pro"_s, u"dejavu sans mono"_s,
            u"liberation mono"_s, u"lucida console"_s, u"fira code"_s,       u"fira mono"_s,
            u"jetbrains mono"_s,  u"sf mono"_s,        u"ubuntu mono"_s,     u"roboto mono"_s};
    return mono.contains(family.trimmed().toLower().remove(u'"').remove(u'\''));
}

struct Style {
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    bool highlight = false;
    QString href;

    bool operator==(const Style &) const = default;
};

Style styleOf(const QTextCharFormat &cf, bool boldImplied)
{
    Style s;
    s.bold = !boldImplied && cf.fontWeight() >= QFont::DemiBold;
    s.italic = cf.fontItalic();
    s.strike = cf.fontStrikeOut();
    s.code = cf.fontFixedPitch();
    if (!s.code) {
        const QStringList families = cf.fontFamilies().toStringList();
        s.code = std::any_of(families.begin(), families.end(), isMonospaceFamily);
    }
    const QBrush bg = cf.background();
    if (bg.style() != Qt::NoBrush) {
        const QColor c = bg.color();
        s.highlight = c.alpha() > 0 && c != QColor(Qt::white);
    }
    if (cf.isAnchor())
        s.href = cf.anchorHref();
    return s;
}

struct Run {
    Style style;
    QString text;
    int image = -1; ///< index into the prepared images (text is then empty)
};

QString encodeDestination(const QString &href)
{
    QString out;
    for (const QChar c : href) {
        switch (c.unicode()) {
        case u' ': out += u"%20"; break;
        case u'(': out += u"%28"; break;
        case u')': out += u"%29"; break;
        case u'<': out += u"%3C"; break;
        case u'>': out += u"%3E"; break;
        case u'\n':
        case u'\r':
        case u'\t': break;
        default: out += c;
        }
    }
    return out;
}

QString codeSpan(const QString &text)
{
    const qsizetype run = detail::longestRun(text, u'`');
    if (run == 0)
        return u'`' + text + u'`';
    const QString ticks(run + 1, u'`');
    return ticks + u' ' + text + u' ' + ticks;
}

class InlineWriter {
public:
    InlineWriter(const QList<HtmlImage> &images, const HtmlToMarkdownOptions &options)
        : m_images(images), m_options(options)
    { }

    /// Markdown of a block's content; `taskState` receives a leading checkbox (removed).
    QString block(const QTextBlock &block, bool boldImplied, std::optional<bool> *taskState = nullptr) const
    {
        QList<Run> runs = collect(block, boldImplied);
        if (taskState)
            *taskState = takeCheckbox(runs);
        QString out;
        for (qsizetype i = 0; i < runs.size();) {
            const QString href = runs[i].style.href;
            qsizetype j = i + 1;
            if (!href.isEmpty()) {
                while (j < runs.size() && runs[j].style.href == href)
                    ++j;
            }
            QString label;
            for (qsizetype k = i; k < j; ++k)
                appendCollapsed(label, render(runs[k], out.isEmpty() && label.isEmpty(), !href.isEmpty()));
            appendCollapsed(out, href.isEmpty() ? label : linkMarkdown(href, label));
            i = j;
        }
        return out;
    }

    /// Append without doubling a space where a dropped element (an image) left two.
    static void appendCollapsed(QString &out, const QString &piece)
    {
        if (piece.startsWith(u' ') && out.endsWith(u' '))
            out += QStringView(piece).sliced(1);
        else
            out += piece;
    }

private:
    QList<Run> collect(const QTextBlock &block, bool boldImplied) const
    {
        QList<Run> runs;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat cf = fragment.charFormat();
            const Style style = styleOf(cf, boldImplied);
            if (cf.isImageFormat()) {
                const QString name = cf.toImageFormat().name();
                int index = -1;
                if (name.startsWith(ImageScheme))
                    index = name.sliced(ImageScheme.size()).toInt();
                for (qsizetype k = 0; k < fragment.text().size(); ++k)
                    runs.append(Run{style, QString(), index});
                continue;
            }
            if (!runs.isEmpty() && runs.last().image < 0 && runs.last().style == style)
                runs.last().text += fragment.text();
            else
                runs.append(Run{style, fragment.text(), -1});
        }
        return runs;
    }

    static std::optional<bool> takeCheckbox(QList<Run> &runs)
    {
        for (Run &r : runs) {
            if (r.image >= 0)
                return std::nullopt;
            qsizetype i = 0;
            while (i < r.text.size() && r.text[i].isSpace())
                ++i;
            if (i == r.text.size())
                continue;
            const QChar c = r.text[i];
            if (c != QChar(UncheckedBox) && c != QChar(CheckedBox))
                return std::nullopt;
            qsizetype end = i + 1;
            while (end < r.text.size() && r.text[end] == u' ')
                ++end;
            r.text.remove(0, end);
            return c == QChar(CheckedBox);
        }
        return std::nullopt;
    }

    /// `linkLabel`: the run is part of a link's label (brackets escaped).
    QString render(const Run &run, bool atStart, bool linkLabel) const
    {
        if (run.image >= 0)
            return imageMarkdown(run.image);
        QString text = run.text;
        text.replace(QChar(UncheckedBox), u"[ ]"_s).replace(QChar(CheckedBox), u"[x]"_s);
        text.replace(QChar::Nbsp, u' ');
        const QStringList lines = text.split(QChar::LineSeparator);
        QStringList out;
        for (qsizetype k = 0; k < lines.size(); ++k)
            out.append(styled(run.style, lines[k], atStart || k > 0, linkLabel));
        return out.join(u'\n');
    }

    static QString styled(const Style &style, const QString &text, bool lineStart, bool linkLabel)
    {
        const QStringView trimmed = QStringView(text).trimmed();
        if (trimmed.isEmpty())
            return text.isEmpty() ? QString() : u" "_s;
        const qsizetype lead = text.indexOf(trimmed.front());
        const QString before = lead > 0 ? u" "_s : QString();
        const QString after = text.size() > lead + trimmed.size() ? u" "_s : QString();
        if (style.code)
            return before + codeSpan(trimmed.toString()) + after;
        QString open;
        QString close;
        if (style.bold) {
            open += u"**";
            close.prepend(u"**");
        }
        if (style.italic) {
            open += u'*';
            close.prepend(u'*');
        }
        if (style.strike) {
            open += u"~~";
            close.prepend(u"~~");
        }
        if (style.highlight) {
            open += u"==";
            close.prepend(u"==");
        }
        return before + open + escapeMarkdownText(trimmed, lineStart && before.isEmpty() && open.isEmpty(), linkLabel)
               + close + after;
    }

    QString imageMarkdown(int index) const
    {
        if (index < 0 || index >= m_images.size())
            return {};
        const HtmlImage &image = m_images[index];
        if (m_options.image)
            return m_options.image(image);
        const QString scheme = QUrl(image.src).scheme().toLower();
        if (image.src.isEmpty() || (!scheme.isEmpty() && scheme != u"http" && scheme != u"https"))
            return {};
        return u"!["_s + escapeMarkdownText(image.alt, false, true) + u"]("_s + encodeDestination(image.src) + u')';
    }

    QString linkMarkdown(const QString &href, const QString &label) const
    {
        if (m_options.link) {
            if (auto custom = m_options.link(href, label))
                return *custom;
        }
        const QString trimmedHref = href.trimmed();
        const QString scheme = QUrl(trimmedHref).scheme().toLower();
        if (trimmedHref.isEmpty() || trimmedHref.startsWith(u'#') || scheme == u"javascript" || scheme == u"vbscript"
            || scheme == u"data" || scheme == u"file")
            return label;
        const QString plainLabel = QString(label).remove(u'\\');
        if (label.trimmed().isEmpty() || plainLabel == trimmedHref
            || (scheme == u"mailto" && u"mailto:"_s + plainLabel == trimmedHref)) {
            return scheme.isEmpty() ? label : u'<' + encodeDestination(trimmedHref) + u'>';
        }
        return u'[' + label + u"]("_s + encodeDestination(trimmedHref) + u')';
    }

    const QList<HtmlImage> &m_images;
    const HtmlToMarkdownOptions &m_options;
};

// --- blocks ------------------------------------------------------------------------------------

/// Blockquote nesting of a block: QTextDocument marks only the first block of a quote with
/// BlockQuoteLevel, but accumulates the margins of all enclosing quotes.
int quoteLevel(const QTextBlockFormat &bf)
{
    const qreal right = bf.rightMargin();
    if (right < QuoteMargin)
        return 0;
    const int level = qRound(right / QuoteMargin);
    return qAbs(right - level * QuoteMargin) < 0.5 ? std::min(level, 8) : 0;
}

enum class Kind { Paragraph, Heading, List, Code, Table, Rule, Blank };

struct OutBlock {
    Kind kind = Kind::Paragraph;
    QStringList lines;
    int quote = 0;
    bool tightTop = false;    ///< no top margin (a <div> line)
    bool tightBottom = false; ///< no bottom margin
    bool codeEnd = false;     ///< a code block's last line had a bottom margin
    QString lang;             ///< code blocks: language of the fence
};

OutBlock outBlock(Kind kind, int quote, QStringList lines = {})
{
    OutBlock b;
    b.kind = kind;
    b.quote = quote;
    b.lines = std::move(lines);
    return b;
}

class BlockWriter {
public:
    BlockWriter(const QList<HtmlImage> &images, const HtmlToMarkdownOptions &options,
                QHash<const QTextList *, qint64> listStarts = {})
        : m_inline(images, options), m_listStarts(std::move(listStarts))
    { }

    void frame(QTextFrame *frame)
    {
        for (auto it = frame->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(child))
                    this->table(table);
                else
                    this->frame(child);
            } else if (it.currentBlock().isValid()) {
                block(it.currentBlock());
            }
        }
    }

    QString markdown() const
    {
        struct Line {
            QString text;
            bool code = false; ///< a line inside a code block: kept exactly as written
        };
        QList<Line> lines;
        const OutBlock *prev = nullptr;
        bool pendingBlank = false;
        for (const OutBlock &b : m_blocks) {
            if (b.kind == Kind::Blank) {
                pendingBlank = prev != nullptr;
                continue;
            }
            if (prev) {
                const bool tight = !pendingBlank
                                   && ((prev->kind == Kind::List && b.kind == Kind::List)
                                       || (prev->kind == Kind::Paragraph && b.kind == Kind::Paragraph
                                           && prev->tightBottom && b.tightTop && prev->quote == b.quote));
                if (!tight)
                    lines.append(Line{prev->quote > 0 && b.quote > 0 ? QString(std::min(prev->quote, b.quote), u'>')
                                                                     : QString()});
            }
            const QString prefix = b.quote > 0 ? QString(b.quote, u'>') + u' ' : QString();
            const bool code = b.kind == Kind::Code;
            QStringList blockLines = b.lines;
            if (code) {
                const QString f = detail::codeFenceFor(blockLines.join(u'\n'));
                blockLines.prepend(f + b.lang);
                blockLines.append(f);
            }
            for (qsizetype k = 0; k < blockLines.size(); ++k) {
                const QString &l = blockLines[k];
                lines.append(Line{l.isEmpty() && !prefix.isEmpty() ? prefix.trimmed() : prefix + l,
                                  code && k > 0 && k + 1 < blockLines.size()});
            }
            prev = &b;
            pendingBlank = false;
        }
        // Outside code: no trailing whitespace, no runs of blank lines, no blank lines at the ends.
        QStringList out;
        bool lastBlank = true;
        for (Line &l : lines) {
            if (!l.code) {
                while (!l.text.isEmpty() && (l.text.back() == u' ' || l.text.back() == u'\t'))
                    l.text.chop(1);
                if (out.isEmpty()) {
                    while (!l.text.isEmpty() && l.text.front().isSpace())
                        l.text.remove(0, 1);
                }
                const bool blank = l.text.isEmpty();
                if (blank && lastBlank)
                    continue;
                lastBlank = blank;
            } else {
                lastBlank = false;
            }
            out.append(l.text);
        }
        while (!out.isEmpty() && out.last().isEmpty())
            out.removeLast();
        return out.isEmpty() ? QString() : out.join(u'\n') + u'\n';
    }

private:
    void block(const QTextBlock &b)
    {
        const QTextBlockFormat bf = b.blockFormat();
        const int quote = quoteLevel(bf);
        if (bf.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth)) {
            add(outBlock(Kind::Rule, quote, {u"---"_s}));
            return;
        }
        if (bf.nonBreakableLines()) {
            codeLine(b, bf, quote);
            return;
        }
        if (QTextList *list = b.textList()) {
            listItem(b, list, quote);
            return;
        }
        const int heading = bf.headingLevel();
        if (heading >= 1 && heading <= 6) {
            QString text = m_inline.block(b, true).replace(u'\n', u' ').trimmed();
            if (text.isEmpty())
                return;
            add(outBlock(Kind::Heading, quote, {QString(heading, u'#') + u' ' + text}));
            return;
        }
        std::optional<bool> task;
        const QString text = m_inline.block(b, false, &task);
        if (task) {
            OutBlock item = outBlock(Kind::List, quote);
            appendItemLines(item, u"- ["_s + (*task ? u'x' : u' ') + u"] "_s, QString(), text);
            add(item);
            return;
        }
        if (text.trimmed().isEmpty()) {
            add(outBlock(Kind::Blank, quote));
            return;
        }
        OutBlock para = outBlock(Kind::Paragraph, quote, text.split(u'\n'));
        para.tightTop = bf.topMargin() <= 0;
        para.tightBottom = bf.bottomMargin() <= 0;
        add(para);
    }

    static void appendItemLines(OutBlock &item, const QString &marker, const QString &indent, const QString &text)
    {
        const QStringList lines = text.split(u'\n');
        for (qsizetype k = 0; k < lines.size(); ++k) {
            if (k == 0)
                item.lines.append(indent + marker + lines[k].trimmed());
            else
                item.lines.append(indent + u"  "_s + lines[k].trimmed());
        }
    }

    void listItem(const QTextBlock &b, QTextList *list, int quote)
    {
        const QTextListFormat lf = list->format();
        const int level = std::clamp(lf.indent(), 1, 16);
        const QString indent(level - 1, u'\t');
        std::optional<bool> task;
        const QString text = m_inline.block(b, false, &task);
        QString marker;
        const int style = int(lf.style());
        if (task) {
            marker = u"- ["_s + (*task ? u'x' : u' ') + u"] "_s;
        } else if (style <= int(QTextListFormat::ListDecimal)) {
            // `<ol start="5">` counts from 5; the parts of a split list continue its numbering.
            const auto part = m_listStarts.constFind(list);
            const qint64 start = part != m_listStarts.cend()              ? *part
                                 : lf.hasProperty(QTextFormat::ListStart) ? lf.start()
                                                                          : 1;
            marker = QString::number(list->itemNumber(b) + start) + u". "_s;
        } else {
            marker = u"- "_s;
        }
        if (b.blockFormat().marker() == QTextBlockFormat::MarkerType::Checked)
            marker = u"- [x] "_s;
        else if (b.blockFormat().marker() == QTextBlockFormat::MarkerType::Unchecked)
            marker = u"- [ ] "_s;
        OutBlock item = outBlock(Kind::List, quote);
        appendItemLines(item, marker, indent, text);
        add(item);
    }

    void codeLine(const QTextBlock &b, const QTextBlockFormat &bf, int quote)
    {
        QString text = b.text();
        text.replace(QChar::LineSeparator, u'\n').replace(QChar::Nbsp, u' ');
        text.replace(QChar(UncheckedBox), u"[ ]"_s).replace(QChar(CheckedBox), u"[x]"_s);
        QString lang;
        if (text.startsWith(QChar(LangOpen))) {
            const qsizetype close = text.indexOf(QChar(LangClose));
            if (close > 0) {
                lang = text.sliced(1, close - 1);
                text.remove(0, close + 1);
            }
        }
        const bool continues = !m_blocks.isEmpty() && m_blocks.last().kind == Kind::Code
                               && m_blocks.last().quote == quote && !(m_blocks.last().codeEnd && bf.topMargin() > 0)
                               && lang.isEmpty();
        if (continues) {
            OutBlock &code = m_blocks.last();
            code.lines.append(text.split(u'\n'));
            code.codeEnd = bf.bottomMargin() > 0;
            return;
        }
        OutBlock code = outBlock(Kind::Code, quote, text.split(u'\n'));
        code.codeEnd = bf.bottomMargin() > 0;
        code.lang = lang;
        add(code);
    }

    void table(QTextTable *table)
    {
        const int rows = table->rows();
        const int cols = table->columns();
        if (rows <= 0 || cols <= 0)
            return;
        OutBlock out = outBlock(Kind::Table, 0);
        for (int r = 0; r < rows; ++r) {
            QStringList cells;
            for (int c = 0; c < cols; ++c) {
                const QTextTableCell cell = table->cellAt(r, c);
                QString text;
                if (cell.isValid() && cell.row() == r && cell.column() == c) {
                    QStringList parts;
                    for (auto it = cell.begin(); !it.atEnd(); ++it) {
                        const QTextBlock b = it.currentBlock();
                        if (!b.isValid())
                            continue;
                        const QString part = m_inline.block(b, r == 0).replace(u'\n', u' ').trimmed();
                        if (!part.isEmpty())
                            parts.append(part);
                    }
                    text = parts.join(u' ');
                }
                cells.append(text.replace(u'|', u"\\|"_s));
            }
            out.lines.append(u"| "_s + cells.join(u" | "_s) + u" |"_s);
            if (r == 0)
                out.lines.append(u'|' + QStringList(cols, u" --- "_s).join(u'|') + u'|');
        }
        add(out);
    }

    void add(OutBlock b) { m_blocks.append(std::move(b)); }

    InlineWriter m_inline;
    QHash<const QTextList *, qint64> m_listStarts; ///< see listPartStarts()
    QList<OutBlock> m_blocks;
};

bool isAsciiPunctuation(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= 0x21 && u <= 0x2F) || (u >= 0x3A && u <= 0x40) || (u >= 0x5B && u <= 0x60) || (u >= 0x7B && u <= 0x7E);
}

QChar at(QStringView s, qsizetype i)
{
    return i >= 0 && i < s.size() ? s[i] : QChar();
}

} // namespace

QString escapeMarkdownText(QStringView text, bool lineStart, bool linkLabel)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        const QChar prev = at(text, i - 1);
        const QChar next = at(text, i + 1);
        bool escape = false;
        switch (c.unicode()) {
        case u'\\': escape = isAsciiPunctuation(next) || next.isNull(); break;
        case u'`': escape = true; break;
        case u'*': escape = (!prev.isNull() && !prev.isSpace()) || (!next.isNull() && !next.isSpace()); break;
        case u'_':
            escape = (!prev.isLetterOrNumber() || !next.isLetterOrNumber())
                     && ((!prev.isNull() && !prev.isSpace()) || (!next.isNull() && !next.isSpace()));
            break;
        case u'[': escape = linkLabel || next == u'[' || next == u'^' || prev == u'!' || prev == u'['; break;
        case u']': escape = linkLabel || next == u'(' || next == u']' || prev == u']'; break;
        case u'<': escape = next.isLetter() || next == u'/' || next == u'!' || next == u'?'; break;
        case u'~': escape = next == u'~' && prev != u'~'; break;
        case u'=': escape = next == u'=' && prev != u'='; break;
        case u'%': escape = next == u'%' && prev != u'%'; break;
        case u'(': escape = next == u'(' && prev != u'('; break;
        case u'^': escape = prev.isNull() || prev.isSpace(); break;
        default: break;
        }
        if (escape)
            out += u'\\';
        out += c;
    }
    if (!lineStart || out.isEmpty())
        return out;
    // Block syntax at the start of a line.
    static const QRegularExpression ordered(u"^(\\d{1,9})([.)])(\\s|$)"_s);
    static const QRegularExpression ruleLine(u"^(?:(?:-[ \\t]*){3,}|(?:_[ \\t]*){3,}|(?:\\\\\\*[ \\t]*){3,})$"_s);
    // Any one character in brackets is a task's checkbox (`[?]`, `[…]`: custom states).
    static const QRegularExpression checkbox(u"^\\[[^\\]\\r\\n]\\](?:\\s|$)"_s);
    const QChar first = out.front();
    if ((first == u'-' || first == u'+' || first == u'*') && (out.size() == 1 || at(out, 1).isSpace()))
        return u'\\' + out;
    if (first == u'#' && (out.size() == 1 || at(out, 1) == u'#' || at(out, 1).isSpace()))
        return u'\\' + out;
    if (first == u'>')
        return u'\\' + out;
    if (out.startsWith(u"~~~"))
        return u'\\' + out;
    if (ruleLine.match(out).hasMatch() || checkbox.match(out).hasMatch())
        return u'\\' + out;
    if (const auto m = ordered.match(out); m.hasMatch())
        return m.captured(1) + u'\\' + out.sliced(m.capturedLength(1));
    return out;
}

std::optional<QString> detail::htmlAttribute(const QString &attributes, const QString &name)
{
    return attribute(attributes, name);
}

QString detail::decodeHtmlEntities(const QString &text)
{
    return decodeEntities(text);
}

QString detail::removeElements(const QString &html, const QStringList &tags, bool *removed)
{
    // Elements whose content browsers read as raw text: never closed, they run to the end.
    static const QStringList rawText = {u"script"_s, u"style"_s, u"iframe"_s, u"noscript"_s, u"noembed"_s, u"xmp"_s};
    const auto nameAt = [&](qsizetype i) -> QString {
        for (const QString &tag : tags) {
            if (QStringView(html).sliced(i).startsWith(tag, Qt::CaseInsensitive)) {
                const qsizetype end = i + tag.size();
                if (end == html.size() || !html[end].isLetterOrNumber())
                    return tag;
            }
        }
        return {};
    };
    QSet<QString> unclosed; // tags without an end tag after the position searched last
    QString out;
    qsizetype pos = 0;
    for (qsizetype i = html.indexOf(u'<'); i >= 0; i = html.indexOf(u'<', i + 1)) {
        if (i < pos)
            continue;
        const QString tag = i + 1 < html.size() ? nameAt(i + 1) : QString();
        if (tag.isEmpty())
            continue;
        out += QStringView(html).sliced(pos, i - pos);
        if (removed)
            *removed = true;
        const qsizetype gt = startTagEnd(html, i + 1 + tag.size());
        if (gt < 0) { // a start tag that never ends: nothing after it is markup
            pos = html.size();
            break;
        }
        if (tag == u"embed" || html[gt - 1] == u'/') { // void or self-closing: the tag alone
            pos = gt + 1;
            i = gt;
            continue;
        }
        const qsizetype close = unclosed.contains(tag) ? -1 : html.indexOf(u"</"_s + tag, gt + 1, Qt::CaseInsensitive);
        if (close < 0) {
            unclosed.insert(tag); // later searches start further on: they fail too
            if (rawText.contains(tag)) {
                pos = html.size();
                break;
            }
            pos = gt + 1; // only the start tag
            i = gt;
            continue;
        }
        const qsizetype closeEnd = html.indexOf(u'>', close);
        pos = closeEnd < 0 ? html.size() : closeEnd + 1;
        i = pos - 1;
    }
    if (pos == 0)
        return html;
    out += QStringView(html).sliced(std::min(pos, html.size()));
    return out;
}

QString detail::limitNesting(const QString &html, int maxDepth)
{
    static const QSet<QString> voidTags = {u"area"_s,  u"base"_s,     u"br"_s,      u"col"_s,    u"embed"_s,
                                           u"hr"_s,    u"img"_s,      u"input"_s,   u"link"_s,   u"meta"_s,
                                           u"param"_s, u"source"_s,   u"track"_s,   u"wbr"_s,    u"keygen"_s,
                                           u"frame"_s, u"basefont"_s, u"isindex"_s, u"command"_s};
    // Closed implicitly by a sibling or by the end of their parent (`<li>a<li>b`, `<p>a<p>b`).
    static const QSet<QString> implied = {u"p"_s,  u"li"_s,    u"dt"_s,    u"dd"_s,    u"tr"_s,       u"td"_s,
                                          u"th"_s, u"thead"_s, u"tbody"_s, u"tfoot"_s, u"option"_s,   u"optgroup"_s,
                                          u"rb"_s, u"rt"_s,    u"rp"_s,    u"rtc"_s,   u"colgroup"_s, u"caption"_s};
    if (maxDepth < 1)
        maxDepth = 1;
    QStringList open;
    QHash<QString, int> dropped; // start tags removed, per name, not matched by an end tag yet
    QString out;
    qsizetype last = 0;
    for (qsizetype i = html.indexOf(u'<'); i >= 0; i = html.indexOf(u'<', i + 1)) {
        if (QStringView(html).sliced(i).startsWith(u"<!--")) {
            const qsizetype end = html.indexOf(u"-->"_s, i + 4);
            if (end < 0)
                break;
            i = end + 2;
            continue;
        }
        qsizetype k = i + 1;
        const bool closing = k < html.size() && html[k] == u'/';
        if (closing)
            ++k;
        if (k >= html.size() || !html[k].isLetter())
            continue;
        qsizetype nameEnd = k;
        while (nameEnd < html.size() && isTagNameChar(html[nameEnd]))
            ++nameEnd;
        const QString name = html.sliced(k, nameEnd - k).toLower();
        const qsizetype gt = startTagEnd(html, nameEnd);
        if (gt < 0)
            break;
        const bool counted = !voidTags.contains(name) && !implied.contains(name);
        bool drop = false;
        if (!closing) {
            if (counted && html[gt - 1] != u'/') {
                if (open.size() >= maxDepth) {
                    drop = true;
                    ++dropped[name];
                } else {
                    open.append(name);
                }
            }
        } else if (counted) {
            if (auto it = dropped.find(name); it != dropped.end() && *it > 0) {
                drop = true;
                --*it;
            } else {
                for (qsizetype j = open.size() - 1; j >= 0; --j) {
                    if (open[j] == name) {
                        open.resize(j);
                        break;
                    }
                }
            }
        }
        if (drop) {
            out += QStringView(html).sliced(last, i - last);
            last = gt + 1;
        }
        i = gt;
    }
    if (last == 0)
        return html;
    out += QStringView(html).sliced(last);
    return out;
}

qsizetype detail::longestRun(QStringView text, QChar c)
{
    qsizetype best = 0;
    qsizetype run = 0;
    for (const QChar x : text) {
        run = x == c ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

QString detail::codeFenceFor(QStringView code)
{
    return QString(std::max<qsizetype>(3, longestRun(code, u'`') + 1), u'`');
}

QString detail::splitLongLists(const QString &html, int maxItems, int *parts)
{
    ListSplitter::Result r = ListSplitter(html).split(maxItems);
    if (parts)
        *parts = r.parts;
    return std::move(r.html);
}

QString detail::htmlToPlainTextWithListLimit(const QString &html, int maxListItems)
{
    SafeDocument doc;
    doc.setHtml(splitLongLists(prepare(html).html, maxListItems));
    QString text = doc.toPlainText();
    text.replace(QChar::Nbsp, u' ').replace(QChar::LineSeparator, u'\n').replace(QChar::ParagraphSeparator, u'\n');
    text.remove(QChar::ObjectReplacementCharacter);
    text.replace(QChar(UncheckedBox), u"[ ]"_s).replace(QChar(CheckedBox), u"[x]"_s);
    return text;
}

HtmlMarkdown detail::htmlToMarkdownWithListLimit(const QString &html, const HtmlToMarkdownOptions &options,
                                                 int maxListItems)
{
    const Prepared prepared = prepare(html);
    const ListSplitter::Result split = ListSplitter(prepared.html).split(maxListItems);
    SafeDocument doc;
    doc.setHtml(split.html);
    BlockWriter writer(prepared.images, options,
                       split.parts > 0 ? listPartStarts(doc, split) : QHash<const QTextList *, qint64>());
    writer.frame(doc.rootFrame());
    HtmlMarkdown out;
    out.markdown = writer.markdown();
    out.title = doc.metaInformation(QTextDocument::DocumentTitle).simplified();
    return out;
}

QString htmlToPlainText(const QString &html)
{
    return detail::htmlToPlainTextWithListLimit(html, MaxListItems);
}

HtmlMarkdown htmlToMarkdown(const QString &html, const HtmlToMarkdownOptions &options)
{
    return detail::htmlToMarkdownWithListLimit(html, options, MaxListItems);
}

} // namespace brulion::io
