#include "io/htmlmarkdown.h"

#include "io/htmlutil_p.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QRegularExpression>
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
const QString ImageScheme = u"brulion-img:"_s;
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
        qsizetype end = -1;
        QChar quote;
        for (qsizetype i = attrs; i < html.size(); ++i) {
            const QChar c = html[i];
            if (!quote.isNull()) {
                if (c == quote)
                    quote = QChar();
            } else if (c == u'"' || c == u'\'') {
                quote = c;
            } else if (c == u'>') {
                end = i;
                break;
            }
        }
        if (end < 0)
            end = html.indexOf(u'>', attrs); // an unclosed quote
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
    for (const char16_t marker : {UncheckedBox, CheckedBox, LangOpen, LangClose})
        s.remove(QChar(marker));
    s = detail::removeElements(s, {u"script"_s, u"style"_s, u"noscript"_s, u"template"_s, u"iframe"_s, u"object"_s,
                                   u"embed"_s, u"svg"_s, u"math"_s, u"canvas"_s, u"video"_s, u"audio"_s});
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
    if (!text.contains(u'`'))
        return u'`' + text + u'`';
    return u"`` "_s + text + u" ``"_s;
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

/// A code fence longer than any backtick run inside the code.
QString fence(const QString &code)
{
    QString f = u"```"_s;
    while (code.contains(f))
        f += u'`';
    return f;
}

class BlockWriter {
public:
    BlockWriter(const QList<HtmlImage> &images, const HtmlToMarkdownOptions &options) : m_inline(images, options) { }

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
                const QString f = fence(blockLines.join(u'\n'));
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
            marker = QString::number(list->itemNumber(b) + 1) + u". "_s;
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
    static const QRegularExpression checkbox(u"^\\[[ xX/>-]\\](?:\\s|$)"_s);
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

QString detail::removeElements(const QString &html, const QStringList &tags, bool *removed)
{
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
        const qsizetype close = html.indexOf(u"</"_s + tag, i + 1, Qt::CaseInsensitive);
        if (close < 0) {
            pos = html.size();
            break;
        }
        const qsizetype gt = html.indexOf(u'>', close);
        pos = gt < 0 ? html.size() : gt + 1;
        i = pos - 1;
    }
    if (pos == 0)
        return html;
    out += QStringView(html).sliced(std::min(pos, html.size()));
    return out;
}

QString htmlToPlainText(const QString &html)
{
    SafeDocument doc;
    doc.setHtml(prepare(html).html);
    QString text = doc.toPlainText();
    text.replace(QChar::Nbsp, u' ').replace(QChar::LineSeparator, u'\n').replace(QChar::ParagraphSeparator, u'\n');
    text.remove(QChar::ObjectReplacementCharacter);
    text.replace(QChar(UncheckedBox), u"[ ]"_s).replace(QChar(CheckedBox), u"[x]"_s);
    return text;
}

HtmlMarkdown htmlToMarkdown(const QString &html, const HtmlToMarkdownOptions &options)
{
    const Prepared prepared = prepare(html);
    SafeDocument doc;
    doc.setHtml(prepared.html);
    BlockWriter writer(prepared.images, options);
    writer.frame(doc.rootFrame());
    HtmlMarkdown out;
    out.markdown = writer.markdown();
    out.title = doc.metaInformation(QTextDocument::DocumentTitle).simplified();
    return out;
}

} // namespace brulion::io
