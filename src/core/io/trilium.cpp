#include "io/trilium_p.h"

#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/importutil_p.h"
#include "io/links_p.h"
#include "markdown/inline.h"
#include "markdown/math.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <utility>

namespace brulion::io::detail::trilium {

using namespace Qt::StringLiterals;

namespace {

// --- code languages ------------------------------------------------------------------------------

/// Trilium's code MIME types (packages/commons/src/lib/mime_type.ts) and their fence languages.
/// Types missing here get a language from their subtype (languageFromSubtype()).
const std::pair<const char16_t *, const char16_t *> MimeLanguages[] = {
        {u"text/plain", u""},
        {u"text/x-trilium-log", u""},
        {u"text/x-abap", u"abap"},
        {u"application/x-bat", u"bat"},
        {u"text/x-csrc", u"c"},
        {u"text/x-csharp", u"csharp"},
        {u"text/x-c++src", u"cpp"},
        {u"text/x-clojure", u"clojure"},
        {u"text/x-cmake", u"cmake"},
        {u"text/coffeescript", u"coffeescript"},
        {u"text/x-common-lisp", u"lisp"},
        {u"text/css", u"css"},
        {u"application/dart", u"dart"},
        {u"text/x-diff", u"diff"},
        {u"text/x-dockerfile", u"dockerfile"},
        {u"text/x-elixir", u"elixir"},
        {u"text/x-erlang", u"erlang"},
        {u"text/x-fsharp", u"fsharp"},
        {u"text/x-fortran", u"fortran"},
        {u"text/x-feature", u"gherkin"},
        {u"text/x-gfm", u"markdown"},
        {u"text/x-go", u"go"},
        {u"text/x-groovy", u"groovy"},
        {u"text/x-haskell", u"haskell"},
        {u"text/html", u"html"},
        {u"message/http", u"http"},
        {u"application/x-jsp", u"java"},
        {u"text/x-java", u"java"},
        {u"text/javascript", u"javascript"},
        {u"application/javascript", u"javascript"},
        {u"application/x-javascript", u"javascript"},
        {u"application/javascript;env=backend", u"javascript"},
        {u"application/javascript;env=frontend", u"javascript"},
        {u"application/json", u"json"},
        {u"application/ld+json", u"json"},
        {u"text/jsx", u"jsx"},
        {u"text/x-julia", u"julia"},
        {u"text/x-kotlin", u"kotlin"},
        {u"text/x-latex", u"latex"},
        {u"text/x-stex", u"latex"},
        {u"text/x-less", u"less"},
        {u"text/x-lua", u"lua"},
        {u"text/x-markdown", u"markdown"},
        {u"text/markdown", u"markdown"},
        {u"text/x-mariadb", u"sql"},
        {u"text/x-mssql", u"sql"},
        {u"text/x-mysql", u"sql"},
        {u"text/x-plsql", u"sql"},
        {u"text/x-pgsql", u"sql"},
        {u"text/x-sql", u"sql"},
        {u"text/x-sqlite", u"sql"},
        {u"text/x-sqlite;schema=trilium", u"sql"},
        {u"text/x-nginx-conf", u"nginx"},
        {u"text/x-nix", u"nix"},
        {u"text/x-objectivec", u"objectivec"},
        {u"text/x-ocaml", u"ocaml"},
        {u"text/x-pascal", u"pascal"},
        {u"text/x-perl", u"perl"},
        {u"text/x-php", u"php"},
        {u"application/x-powershell", u"powershell"},
        {u"text/x-properties", u"properties"},
        {u"text/x-protobuf", u"protobuf"},
        {u"text/x-python", u"python"},
        {u"text/x-rsrc", u"r"},
        {u"text/x-ruby", u"ruby"},
        {u"text/x-rustsrc", u"rust"},
        {u"text/x-sass", u"sass"},
        {u"text/x-scala", u"scala"},
        {u"text/x-scheme", u"scheme"},
        {u"text/x-scss", u"scss"},
        {u"text/x-sh", u"sh"},
        {u"text/x-swift", u"swift"},
        {u"text/x-tcl", u"tcl"},
        {u"text/x-hcl", u"hcl"},
        {u"text/x-toml", u"toml"},
        {u"application/typescript", u"typescript"},
        {u"text/typescript-jsx", u"tsx"},
        {u"text/x-vb", u"vbnet"},
        {u"text/vbscript", u"vbscript"},
        {u"text/x-verilog", u"verilog"},
        {u"text/x-vhdl", u"vhdl"},
        {u"text/x-vue", u"vue"},
        {u"text/xml", u"xml"},
        {u"application/xml", u"xml"},
        {u"text/x-yaml", u"yaml"},
        {u"text/vnd.mermaid", u"mermaid"},
        {u"text/mermaid", u"mermaid"},
};

/// Trilium's normalizeMimeTypeForCKEditor(): lower case, every run of characters other than
/// [a-z0-9] -> "-".
QString ckeditorClass(QStringView mime)
{
    QString out;
    bool dash = false;
    for (const QChar c : mime) {
        const QChar l = c.toLower();
        if ((l >= u'a' && l <= u'z') || (l >= u'0' && l <= u'9')) {
            out += l;
            dash = false;
        } else if (!dash) {
            out += u'-';
            dash = true;
        }
    }
    return out;
}

struct LanguageTables {
    QHash<QString, QString> byMime;  ///< MIME type -> language
    QHash<QString, QString> byClass; ///< CKEditor class suffix -> language
    QSet<QString> languages;         ///< every language of the table
};

const LanguageTables &languageTables()
{
    static const LanguageTables tables = [] {
        LanguageTables t;
        for (const auto &[mime, language] : MimeLanguages) {
            const QString m = QString::fromUtf16(mime);
            const QString l = QString::fromUtf16(language);
            t.byMime.insert(m, l);
            t.byClass.insert(ckeditorClass(m), l);
            if (!l.isEmpty())
                t.languages.insert(l);
        }
        for (const QString &plain : {u"text-x-trilium-auto"_s, u"plain"_s, u"plaintext"_s, u"text"_s, u"txt"_s})
            t.byClass.insert(plain, QString());
        return t;
    }();
    return tables;
}

/// "x-rustsrc" -> "rust", "x-python" -> "python", "vnd.mermaid" -> "mermaid".
QString languageFromSubtype(QString s)
{
    for (const QString &prefix : {u"x-"_s, u"vnd."_s})
        if (s.startsWith(prefix))
            s.remove(0, prefix.size());
    s.replace(u"c++"_s, u"cpp"_s);
    if (s.size() > 3 && s.endsWith(u"src"_s))
        s.chop(3);
    QString out;
    for (const QChar c : std::as_const(s)) {
        if (c.isLetterOrNumber() || c == u'+' || c == u'#' || c == u'-' || c == u'.')
            out += c;
    }
    return out == u"plain" ? QString() : out.left(32);
}

/// Line endings "\n", trailing spaces removed outside fenced code (a hard break "  " stays),
/// blank lines at both ends removed. Code keeps its lines exactly.
QString tidy(const QString &text)
{
    QString t = text;
    t.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    t = mapLinesOutsideCode(t, [](const QString &line, const QString &) {
        QString l = line;
        while (!l.isEmpty() && (l.back() == u' ' || l.back() == u'\t') && !l.endsWith(u"  "_s))
            l.chop(1);
        return l.trimmed().isEmpty() ? QString() : l;
    });
    qsizetype start = 0;
    while (start < t.size() && t[start] == u'\n')
        ++start;
    qsizetype end = t.size();
    while (end > start && t[end - 1] == u'\n')
        --end;
    return t.sliced(start, end - start);
}

// --- HTML elements ---------------------------------------------------------------------------------

bool isTagNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'-' || c == u':' || c == u'_';
}

/// Index of the '>' ending the start tag whose attributes begin at `from`: quoted values may
/// contain '>'; an unclosed quote ends at the first '>'. -1 when the tag never ends.
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

/// Next `<name` / `</name` at or after `from` and before `limit` (whole tag name,
/// case-insensitive), or -1.
qsizetype findTag(const QString &html, QStringView name, qsizetype from, bool *closing,
                  qsizetype limit = std::numeric_limits<qsizetype>::max())
{
    for (qsizetype i = html.indexOf(u'<', from); i >= 0 && i < limit; i = html.indexOf(u'<', i + 1)) {
        qsizetype k = i + 1;
        const bool close = k < html.size() && html[k] == u'/';
        if (close)
            ++k;
        if (!QStringView(html).sliced(k).startsWith(name, Qt::CaseInsensitive))
            continue;
        const qsizetype after = k + name.size();
        if (after < html.size() && isTagNameChar(html[after]))
            continue;
        *closing = close;
        return i;
    }
    return -1;
}

struct Element {
    qsizetype start = 0;        ///< '<' of the start tag
    qsizetype contentStart = 0; ///< just after the start tag
    qsizetype contentEnd = 0;   ///< '<' of the end tag (the end of the text when unclosed)
    qsizetype end = 0;          ///< just after the end tag
    bool closed = false;        ///< an end tag was found (or the element is void / self-closing)
    QString attributes;         ///< attribute text of the start tag
};

/// All elements `name` of `html` in document order (by start tag), nested ones of the same name
/// balanced; an element never closed ends with the text. `isVoid`: elements without content
/// (`<img>`, `<br>`). One pass over the text, however deep the nesting.
QList<Element> elementsOf(const QString &html, QStringView name, bool isVoid = false)
{
    QList<Element> out;
    QList<qsizetype> open; // indices into out
    bool closing = false;
    for (qsizetype i = findTag(html, name, 0, &closing); i >= 0; i = findTag(html, name, i + 1, &closing)) {
        if (closing) {
            if (isVoid || open.isEmpty())
                continue; // a stray end tag
            Element &e = out[open.takeLast()];
            const qsizetype gt = html.indexOf(u'>', i);
            e.contentEnd = i;
            e.end = gt < 0 ? html.size() : gt + 1;
            e.closed = true;
            continue;
        }
        const qsizetype attrsFrom = i + 1 + name.size();
        const qsizetype gt = startTagEnd(html, attrsFrom);
        if (gt < 0)
            break;
        Element e;
        e.start = i;
        QStringView attrs = QStringView(html).sliced(attrsFrom, gt - attrsFrom);
        const bool selfClosing = attrs.endsWith(u'/');
        if (selfClosing)
            attrs.chop(1);
        e.attributes = attrs.toString();
        e.contentStart = gt + 1;
        if (isVoid || selfClosing) {
            e.contentEnd = e.end = e.contentStart;
            e.closed = true;
        } else {
            e.contentEnd = e.end = html.size();
            open.append(out.size());
        }
        out.append(std::move(e));
        i = gt;
    }
    return out;
}

/// The first element `name` (with the class `cls`, when given).
std::optional<Element> firstElement(const QString &html, QStringView name, QStringView cls = {});

using ElementFn = std::function<std::optional<QString>(const Element &element, QStringView inner)>;

/// Replace elements `name` by `fn(element, inner HTML)`; where `fn` returns nullopt the element
/// stays and its content is searched for further elements. Replacements are not searched again.
QString transformElements(const QString &html, QStringView name, const ElementFn &fn)
{
    QString out;
    qsizetype last = 0;
    bool changed = false;
    for (const Element &e : elementsOf(html, name)) {
        if (e.start < last)
            continue; // inside a replaced element
        if (const auto replacement = fn(e, QStringView(html).sliced(e.contentStart, e.contentEnd - e.contentStart))) {
            out += QStringView(html).sliced(last, e.start - last);
            out += *replacement;
            last = e.end;
            changed = true;
        }
    }
    if (!changed)
        return html;
    out += QStringView(html).sliced(last);
    return out;
}

/// Replace every start tag `<name …>` by `fn(attributes)` (nullopt: kept as written).
QString replaceStartTags(const QString &html, QStringView name,
                         const std::function<std::optional<QString>(const QString &)> &fn)
{
    QString out;
    qsizetype last = 0;
    bool closing = false;
    for (qsizetype i = findTag(html, name, 0, &closing); i >= 0; i = findTag(html, name, i + 1, &closing)) {
        if (closing)
            continue;
        const qsizetype attrsFrom = i + 1 + name.size();
        const qsizetype gt = startTagEnd(html, attrsFrom);
        if (gt < 0)
            break;
        QStringView attrs = QStringView(html).sliced(attrsFrom, gt - attrsFrom);
        if (attrs.endsWith(u'/'))
            attrs.chop(1);
        const auto replacement = fn(attrs.toString());
        if (!replacement)
            continue;
        out += QStringView(html).sliced(last, i - last);
        out += *replacement;
        last = gt + 1;
        i = gt;
    }
    if (last == 0)
        return html;
    out += QStringView(html).sliced(last);
    return out;
}

/// Rename the elements `from` to `to` (`<figure class=x>` -> `<div>`), attributes dropped.
QString renameElements(const QString &html, QStringView from, const QString &to)
{
    QString out =
            replaceStartTags(html, from, [&](const QString &) -> std::optional<QString> { return u'<' + to + u'>'; });
    const QRegularExpression end(u"</"_s + from.toString() + u"\\s*>"_s, QRegularExpression::CaseInsensitiveOption);
    return out.replace(end, u"</"_s + to + u'>');
}

QStringList classesOf(const QString &attributes)
{
    return htmlAttribute(attributes, u"class"_s).value_or(QString()).simplified().split(u' ', Qt::SkipEmptyParts);
}

bool hasClass(const QString &attributes, QStringView cls)
{
    const QStringList classes = classesOf(attributes);
    return std::any_of(classes.begin(), classes.end(),
                       [&](const QString &c) { return c.compare(cls, Qt::CaseInsensitive) == 0; });
}

std::optional<Element> firstElement(const QString &html, QStringView name, QStringView cls)
{
    for (const Element &e : elementsOf(html, name)) {
        if (cls.isEmpty() || hasClass(e.attributes, cls))
            return e;
    }
    return std::nullopt;
}

/// The elements `name` of `html` that are not inside another one of them (`<li>` of a list, not
/// those of its nested lists).
QList<Element> outermostElements(const QString &html, QStringView name)
{
    QList<Element> out;
    qsizetype covered = 0;
    for (Element &e : elementsOf(html, name)) {
        if (e.start < covered)
            continue;
        covered = e.end;
        out.append(std::move(e));
    }
    return out;
}

/// Is `pos` inside one of `elements` (outermostElements(), in document order)?
bool insideAny(const QList<Element> &elements, qsizetype pos)
{
    const auto it = std::upper_bound(elements.cbegin(), elements.cend(), pos,
                                     [](qsizetype p, const Element &e) { return p < e.start; });
    return it != elements.cbegin() && pos < std::prev(it)->end;
}

/// Text of an HTML fragment without its tags (entities decoded, whitespace collapsed).
QString plainText(QStringView html)
{
    static const QRegularExpression tag(u"<[^>]*>"_s);
    return decodeHtmlEntities(html.toString().replace(tag, u" "_s)).replace(QChar::Nbsp, u' ').simplified();
}

QString codeSpan(const QString &text)
{
    const QString ticks(longestRun(text, u'`') + 1, u'`');
    const bool pad = text.startsWith(u'`') || text.endsWith(u'`') || ticks.size() > 1;
    return ticks + (pad ? u" "_s : QString()) + text + (pad ? u" "_s : QString()) + ticks;
}

/// `\(E=mc^2\)` -> inline math, `\[…\]` -> display math (the TeX source and whether it is display).
std::pair<QString, bool> mathSource(const QString &text)
{
    QString t = text.trimmed();
    if (t.startsWith(u"\\["_s) && t.endsWith(u"\\]"_s) && t.size() >= 4)
        return {t.sliced(2, t.size() - 4).trimmed(), true};
    if (t.startsWith(u"\\("_s) && t.endsWith(u"\\)"_s) && t.size() >= 4)
        return {t.sliced(2, t.size() - 4).trimmed(), false};
    return {t, false};
}

// A formula the importer writes travels between the formula marks FormulaOpen and FormulaClose
// (htmlutil_p.h; removed from the input first) until finishFormulas() has checked it in its
// paragraph. htmlToMarkdown() leaves it alone and escapes the dollars of the text around it that
// would pair with it or with each other (escapeTextFormulas()).

/// `source` (`$…$`, `$$…$$`) between the formula marks when the editor sees it as one formula
/// covering the whole string, otherwise a code span. TeX that Markdown would break (a backtick, a
/// `$`, a trailing backslash) keeps the old behaviour: a code span with the LaTeX source as written.
QString formulaOrCode(const QString &source)
{
    const QString masked = markdown::maskInlineCode(source);
    if (masked == source && !source.contains(u'`')) {
        const QList<markdown::MathSpan> spans = markdown::findMathSpans(masked);
        if (spans.size() == 1 && spans.first().from == 0 && spans.first().to == source.size())
            return QChar(FormulaOpen) + source + QChar(FormulaClose);
    }
    return codeSpan(source);
}

QString inlineMath(const QString &tex)
{
    return formulaOrCode(u'$' + tex + u'$');
}

/// Display math that is not a block of its own (inside a line, a table cell): `$$…$$`, with the
/// same fallback as inlineMath().
QString inlineDisplayMath(const QString &tex)
{
    return formulaOrCode(u"$$"_s + tex + u"$$"_s);
}

QString displayMath(const QString &tex)
{
    const QString f = codeFenceFor(tex);
    return f + u"math\n"_s + tex + u'\n' + f;
}

/// Code spans and the importer's formulas (between the formula marks) blanked: their text is
/// neither Markdown nor HTML to convert (a `<b>` or `#tag` inside a formula is part of the
/// formula). Text that only looks like a formula to the editor ("10$/h #pilne … 15$/h") is text.
QString maskCodeAndMath(const QString &line)
{
    QString masked = markdown::maskInlineCode(line);
    for (qsizetype i = masked.indexOf(QChar(FormulaOpen)); i >= 0; i = masked.indexOf(QChar(FormulaOpen), i)) {
        const qsizetype close = masked.indexOf(QChar(FormulaClose), i);
        if (close < 0)
            break;
        for (qsizetype k = i; k <= close; ++k)
            masked[k] = u' ';
        i = close + 1;
    }
    return masked;
}

const QString RawScheme = u"trilium-raw:"_s;
const QString RefScheme = u"trilium-ref:"_s;
/// A block written as Markdown of its own (callouts, quotes, lists, display math) travels through
/// QTextDocument as a paragraph holding only BlockOpen <number> BlockClose (private-use
/// characters, removed from the note first) and is put in place afterwards (expandBlocks()).
constexpr char16_t BlockOpen = 0xE0F0;
constexpr char16_t BlockClose = 0xE0F1;
/// Nested conversions (callouts in callouts, lists in lists, footnotes) and element recursion
/// together deeper than this are converted plainly: bounded time and stack (an Android worker
/// thread has about 1 MB; the deepest nesting needs about 100 KB, 400 KB with AddressSanitizer).
constexpr int MaxDepth = 16;
/// Element nesting kept for the conversion (QTextDocument's time grows with its square).
constexpr int MaxHtmlDepth = 64;
/// List items converted as such; later ones become paragraphs: QTextDocument's time grows with
/// the square of a list's length. (The importer keeps notes with more items as files.)
constexpr int MaxListItems = 5'000;
/// Longer lines of the Markdown export are not searched for HTML (only escaped).
constexpr qsizetype MaxIslandLine = 200'000;
/// HTML islands of the Markdown export converted per note (each takes a QTextDocument); later
/// ones stay as escaped text.
constexpr int MaxIslands = 2'000;
constexpr int MaxHtmlBlockLines = 5'000;

/// The label of a footnote (`[^label]`): its number, else its id (letters, digits, "-").
QString footnoteLabel(const QString &index, const QString &id, int counter)
{
    static const QRegularExpression digits(u"^\\d{1,6}$"_s);
    if (digits.match(index.trimmed()).hasMatch())
        return index.trimmed();
    QString clean;
    for (const QChar c : id) {
        if ((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-')
            clean += c;
    }
    return clean.isEmpty() ? QString::number(counter) : clean.left(32);
}

/// Elements HTML never closes (they hold no content).
bool isVoidElement(const QString &name)
{
    static const QSet<QString> names = {u"area"_s,  u"base"_s,   u"br"_s,    u"col"_s,  u"embed"_s,
                                        u"hr"_s,    u"img"_s,    u"input"_s, u"link"_s, u"meta"_s,
                                        u"param"_s, u"source"_s, u"track"_s, u"wbr"_s};
    return names.contains(name);
}

/// Block-level elements as list item content (lists themselves aside).
bool isBlockElement(const QString &name)
{
    static const QSet<QString> names = {
            u"p"_s,     u"div"_s,     u"pre"_s,      u"table"_s,   u"h1"_s,         u"h2"_s,     u"h3"_s,
            u"h4"_s,    u"h5"_s,      u"h6"_s,       u"hr"_s,      u"blockquote"_s, u"figure"_s, u"section"_s,
            u"aside"_s, u"details"_s, u"dl"_s,       u"address"_s, u"article"_s,    u"header"_s, u"footer"_s,
            u"nav"_s,   u"main"_s,    u"fieldset"_s, u"form"_s,    u"center"_s};
    return names.contains(name);
}

/// A top-level element of an HTML fragment.
struct Child {
    QString name; ///< lower case
    qsizetype start = 0;
    qsizetype end = 0; ///< just after its end tag (the end of the fragment when unclosed)
};

/// The top-level elements of `html` in order (text between them is not listed); nullopt when the
/// nesting is too deep to follow.
std::optional<QList<Child>> topLevelElements(const QString &html)
{
    QList<Child> out;
    QStringList open;
    qsizetype topStart = 0;
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
        if (!closing) {
            const bool empty = isVoidElement(name) || html[gt - 1] == u'/';
            if (open.isEmpty()) {
                if (empty) {
                    out.append(Child{name, i, gt + 1});
                } else {
                    topStart = i;
                    open.append(name);
                }
            } else if (!empty) {
                if (open.size() >= MaxHtmlDepth)
                    return std::nullopt;
                open.append(name);
            }
        } else if (!open.isEmpty()) {
            for (qsizetype j = open.size() - 1; j >= 0; --j) {
                if (open[j] != name)
                    continue;
                if (j == 0)
                    out.append(Child{open.first(), topStart, gt + 1});
                open.resize(j);
                break;
            }
        }
        i = gt;
    }
    if (!open.isEmpty())
        out.append(Child{open.first(), topStart, html.size()});
    return out;
}

/// Does an item's content (`<li>` inside, its top-level `children`) hold blocks QTextDocument
/// would move out of the list: several blocks, a block after text, or a block other than a
/// paragraph (code, image, quote, table, a callout …)? Nested lists are fine.
bool itemNeedsOwnConversion(const QString &content, const QList<Child> &children)
{
    int blocks = 0;
    bool inlineContent = false;
    qsizetype last = 0;
    const auto text = [&](qsizetype from, qsizetype to) {
        if (to > from && !plainText(QStringView(content).sliced(from, to - from)).isEmpty())
            inlineContent = true;
    };
    for (const Child &c : children) {
        text(last, c.start);
        last = c.end;
        if (c.name == u"ul" || c.name == u"ol")
            continue;
        if (!isBlockElement(c.name)) {
            inlineContent = true;
            continue;
        }
        const QStringView element = QStringView(content).sliced(c.start, c.end - c.start);
        if ((c.name != u"p" && c.name != u"div") || element.contains(QChar(BlockOpen)))
            return true;
        ++blocks;
    }
    text(last, content.size());
    return blocks >= 2 || (blocks == 1 && inlineContent);
}

/// CKEditor markup of Trilium -> HTML that htmlToMarkdown() understands. Markdown that the HTML
/// cannot express (footnotes, math, embeds, task states) travels as an image with a
/// "trilium-raw:<n>" source, written back by the image callback; whole blocks (callouts, quotes,
/// lists QTextDocument would break, display math) as block markers (expandBlocks()).
class Prepass {
public:
    explicit Prepass(const Resolver &resolver)
        : m_resolver(resolver), m_raws(&m_ownRaws), m_blocks(&m_ownBlocks), m_footnoteIds(&m_ownFootnoteIds)
    { }
    Prepass(const Prepass &) = delete;
    Prepass &operator=(const Prepass &) = delete;

    /// Markdown of a note's HTML (`prepass`: Trilium's markup prepared first; false for a part
    /// nested too deeply).
    QString convert(const QString &html, bool prepass = true)
    {
        const QString prepared = prepass ? run(html) : html;
        HtmlToMarkdownOptions o;
        o.image = [&](const HtmlImage &img) -> QString {
            if (const auto markdown = raw(img.src))
                return *markdown;
            return m_resolver.image ? m_resolver.image(img.src, img.alt) : QString();
        };
        o.link = [&](const QString &href, const QString &label) -> std::optional<QString> {
            QString target = href;
            const bool reference = target.startsWith(RefScheme);
            if (reference)
                target.remove(0, RefScheme.size());
            if (m_resolver.link) {
                if (auto md = m_resolver.link(target, label, reference))
                    return md;
            }
            // A reference link outside the export that is no URL either: its text.
            return reference ? std::optional<QString>(label) : std::nullopt;
        };
        QString md = tidy(expandBlocks(htmlToMarkdown(prepared, o).markdown));
        if (!m_footnotes.isEmpty())
            md += (md.isEmpty() ? QString() : u"\n\n"_s) + m_footnotes.join(u'\n');
        return md;
    }

private:
    /// A conversion of a part of the note (a callout, a footnote, a list item): it shares the
    /// markers.
    Prepass(const Resolver &resolver, QStringList *raws, QStringList *blocks, QHash<QString, QString> *footnoteIds,
            int depth)
        : m_resolver(resolver)
        , m_raws(raws)
        , m_blocks(blocks)
        , m_footnoteIds(footnoteIds)
        , m_nested(true)
        , m_depth(depth)
    { }

    /// Nesting of this conversion: enclosing conversions and the elements being recursed into.
    int depth() const { return m_depth + m_elementDepth; }

    QString nested(QStringView html)
    {
        Prepass part(m_resolver, m_raws, m_blocks, m_footnoteIds, depth() + 1);
        // Pathological nesting: the part without Trilium's markup prepared.
        return part.convert(html.toString(), depth() + 1 < MaxDepth);
    }

    QString run(QString s)
    {
        if (!m_nested) {
            // Only our own markers may use the schemes and the block characters.
            s.replace(RawScheme, u"trilium-raw-:"_s, Qt::CaseInsensitive);
            s.replace(RefScheme, u"trilium-ref-:"_s, Qt::CaseInsensitive);
            s.remove(QChar(BlockOpen)).remove(QChar(BlockClose)).remove(QChar(FormulaOpen)).remove(QChar(FormulaClose));
            s = limitListItems(limitNesting(s, MaxHtmlDepth));
        }
        s = footnoteLinks(s);
        s = footnoteSections(s);
        s = transformElements(s, u"span", [&](const Element &e, QStringView inner) { return span(e, inner); });
        s = sections(s);
        s = callouts(s);
        s = blockquotes(s);
        s = transformElements(s, u"div", [](const Element &e, QStringView) -> std::optional<QString> {
            if (hasClass(e.attributes, u"page-break"))
                return QString();
            return std::nullopt;
        });
        s = renameElements(s, u"figure", u"div"_s);
        s = renameElements(s, u"figcaption", u"p"_s);
        s = dropEmptyTasks(s);
        s = taskStates(s);
        s = replaceStartTags(s, u"code", [](const QString &attrs) -> std::optional<QString> {
            static const QRegularExpression language(u"(?:^|\\s)language-(\\S+)"_s);
            const auto m = language.match(htmlAttribute(attrs, u"class"_s).value_or(QString()));
            if (!m.hasMatch())
                return std::nullopt;
            const QString lang = languageForCodeClass(m.captured(1));
            return lang.isEmpty() ? u"<code>"_s : u"<code class=\"language-"_s + lang + u"\">"_s;
        });
        s = replaceStartTags(s, u"a", [](const QString &attrs) -> std::optional<QString> {
            const auto href = htmlAttribute(attrs, u"href"_s);
            if (!href || !hasClass(attrs, u"reference-link"))
                return std::nullopt;
            return u"<a href=\""_s + QString(RefScheme + *href).toHtmlEscaped() + u"\">"_s;
        });
        // Pretty-printed pages: the line break and indentation after a code block would end up
        // in its last line.
        static const QRegularExpression afterPre(u"</pre\\s*>\\s+"_s, QRegularExpression::CaseInsensitiveOption);
        s.replace(afterPre, u"</pre>"_s);
        return lists(s);
    }

    /// The list items after the first MaxListItems as paragraphs (their text stays).
    static QString limitListItems(const QString &html)
    {
        QString out;
        qsizetype last = 0;
        int items = 0;
        bool closing = false;
        for (qsizetype i = findTag(html, u"li", 0, &closing); i >= 0; i = findTag(html, u"li", i + 1, &closing)) {
            if (!closing && ++items <= MaxListItems)
                continue;
            if (items <= MaxListItems)
                continue; // an end tag of a kept item
            const qsizetype gt = startTagEnd(html, i + (closing ? 4 : 3));
            if (gt < 0)
                break;
            out += QStringView(html).sliced(last, i - last);
            out += closing ? u"</p>"_s : u"<p>"_s;
            last = gt + 1;
            i = gt;
        }
        if (last == 0)
            return html;
        out += QStringView(html).sliced(last);
        return out;
    }

    std::optional<QString> raw(const QString &src) const
    {
        if (!src.startsWith(RawScheme))
            return std::nullopt;
        bool ok = false;
        const int index = src.sliced(RawScheme.size()).toInt(&ok);
        return ok && index >= 0 && index < m_raws->size() ? m_raws->at(index) : QString();
    }

    /// Markdown within a line (a footnote reference, inline math, an embed, a task state).
    QString marker(const QString &markdown)
    {
        m_raws->append(markdown);
        return u"<img src=\""_s + RawScheme + QString::number(m_raws->size() - 1) + u"\">"_s;
    }

    /// Markdown blocks (lines) standing for an element: a paragraph of their own.
    QString block(const QString &markdown)
    {
        m_blocks->append(markdown);
        return u"<p>"_s + QChar(BlockOpen) + QString::number(m_blocks->size() - 1) + QChar(BlockClose) + u"</p>"_s;
    }

    /// The Markdown of htmlToMarkdown() with the block markers replaced by their blocks: further
    /// lines indented like the marker's line (a list item's continuation, a quote), inside a
    /// table row joined into one line.
    QString expandBlocks(const QString &markdown) const
    {
        if (!markdown.contains(QChar(BlockOpen)))
            return markdown;
        static const QRegularExpression token(u"\\x{E0F0}(\\d{1,9})\\x{E0F1}"_s);
        QStringList out;
        for (const QString &line : markdown.split(u'\n')) {
            if (!line.contains(QChar(BlockOpen))) {
                out.append(line);
                continue;
            }
            const bool tableRow = line.trimmed().startsWith(u'|');
            QStringList lines = {QString()};
            QString continuation;
            qsizetype last = 0;
            for (auto it = token.globalMatch(line); it.hasNext();) {
                const auto m = it.next();
                const QString before = line.sliced(last, m.capturedStart() - last);
                lines.last() += before;
                if (lines.size() == 1 && last == 0) {
                    // The text before the first block: quote markers repeat, anything else
                    // (a list marker) becomes indentation.
                    for (const QChar c : before)
                        continuation += c == u'>' || c.isSpace() ? c : QChar(u' ');
                }
                bool ok = false;
                const int index = m.captured(1).toInt(&ok);
                const QString blockMarkdown = ok && index < m_blocks->size() ? m_blocks->at(index) : QString();
                if (tableRow) {
                    lines.last() += QString(blockMarkdown).replace(u'\n', u' ').replace(u'|', u"\\|"_s);
                } else {
                    const QStringList blockLines = blockMarkdown.split(u'\n');
                    lines.last() += blockLines.first();
                    for (qsizetype k = 1; k < blockLines.size(); ++k) {
                        const QString &l = blockLines[k];
                        lines.append(l.isEmpty() ? QString(continuation).remove(u' ').remove(u'\t') : continuation + l);
                    }
                }
                last = m.capturedEnd();
            }
            lines.last() += line.sliced(last);
            out.append(lines);
        }
        return out.join(u'\n');
    }

    /// Footnote references of notes Trilium imported from Markdown (`<a href="#fn…"><sup>[1]</sup>
    /// </a>`, also `<sup><a href="#fn…">[1]</a></sup>`) -> `[^1]`; their ids are remembered for
    /// the definitions.
    QString footnoteLinks(const QString &html)
    {
        static const QRegularExpression ref(
                u"<a\\b[^>]*?\\bhref\\s*=\\s*\"#fn(?!ref)([^\"]*)\"[^>]*>\\s*<sup\\b[^>]*>\\s*\\[(\\d{1,6})\\]\\s*"
                u"</sup>\\s*</"
                u"a>|<sup\\b[^>]*>\\s*<a\\b[^>]*?\\bhref\\s*=\\s*\"#fn(?!ref)([^\"]*)\"[^>]*>\\s*\\[(\\d{1,6})"
                u"\\]\\s*</a>\\s*</sup>"_s,
                QRegularExpression::CaseInsensitiveOption);
        if (!html.contains(u"#fn"_s))
            return html;
        QString out;
        qsizetype last = 0;
        for (auto it = ref.globalMatch(html); it.hasNext();) {
            const auto m = it.next();
            const QString id = m.captured(1) + m.captured(3);
            const QString number = m.captured(2) + m.captured(4);
            m_footnoteIds->insert(id, number);
            out += QStringView(html).sliced(last, m.capturedStart() - last);
            out += marker(u"[^"_s + number + u']');
            last = m.capturedEnd();
        }
        if (last == 0)
            return html;
        out += QStringView(html).sliced(last);
        return out;
    }

    /// A footnote definition `[^label]: …` of the item content `html` (continuation lines
    /// indented).
    void addFootnote(const QString &label, QStringView html)
    {
        const QStringList lines = nested(html).split(u'\n');
        QString def = u"[^"_s + label + u"]: "_s + lines.first();
        for (qsizetype k = 1; k < lines.size(); ++k)
            def += lines[k].isEmpty() ? u"\n"_s : u"\n    "_s + lines[k];
        m_footnotes.append(def);
    }

    /// The footnotes at the end of a note: CKEditor's `<ol class="footnote-section">`, or (notes
    /// imported from Markdown) a list whose every item starts with a back link
    /// `<a href="#fnref…">^</a>`.
    QString footnoteSections(const QString &html)
    {
        if (!html.contains(u"footnote-section"_s, Qt::CaseInsensitive) && !html.contains(u"#fnref"_s))
            return html;
        return transformElements(html, u"ol", [&](const Element &e, QStringView inner) -> std::optional<QString> {
            const QString in = inner.toString();
            if (hasClass(e.attributes, u"footnote-section")) {
                for (const Element &item : outermostElements(in, u"li")) {
                    const QString content = in.sliced(item.contentStart, item.contentEnd - item.contentStart);
                    QString text = content;
                    if (const auto div = firstElement(content, u"div", u"footnote-content"))
                        text = content.sliced(div->contentStart, div->contentEnd - div->contentStart);
                    else
                        text = transformElements(content, u"span",
                                                 [](const Element &s, QStringView) -> std::optional<QString> {
                                                     if (hasClass(s.attributes, u"footnote-back-link"))
                                                         return QString();
                                                     return std::nullopt;
                                                 });
                    addFootnote(
                            footnoteLabel(htmlAttribute(item.attributes, u"data-footnote-index"_s).value_or(QString()),
                                          htmlAttribute(item.attributes, u"data-footnote-id"_s).value_or(QString()),
                                          int(m_footnotes.size()) + 1),
                            text);
                }
                return QString();
            }
            static const QRegularExpression backLink(
                    u"^\\s*(?:<p\\b[^>]*>\\s*)?(<a\\b[^>]*?\\bhref\\s*=\\s*\"#fnref([^\"]*)\"[^>]*>)"_s,
                    QRegularExpression::CaseInsensitiveOption);
            const QList<Element> items = outermostElements(in, u"li");
            if (items.isEmpty())
                return std::nullopt;
            QList<QRegularExpressionMatch> links;
            for (const Element &item : items) {
                auto m = backLink.matchView(
                        QStringView(in).sliced(item.contentStart, item.contentEnd - item.contentStart));
                if (!m.hasMatch())
                    return std::nullopt;
                links.append(std::move(m));
            }
            bool ok = false;
            const int start = htmlAttribute(e.attributes, u"start"_s).value_or(QString()).toInt(&ok);
            for (qsizetype k = 0; k < items.size(); ++k) {
                const QString content = in.sliced(items[k].contentStart, items[k].contentEnd - items[k].contentStart);
                const QRegularExpressionMatch &m = links[k];
                // The back link (`^`) goes with its anchor element.
                const qsizetype close = content.indexOf(u"</a"_s, m.capturedEnd(1), Qt::CaseInsensitive);
                const qsizetype closeEnd = close < 0 ? -1 : content.indexOf(u'>', close);
                const QString text =
                        content.first(m.capturedStart(1)) + (closeEnd < 0 ? QString() : content.sliced(closeEnd + 1));
                const QString number = QString::number((ok ? start : 1) + int(k));
                addFootnote(m_footnoteIds->value(m.captured(2), number), text);
            }
            return QString();
        });
    }

    std::optional<QString> span(const Element &e, QStringView inner)
    {
        const QString &attrs = e.attributes;
        if (hasClass(attrs, u"footnote-reference")) {
            return marker(u"[^"_s
                          + footnoteLabel(htmlAttribute(attrs, u"data-footnote-index"_s).value_or(QString()),
                                          htmlAttribute(attrs, u"data-footnote-id"_s).value_or(QString()),
                                          ++m_references)
                          + u']');
        }
        if (hasClass(attrs, u"math-tex")) {
            const auto [tex, display] = mathSource(plainText(inner));
            if (tex.isEmpty())
                return QString();
            return display ? block(displayMath(tex)) : marker(inlineMath(tex));
        }
        if (hasClass(attrs, u"tn-icon"))
            return QString();
        if (hasClass(attrs, u"link-mention"))
            return previewLink(attrs, inner);
        return std::nullopt;
    }

    /// A link preview (opengraph card, mention) as an ordinary link to its URL.
    static QString previewLink(const QString &attrs, QStringView inner)
    {
        const QString url = htmlAttribute(attrs, u"data-url"_s).value_or(QString()).trimmed();
        QString title = htmlAttribute(attrs, u"data-title"_s).value_or(QString()).simplified();
        if (title.isEmpty())
            title = plainText(inner);
        if (url.isEmpty())
            return title.toHtmlEscaped();
        if (title.isEmpty())
            title = url;
        return u"<a href=\""_s + url.toHtmlEscaped() + u"\">"_s + title.toHtmlEscaped() + u"</a>"_s;
    }

    QString sections(const QString &html)
    {
        ElementFn fn = [&](const Element &e, QStringView inner) -> std::optional<QString> {
            if (hasClass(e.attributes, u"include-note")) {
                const QString id = htmlAttribute(e.attributes, u"data-note-id"_s).value_or(QString()).trimmed();
                const QString embed = m_resolver.include && !id.isEmpty() ? m_resolver.include(id) : QString();
                return embed.isEmpty() ? QString() : u"<p>"_s + marker(embed) + u"</p>"_s;
            }
            if (hasClass(e.attributes, u"link-embed"))
                return u"<p>"_s + previewLink(e.attributes, inner) + u"</p>"_s;
            if (depth() >= MaxDepth)
                return u"<div>"_s + inner.toString() + u"</div>"_s;
            ++m_elementDepth;
            const QString body = transformElements(inner.toString(), u"section", fn);
            --m_elementDepth;
            return u"<div>"_s + body + u"</div>"_s;
        };
        return transformElements(html, u"section", fn);
    }

    /// Admonitions (`<aside class="admonition tip">`) -> `> [!tip]`, collapsible blocks
    /// (`<details><summary>S</summary>…`) -> `> [!note]- S` (`+` when open).
    QString callouts(const QString &html)
    {
        ElementFn aside = [&](const Element &e, QStringView inner) -> std::optional<QString> {
            if (!hasClass(e.attributes, u"admonition")) {
                if (depth() >= MaxDepth)
                    return u"<div>"_s + inner.toString() + u"</div>"_s;
                ++m_elementDepth;
                const QString body = transformElements(inner.toString(), u"aside", aside);
                --m_elementDepth;
                return u"<div>"_s + body + u"</div>"_s;
            }
            QString type = u"note"_s;
            for (const QString &c : classesOf(e.attributes)) {
                if (c.compare(u"admonition"_s, Qt::CaseInsensitive) != 0) {
                    type = c.toLower();
                    break;
                }
            }
            return callout(u"[!"_s + type + u']', inner);
        };
        const ElementFn details = [&](const Element &e, QStringView inner) -> std::optional<QString> {
            const QString in = inner.toString();
            QString summary;
            QString body = in;
            if (const auto s = firstElement(in, u"summary")) {
                summary = plainText(QStringView(in).sliced(s->contentStart, s->contentEnd - s->contentStart));
                body = in.first(s->start) + in.sliced(s->end);
            }
            QString header = htmlAttribute(e.attributes, u"open"_s) ? u"[!note]+"_s : u"[!note]-"_s;
            if (!summary.isEmpty())
                header += u' ' + escapeMarkdownText(summary, false);
            return callout(header, body);
        };
        return transformElements(transformElements(html, u"aside", aside), u"details", details);
    }

    /// Markdown lines `prefix + line` of converted HTML (`> ` quotes; blank lines `>`).
    QString quoted(const QString &firstLine, QStringView bodyHtml)
    {
        QStringList lines;
        if (!firstLine.isEmpty())
            lines.append(u"> "_s + firstLine);
        const QString body = nested(bodyHtml);
        if (!body.isEmpty()) {
            for (const QString &l : body.split(u'\n'))
                lines.append(l.isEmpty() ? u">"_s : u"> "_s + l);
        }
        return lines.join(u'\n');
    }

    /// A callout written as Markdown of its own (its content converted separately, so callouts
    /// next to quotes stay separate blocks and callouts may nest).
    QString callout(const QString &header, QStringView bodyHtml) { return block(quoted(header, bodyHtml)); }

    /// Quotes as Markdown of their own: QTextDocument merges quotes next to each other and moves
    /// a quote out of a list item. Quotes in tables stay as they are (a cell holds one line).
    QString blockquotes(const QString &html)
    {
        if (!html.contains(u"<blockquote"_s, Qt::CaseInsensitive))
            return html;
        const QList<Element> tables = outermostElements(html, u"table");
        return transformElements(html, u"blockquote",
                                 [&](const Element &e, QStringView inner) -> std::optional<QString> {
                                     if (insideAny(tables, e.start))
                                         return std::nullopt;
                                     const QString markdown = quoted(QString(), inner);
                                     return markdown.isEmpty() ? QString() : block(markdown);
                                 });
    }

    /// To-do items without text (the empty checkbox of a day template) are left out.
    QString dropEmptyTasks(const QString &html)
    {
        if (!html.contains(u"<input"_s, Qt::CaseInsensitive))
            return html;
        QString out;
        qsizetype last = 0;
        for (const Element &item : elementsOf(html, u"li")) {
            if (item.start < last || !item.closed)
                continue;
            const QStringView content =
                    QStringView(html).sliced(item.contentStart, item.contentEnd - item.contentStart);
            if (!content.contains(u"<input"_s, Qt::CaseInsensitive)
                || !content.contains(u"checkbox"_s, Qt::CaseInsensitive)
                || content.contains(u"<img"_s, Qt::CaseInsensitive) || content.contains(u"<ul"_s, Qt::CaseInsensitive)
                || content.contains(u"<ol"_s, Qt::CaseInsensitive) || !plainText(content).isEmpty())
                continue;
            out += QStringView(html).sliced(last, item.start - last);
            last = item.end;
        }
        if (last == 0)
            return html;
        out += QStringView(html).sliced(last);
        return out;
    }

    /// To-do items in progress / cancelled (`<li data-trilium-task-state="doing">`): their
    /// checkbox becomes `[/]` / `[-]` (done, open and other states keep the checkbox: `[x]`, `[ ]`).
    QString taskStates(const QString &html)
    {
        static const QRegularExpression item(u"<li\\b"_s, QRegularExpression::CaseInsensitiveOption);
        QString out;
        qsizetype last = 0;
        for (auto it = item.globalMatch(html); it.hasNext();) {
            const auto m = it.next();
            if (m.capturedStart() < last)
                continue;
            const qsizetype gt = startTagEnd(html, m.capturedEnd());
            if (gt < 0)
                break;
            const QString attrs = html.sliced(m.capturedEnd(), gt - m.capturedEnd());
            const QString state =
                    htmlAttribute(attrs, u"data-trilium-task-state"_s).value_or(QString()).trimmed().toLower();
            const QString symbol = state == u"doing" ? u"/"_s : state == u"cancelled" ? u"-"_s : QString();
            if (symbol.isEmpty())
                continue;
            // The item's own checkbox: right after the start tag, before any nested list or item.
            bool closing = false;
            const qsizetype input = findTag(html, u"input", gt, &closing, gt + 2000);
            if (input < 0 || closing)
                continue;
            const auto before = [&](QStringView tag) {
                bool c = false;
                return findTag(html, tag, gt, &c, input) >= 0;
            };
            if (before(u"li") || before(u"ul") || before(u"ol"))
                continue;
            const qsizetype inputEnd = startTagEnd(html, input + 6);
            if (inputEnd < 0)
                continue;
            out += QStringView(html).sliced(last, input - last);
            out += marker(u'[' + symbol + u"] "_s);
            last = inputEnd + 1;
        }
        if (last == 0)
            return html;
        out += QStringView(html).sliced(last);
        return out;
    }

    /// Lists QTextDocument cannot keep: an item holding blocks besides its text (code, images,
    /// quotes, more paragraphs …) — they would leave the list —, or a list right after another
    /// one of the same kind (Markdown would merge them; it gets the other marker, `*` / `1)`).
    /// Such a list is written as Markdown of its own. Lists in tables stay as they are.
    QString lists(const QString &html)
    {
        if (!html.contains(u"<ul"_s, Qt::CaseInsensitive) && !html.contains(u"<ol"_s, Qt::CaseInsensitive))
            return html;
        struct List {
            Element element;
            bool ordered = false;
        };
        QList<List> all;
        for (Element &e : outermostElements(html, u"ul"))
            all.append(List{std::move(e), false});
        for (Element &e : outermostElements(html, u"ol"))
            all.append(List{std::move(e), true});
        std::sort(all.begin(), all.end(),
                  [](const List &a, const List &b) { return a.element.start < b.element.start; });
        const QList<Element> tables = outermostElements(html, u"table");
        QString out;
        qsizetype last = 0;
        qsizetype covered = 0;
        qsizetype previousEnd = -1;
        bool previousOrdered = false;
        bool previousAlternate = false;
        for (const List &l : std::as_const(all)) {
            const Element &e = l.element;
            if (e.start < covered)
                continue; // a list in a list
            covered = e.end;
            if (insideAny(tables, e.start)) {
                previousEnd = -1;
                continue;
            }
            const bool adjacent = previousEnd >= 0 && l.ordered == previousOrdered
                                  && plainText(QStringView(html).sliced(previousEnd, e.start - previousEnd)).isEmpty()
                                  && !QStringView(html).sliced(previousEnd, e.start - previousEnd).contains(u'<');
            const bool alternate = adjacent && !previousAlternate;
            previousEnd = e.end;
            previousOrdered = l.ordered;
            previousAlternate = alternate;
            const QString inner = html.sliced(e.contentStart, e.contentEnd - e.contentStart);
            if (!alternate && !listNeedsOwnConversion(inner))
                continue;
            out += QStringView(html).sliced(last, e.start - last);
            const QStringList lines = listMarkdown(inner, l.ordered, listStart(e), 0, alternate);
            out += lines.isEmpty() ? QString() : block(lines.join(u'\n'));
            last = e.end;
        }
        if (last == 0)
            return html;
        out += QStringView(html).sliced(last);
        return out;
    }

    static int listStart(const Element &list)
    {
        bool ok = false;
        const int start = htmlAttribute(list.attributes, u"start"_s).value_or(QString()).trimmed().toInt(&ok);
        return ok && start >= 0 && start < 1'000'000'000 ? start : 1;
    }

    /// Has the list `inner` (or a list nested in it) an item itemNeedsOwnConversion()? Each level
    /// reads only its own items: linear per level, however the items nest.
    static bool listNeedsOwnConversion(const QString &inner, int level = 0)
    {
        for (const Element &item : outermostElements(inner, u"li")) {
            const QString content = inner.sliced(item.contentStart, item.contentEnd - item.contentStart);
            const auto children = topLevelElements(content);
            if (!children)
                continue;
            if (itemNeedsOwnConversion(content, *children))
                return true;
            if (level + 1 >= MaxHtmlDepth)
                continue;
            for (const Child &c : *children) {
                if (c.name != u"ul" && c.name != u"ol")
                    continue;
                const QString list = content.sliced(c.start, c.end - c.start);
                const auto e = firstElement(list, c.name);
                if (e
                    && listNeedsOwnConversion(list.sliced(e->contentStart, e->contentEnd - e->contentStart), level + 1))
                    return true;
            }
        }
        return false;
    }

    /// Markdown lines of a list (`inner`: the HTML inside `<ul>` / `<ol>`) at nesting `level`:
    /// every item's first block on the marker line, its other blocks below it (a blank line
    /// before each), indented past the marker; nested lists indented by a tab per level.
    QStringList listMarkdown(const QString &inner, bool ordered, int start, int level, bool alternate)
    {
        QStringList lines;
        const QString indent(level, u'\t');
        int number = start;
        for (const Element &item : outermostElements(inner, u"li")) {
            QString content = inner.sliced(item.contentStart, item.contentEnd - item.contentStart);
            const std::optional<bool> checkbox = takeCheckbox(content);
            QString marker;
            if (ordered && !checkbox)
                marker = QString::number(number) + (alternate ? u") "_s : u". "_s);
            else
                marker = alternate ? u"* "_s : u"- "_s;
            // Continuation lines are indented past the list marker (a checkbox is content).
            const QString continuation = indent + QString(marker.size(), u' ');
            if (checkbox)
                marker += *checkbox ? u"[x] "_s : u"[ ] "_s;
            QStringList itemLines;
            const auto addBlock = [&](const QString &markdown) {
                if (markdown.trimmed().isEmpty())
                    return;
                const QStringList mdLines = markdown.split(u'\n');
                qsizetype k = 0;
                if (itemLines.isEmpty()) {
                    itemLines.append(indent + marker + mdLines.first());
                    k = 1;
                } else {
                    itemLines.append(QString());
                }
                for (; k < mdLines.size(); ++k)
                    itemLines.append(mdLines[k].isEmpty() ? QString() : continuation + mdLines[k]);
            };
            const auto addList = [&](const Child &c) {
                const QString list = content.sliced(c.start, c.end - c.start);
                if (itemLines.isEmpty())
                    itemLines.append(indent + marker.trimmed());
                if (depth() + 1 >= MaxDepth) { // pathological nesting: converted plainly
                    for (const QString &l : nested(list).split(u'\n'))
                        itemLines.append(l.isEmpty() ? QString() : continuation + l);
                    return;
                }
                const auto e = firstElement(list, c.name);
                if (!e)
                    return;
                ++m_elementDepth;
                itemLines.append(listMarkdown(list.sliced(e->contentStart, e->contentEnd - e->contentStart),
                                              c.name == u"ol", listStart(*e), level + 1, false));
                --m_elementDepth;
            };
            const auto children = topLevelElements(content);
            if (!children) {
                addBlock(nested(content));
            } else {
                qsizetype runStart = 0; // inline content not converted yet
                const auto flush = [&](qsizetype end) {
                    const QStringView run = QStringView(content).sliced(runStart, end - runStart);
                    if (!run.trimmed().isEmpty())
                        addBlock(nested(run));
                };
                for (const Child &c : *children) {
                    const bool list = c.name == u"ul" || c.name == u"ol";
                    if (!list && !isBlockElement(c.name))
                        continue; // inline: part of the run
                    flush(c.start);
                    if (list)
                        addList(c);
                    else
                        addBlock(nested(QStringView(content).sliced(c.start, c.end - c.start)));
                    runStart = c.end;
                }
                flush(content.size());
            }
            if (itemLines.isEmpty())
                continue; // an empty item
            lines.append(itemLines);
            ++number;
        }
        return lines;
    }

    /// The checkbox of a to-do item (`<label><input type="checkbox" checked>…`), removed from
    /// `content`: checked or not; nullopt when the item has none before its text.
    static std::optional<bool> takeCheckbox(QString &content)
    {
        bool closing = false;
        const qsizetype input = findTag(content, u"input", 0, &closing, 4000);
        if (input < 0 || closing)
            return std::nullopt;
        const QStringView before = QStringView(content).first(input);
        if (!plainText(before).isEmpty() || before.contains(u"<img"_s, Qt::CaseInsensitive)
            || before.contains(u"<ul"_s, Qt::CaseInsensitive) || before.contains(u"<ol"_s, Qt::CaseInsensitive))
            return std::nullopt;
        const qsizetype gt = startTagEnd(content, input + 6);
        if (gt < 0)
            return std::nullopt;
        const QString attrs = content.sliced(input + 6, gt - input - 6);
        if (htmlAttribute(attrs, u"type"_s).value_or(QString()).compare(u"checkbox"_s, Qt::CaseInsensitive) != 0)
            return std::nullopt;
        const bool checked = htmlAttribute(attrs, u"checked"_s).has_value();
        content.remove(input, gt + 1 - input);
        return checked;
    }

    const Resolver &m_resolver;
    QStringList m_ownRaws;
    QStringList m_ownBlocks;
    QHash<QString, QString> m_ownFootnoteIds;
    QStringList *m_raws;                    ///< the inline markers of the whole note
    QStringList *m_blocks;                  ///< the block markers of the whole note
    QHash<QString, QString> *m_footnoteIds; ///< footnote id -> label (notes imported from Markdown)
    bool m_nested = false;
    int m_depth = 0;
    int m_elementDepth = 0;
    QStringList m_footnotes;
    int m_references = 0;
};

// --- Markdown export -----------------------------------------------------------------------------

/// HTML that Turndown leaves inline in a line (reference links, images, `<sub>`, spans, …).
const QSet<QString> &inlineTags()
{
    static const QSet<QString> tags = {u"a"_s, u"img"_s,  u"span"_s, u"sup"_s,   u"sub"_s,  u"kbd"_s,
                                       u"u"_s, u"mark"_s, u"br"_s,   u"small"_s, u"abbr"_s, u"cite"_s,
                                       u"q"_s, u"s"_s,    u"del"_s,  u"ins"_s,   u"em"_s,   u"strong"_s,
                                       u"i"_s, u"b"_s,    u"big"_s,  u"time"_s,  u"var"_s,  u"samp"_s};
    return tags;
}

/// HTML that Turndown leaves as a block (figures, include-note boxes, `<details>`, tables).
const QSet<QString> &blockTags()
{
    static const QSet<QString> tags = {u"figure"_s,     u"section"_s, u"details"_s, u"table"_s,
                                       u"aside"_s,      u"div"_s,     u"ul"_s,      u"ol"_s,
                                       u"blockquote"_s, u"p"_s,       u"dl"_s,      u"figcaption"_s};
    return tags;
}

/// Tag name right after `<` at `pos` (lower case), empty when none.
QString tagNameAt(const QString &text, qsizetype pos)
{
    qsizetype k = pos + 1;
    while (k < text.size() && (text[k].isLetterOrNumber() || text[k] == u'-'))
        ++k;
    return text.sliced(pos + 1, k - pos - 1).toLower();
}

bool escapedAt(QStringView text, qsizetype pos)
{
    qsizetype n = 0;
    for (qsizetype k = pos - 1; k >= 0 && text[k] == u'\\'; --k)
        ++n;
    return n % 2 == 1;
}

/// Markdown of a piece of HTML within a line (one line).
QString inlineHtml(const QString &html, const Resolver &resolver)
{
    return htmlNoteToMarkdown(html, resolver).replace(u'\n', u' ').trimmed();
}

/// The islands Turndown leaves most often, when they hold plain text only: written directly
/// (a QTextDocument per island is slow). nullopt: needs the full conversion.
std::optional<QString> simpleInline(const QString &name, const Element &e, const QString &line,
                                    const Resolver &resolver)
{
    static const QSet<QString> plain = {u"sub"_s,  u"sup"_s,  u"u"_s,    u"small"_s, u"big"_s,
                                        u"abbr"_s, u"cite"_s, u"time"_s, u"var"_s,   u"ins"_s};
    const QString inner = line.sliced(e.contentStart, e.contentEnd - e.contentStart);
    if (inner.contains(u'<'))
        return std::nullopt;
    const QString text = decodeHtmlEntities(inner).replace(QChar::Nbsp, u' ').simplified();
    if (name == u"img") {
        if (!resolver.image)
            return QString();
        return resolver.image(htmlAttribute(e.attributes, u"src"_s).value_or(QString()).trimmed(),
                              htmlAttribute(e.attributes, u"alt"_s).value_or(QString()).trimmed());
    }
    if (name == u"a") {
        const auto href = htmlAttribute(e.attributes, u"href"_s);
        if (!href || !resolver.link)
            return std::nullopt;
        if (auto md = resolver.link(*href, escapeMarkdownText(text, false, true),
                                    hasClass(e.attributes, u"reference-link")))
            return md;
        return std::nullopt; // an external link: the full conversion writes it
    }
    if (name == u"kbd" || name == u"samp")
        return text.isEmpty() ? QString() : codeSpan(text);
    if (name == u"span") {
        if (hasClass(e.attributes, u"tn-icon"))
            return QString();
        if (e.attributes.contains(u"background"_s, Qt::CaseInsensitive) || e.attributes.contains(u"class"_s))
            return std::nullopt; // a highlight, a Trilium widget
        return escapeMarkdownText(text, false, true);
    }
    // Brackets escaped: `[<sup>[1]</sup>](…)` must not become a wiki link `[[1]]`.
    if (plain.contains(name))
        return escapeMarkdownText(text, false, true);
    return std::nullopt;
}

/// A figure inside a line (a table row of the Markdown export): its image and caption.
/// nullopt: anything else in it (the full conversion writes it).
std::optional<QString> figureInline(const Element &e, const QString &line, const Resolver &resolver)
{
    QString inner = line.sliced(e.contentStart, e.contentEnd - e.contentStart);
    QString caption;
    if (const auto c = firstElement(inner, u"figcaption")) {
        caption = plainText(QStringView(inner).sliced(c->contentStart, c->contentEnd - c->contentStart));
        inner = inner.first(c->start) + inner.sliced(c->end);
    }
    const QList<Element> images = elementsOf(inner, u"img", true);
    if (images.size() != 1)
        return std::nullopt;
    const Element &img = images.first();
    if (!plainText(QStringView(inner).first(img.start)).isEmpty()
        || !plainText(QStringView(inner).sliced(img.end)).isEmpty())
        return std::nullopt;
    QString md = resolver.image ? resolver.image(htmlAttribute(img.attributes, u"src"_s).value_or(QString()).trimmed(),
                                                 htmlAttribute(img.attributes, u"alt"_s).value_or(QString()).trimmed())
                                : QString();
    if (!caption.isEmpty())
        md += (md.isEmpty() ? QString() : u" "_s) + escapeMarkdownText(caption, false);
    return md;
}

/// A text segment of a Markdown line with `<` that would start HTML escaped (old exports wrote
/// `<` of the text unescaped; Brulion shows raw HTML as text anyway). Autolinks stay.
QString escapeAngles(QStringView text, QStringView masked)
{
    static const QRegularExpression autolink(u"^<(?:[A-Za-z][A-Za-z0-9+.-]{1,31}:[^\\s<>]*|[^\\s<>@]+@[^\\s<>@]+)>"_s);
    QString out;
    out.reserve(text.size() + 4);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'<' && masked[i] == u'<' && i + 1 < text.size()) {
            const QChar next = text[i + 1];
            // A short window: a regular expression checks its whole subject on every match.
            if ((next.isLetter() || next == u'/' || next == u'!' || next == u'?') && !escapedAt(text, i)
                && !autolink.matchView(text.sliced(i, std::min<qsizetype>(text.size() - i, 2048))).hasMatch())
                out += u'\\';
        }
        out += c;
    }
    return out;
}

/// One line of the Markdown export (outside code blocks): HTML islands converted (at most
/// `budget` more), math as code.
QString convertLine(const QString &line, const Resolver &resolver, int &budget)
{
    static const QRegularExpression footnoteRef(
            u"^<sup>\\s*<a\\s+href=\"#fn([^\"]*)\"\\s*>\\s*\\[(\\d{1,6})\\]\\s*</a>\\s*</sup>"_s,
            QRegularExpression::CaseInsensitiveOption);
    const QString masked = maskCodeAndMath(line);
    if (line.size() > MaxIslandLine || !masked.contains(u'<'))
        return escapeAngles(line, masked);
    QHash<QString, QList<Element>> elements; // per tag name, found once per line
    const auto elementAt = [&](const QString &name, qsizetype pos) -> const Element * {
        auto it = elements.find(name);
        if (it == elements.end()) // in `masked`: no "<b" of inline code taken for a tag
            it = elements.insert(name, elementsOf(masked, name, name == u"img" || name == u"br"));
        const auto e = std::lower_bound(it->cbegin(), it->cend(), pos,
                                        [](const Element &x, qsizetype p) { return x.start < p; });
        return e != it->cend() && e->start == pos ? &*e : nullptr;
    };
    QString out;
    qsizetype last = 0;
    for (qsizetype i = masked.indexOf(u'<'); i >= 0; i = masked.indexOf(u'<', i + 1)) {
        if (i < last || escapedAt(line, i))
            continue;
        if (const auto m =
                    footnoteRef.matchView(QStringView(line).sliced(i, std::min<qsizetype>(line.size() - i, 512)));
            m.hasMatch()) {
            out += escapeAngles(QStringView(line).sliced(last, i - last), QStringView(masked).sliced(last, i - last));
            out += u"[^"_s + m.captured(2) + u']';
            last = i + m.capturedLength();
            i = last - 1;
            continue;
        }
        const QString name = tagNameAt(line, i);
        const bool figure = name == u"figure" || name == u"figcaption";
        if (!figure && !inlineTags().contains(name))
            continue;
        const Element *e = elementAt(name, i);
        if (!e || !e->closed)
            continue;
        const auto simple = figure ? figureInline(*e, line, resolver) : simpleInline(name, *e, line, resolver);
        if (!simple) {
            if (budget <= 0)
                continue; // stays as (escaped) text
            --budget;
        }
        out += escapeAngles(QStringView(line).sliced(last, i - last), QStringView(masked).sliced(last, i - last));
        const QString md = simple ? *simple : inlineHtml(line.sliced(i, e->end - i), resolver);
        out += md;
        last = e->end;
        // A dropped element (an icon) leaves no double space.
        if (md.isEmpty() && out.endsWith(u' ') && last < line.size() && line[last] == u' ')
            ++last;
        i = last - 1;
    }
    out += escapeAngles(QStringView(line).sliced(last), QStringView(masked).sliced(last));
    return out;
}

/// Text with Markdown's backslash escapes removed: 0.6x's Turndown escaped the TeX of math
/// (`\\int\_0` -> `\int_0`).
QString unescapeMarkdown(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'\\' && i + 1 < text.size()) {
            const char16_t n = text[i + 1].unicode();
            if ((n >= 0x21 && n <= 0x2F) || (n >= 0x3A && n <= 0x40) || (n >= 0x5B && n <= 0x60)
                || (n >= 0x7B && n <= 0x7E)) {
                out += text[i + 1];
                ++i;
                continue;
            }
        }
        out += c;
    }
    return out;
}

/// Inline math of the Markdown export -> a formula: `$…$` and `$$…$$` (0.10x, the TeX as
/// written), `\\(…\\)` and `\\\[…\\\]` (0.6x: the TeX escaped by Turndown). A `$` before a
/// digit does not close math ("$5 i $10"). TeX that Markdown would break stays a code span.
QString convertInlineMath(const QString &line)
{
    static const QRegularExpression math(
            uR"((?<![\\$\w])\$\$([^$\n]+?)\$\$(?![$\d])|(?<![\\$\w])\$(?=[^\s$])([^$\n]*?[^\s$\\])\$(?![$\d])|\\\\\((.+?)\\\\\)|\\\\\\?\[(.+?)\\\\\\?\])"_s);
    if (!line.contains(u'$') && !line.contains(u"\\\\"_s))
        return line;
    const QString masked = markdown::maskInlineCode(line);
    QString out;
    qsizetype last = 0;
    for (auto it = math.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        if (QStringView(masked).sliced(m.capturedStart(), m.capturedLength())
            != QStringView(line).sliced(m.capturedStart(), m.capturedLength()))
            continue; // touches code
        out += QStringView(line).sliced(last, m.capturedStart() - last);
        if (m.capturedLength(1) > 0)
            out += inlineDisplayMath(m.captured(1).trimmed());
        else if (m.capturedLength(2) > 0)
            out += inlineMath(m.captured(2));
        else if (m.capturedLength(3) > 0)
            out += inlineMath(unescapeMarkdown(m.captured(3)).trimmed());
        else
            out += inlineDisplayMath(unescapeMarkdown(m.captured(4)).trimmed());
        last = m.capturedEnd();
    }
    if (last == 0)
        return line;
    out += QStringView(line).sliced(last);
    return out;
}

/// A line holding only display math — `$$…$$` (several: one block each) or 0.6x's
/// `\\\[…\\\]` (escaped TeX) — after quote markers: the prefix and the TeX of each block;
/// nullopt for any other line. Linear (no backtracking over the segments).
std::optional<std::pair<QString, QStringList>> displayMathLine(const QString &line)
{
    qsizetype i = 0;
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t' || line[i] == u'>'))
        ++i;
    const QString prefix = line.first(i);
    QStringList blocks;
    while (true) {
        while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
            ++i;
        if (i >= line.size())
            break;
        const QStringView rest = QStringView(line).sliced(i);
        if (rest.startsWith(u"$$")) {
            const qsizetype end = line.indexOf(u"$$"_s, i + 2);
            if (end < 0)
                return std::nullopt;
            const QString tex = line.sliced(i + 2, end - i - 2).trimmed();
            if (tex.isEmpty() || tex.contains(u'$'))
                return std::nullopt;
            blocks.append(tex);
            i = end + 2;
            continue;
        }
        if (rest.startsWith(u"\\\\[") || rest.startsWith(u"\\\\\\[")) {
            const qsizetype open = rest.startsWith(u"\\\\[") ? 3 : 4;
            qsizetype end = line.indexOf(u"\\\\\\]"_s, i + open);
            qsizetype closeLength = 4;
            const qsizetype shortEnd = line.indexOf(u"\\\\]"_s, i + open);
            if (shortEnd >= 0 && (end < 0 || shortEnd < end)) {
                end = shortEnd;
                closeLength = 3;
            }
            if (end < 0)
                return std::nullopt;
            const QString tex = unescapeMarkdown(QStringView(line).sliced(i + open, end - i - open)).trimmed();
            if (tex.isEmpty())
                return std::nullopt;
            blocks.append(tex);
            i = end + closeLength;
            continue;
        }
        return std::nullopt;
    }
    if (blocks.isEmpty())
        return std::nullopt;
    return std::make_pair(prefix, blocks);
}

/// Strikethrough with single tildes (old exports: `~text~`) -> `~~text~~`.
QString convertStrike(const QString &line)
{
    static const QRegularExpression strike(u"(?<![~\\\\\\w])~(?=[^\\s~])([^~\\n]*?[^\\s~\\\\])~(?![~\\w])"_s);
    if (!line.contains(u'~'))
        return line;
    const QString masked = maskCodeAndMath(line);
    QString out;
    qsizetype last = 0;
    for (auto it = strike.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        if (QStringView(masked).sliced(m.capturedStart(), m.capturedLength())
            != QStringView(line).sliced(m.capturedStart(), m.capturedLength()))
            continue;
        out += QStringView(line).sliced(last, m.capturedStart() - last);
        out += u"~~"_s + m.captured(1) + u"~~"_s;
        last = m.capturedEnd();
    }
    if (last == 0)
        return line;
    return out + QStringView(line).sliced(last);
}

/// Callout types lower-cased (`> [!TIP]` -> `> [!tip]`), task states the dialect lacks
/// (`[?]` maybe, custom symbols) -> `[ ]`.
QString convertMarkers(const QString &line)
{
    static const QRegularExpression callout(u"^((?:[ \\t]*>)+[ \\t]*)\\[!([A-Za-z]+)\\]"_s);
    static const QRegularExpression task(
            u"^([ \\t]*(?:>[ \\t]*)*(?:[-*+]|\\d{1,9}[.)])[ \\t]+)\\[([^\\]\\n])\\](?=\\s|$)"_s);
    if (const auto m = callout.match(line); m.hasMatch())
        return m.captured(1) + u"[!"_s + m.captured(2).toLower() + u']' + line.sliced(m.capturedEnd());
    if (const auto m = task.match(line); m.hasMatch()) {
        static const QString known = u" xX/->"_s;
        if (!known.contains(m.captured(2)))
            return m.captured(1) + u"[ ]"_s + line.sliced(m.capturedEnd());
    }
    return line;
}

struct Fence {
    QChar ch;
    qsizetype length = 0;
};

/// A fence opening (or closing) line: prefix (quotes, list marker), fence and info string.
struct FenceLine {
    QString prefix;
    Fence fence;
    QString info;
    QString rest;
};

std::optional<FenceLine> fenceLine(const QString &line)
{
    static const QRegularExpression re(
            u"^([ \\t]*(?:>[ \\t]*)*(?:(?:[-*+]|\\d{1,9}[.)])[ \\t]+)?)(`{3,}|~{3,})[ \\t]*([^`\\s]*)(.*)$"_s);
    const auto m = re.match(line);
    if (!m.hasMatch())
        return std::nullopt;
    FenceLine f;
    f.prefix = m.captured(1);
    f.fence = Fence{m.captured(2).front(), m.capturedLength(2)};
    f.info = m.captured(3);
    f.rest = m.captured(4);
    return f;
}

/// Footnote references of notes Trilium imported from Markdown, as its Markdown export writes
/// them (`[<sup>[1]</sup>](#fn…)`) -> `[^1]`.
QString convertFootnoteRefs(const QString &line)
{
    static const QRegularExpression ref(u"\\[<sup>\\[(\\d{1,6})\\]</sup>\\]\\(#fn(?!ref)[^)\\s]*\\)"_s,
                                        QRegularExpression::CaseInsensitiveOption);
    if (!line.contains(u"](#fn"_s))
        return line;
    const QString masked = maskCodeAndMath(line);
    QString out;
    qsizetype last = 0;
    for (auto it = ref.globalMatch(masked); it.hasNext();) {
        const auto m = it.next();
        if (QStringView(masked).sliced(m.capturedStart(), m.capturedLength())
            != QStringView(line).sliced(m.capturedStart(), m.capturedLength()))
            continue;
        out += QStringView(line).sliced(last, m.capturedStart() - last);
        out += u"[^"_s + m.captured(1) + u']';
        last = m.capturedEnd();
    }
    if (last == 0)
        return line;
    return out + QStringView(line).sliced(last);
}

/// A text line of the Markdown export (outside code blocks): footnote references, math (first,
/// so its TeX is code), HTML islands (at most `budget` more), single-tilde strikethrough.
QString convertText(const QString &line, const Resolver &resolver, int &budget)
{
    return convertStrike(convertLine(convertInlineMath(convertFootnoteRefs(line)), resolver, budget));
}

/// Ordered lists that follow each other with only blank lines between them (Turndown writes two
/// lists like one loose list): the later one gets the other delimiter (`5)`), so they stay
/// apart. A list is new when its first number does not continue the previous item's.
QStringList separateOrderedLists(QStringList lines)
{
    static const QRegularExpression item(u"^([ \\t]*)(\\d{1,9})([.)])(?=[ \\t]|$)"_s);
    const auto indentOf = [](const QString &l) {
        qsizetype n = 0;
        while (n < l.size() && (l[n] == u' ' || l[n] == u'\t'))
            ++n;
        return n;
    };
    struct Last {
        qsizetype indent = 0;
        int number = 0;
        QChar written; ///< the delimiter the list is written with
        QChar original;
    };
    std::optional<Last> last;
    bool blank = false;
    std::optional<Fence> fence;
    for (QString &line : lines) {
        if (fence) {
            if (const auto f = fenceLine(line); f && f->fence.ch == fence->ch && f->fence.length >= fence->length
                                                && f->info.isEmpty() && f->rest.trimmed().isEmpty())
                fence.reset();
            continue;
        }
        if (const auto f = fenceLine(line)) {
            fence = f->fence;
            if (last && indentOf(line) <= last->indent)
                last.reset();
            continue;
        }
        if (line.trimmed().isEmpty()) {
            blank = true;
            continue;
        }
        const auto m = item.match(line);
        if (!m.hasMatch()) {
            // Text at the list's indentation (or left of it) ends the list; indented text belongs
            // to an item.
            if (last && indentOf(line) <= last->indent)
                last.reset();
            blank = false;
            continue;
        }
        const qsizetype indent = m.capturedLength(1);
        if (last && indent > last->indent) {
            blank = false; // a nested item
            continue;
        }
        const int number = m.captured(2).toInt();
        const QChar delimiter = m.captured(3).front();
        QChar written = delimiter;
        if (last && indent == last->indent) {
            const bool newList = (blank && number != last->number + 1) || delimiter != last->original;
            written = newList ? (last->written == u'.' ? u')' : u'.') : last->written;
        }
        if (written != delimiter)
            line[m.capturedStart(3)] = written;
        last = Last{indent, number, written, delimiter};
        blank = false;
    }
    return lines;
}

/// Is `text` at `pos` the body of a tag (`#tag`, `#[[tag]]`): tag characters with a non-digit?
bool tagBodyAt(QStringView text, qsizetype pos)
{
    if (QStringView(text).sliced(pos).startsWith(u"[["))
        return true;
    bool nonDigit = false;
    qsizetype k = pos;
    for (; k < text.size(); ++k) {
        const QChar c = text[k];
        if (!c.isLetterOrNumber() && !c.isMark() && c != u'_' && c != u'-' && c != u'/')
            break;
        if (!c.isDigit())
            nonDigit = true;
    }
    return k > pos && nonDigit;
}

/// `#` before a tag-like word escaped (`\#archived`): Trilium has no inline tags, so "#node.js",
/// a colour "#a13d5f" or "#archived" in the text are no tags in Brulion either. Code, wiki links
/// and link destinations are left alone.
QString escapeHashtags(const QString &markdown)
{
    if (!markdown.contains(u'#'))
        return markdown;
    return mapLinesOutsideCode(markdown, [](const QString &line, const QString &masked) -> QString {
        if (!masked.contains(u'#'))
            return line;
        const QString plain = maskCodeAndMath(line);
        QString out;
        qsizetype last = 0;
        bool changed = false;
        bool wiki = false;
        for (qsizetype i = 0; i < plain.size(); ++i) {
            const QChar c = plain[i];
            if (c == u'[' && i + 1 < plain.size() && plain[i + 1] == u'[') {
                wiki = true;
                ++i;
                continue;
            }
            if (c == u']' && i + 1 < plain.size() && plain[i + 1] == u']') {
                wiki = false;
                ++i;
                continue;
            }
            if (c != u'#' || wiki)
                continue;
            const QChar prev = i > 0 ? line[i - 1] : QChar();
            if (!prev.isNull() && !prev.isSpace() && prev != u'(')
                continue;
            if (prev == u'(' && i >= 2 && line[i - 2] == u']')
                continue; // a link destination `](#…)`
            if (!tagBodyAt(line, i + 1))
                continue;
            out += QStringView(line).sliced(last, i - last);
            out += u'\\';
            last = i;
            changed = true;
        }
        if (!changed)
            return line;
        return out + QStringView(line).sliced(last);
    });
}

/// `line` with each of the importer's formulas as a code span with its LaTeX source (the output
/// before Brulion had formulas), the formula marks removed.
QString formulasAsCode(const QString &line)
{
    QString out;
    qsizetype last = 0;
    for (qsizetype i = line.indexOf(QChar(FormulaOpen)); i >= 0; i = line.indexOf(QChar(FormulaOpen), last)) {
        const qsizetype close = line.indexOf(QChar(FormulaClose), i);
        if (close < 0)
            break;
        out += QStringView(line).sliced(last, i - last);
        out += codeSpan(line.sliced(i + 1, close - i - 1));
        last = close + 1;
    }
    return out + QStringView(line).sliced(last);
}

/// The importer's formulas in lines [from, to) of a paragraph (lines without a blank one between
/// them) checked as the editor reads them: each must be exactly one formula of the paragraph's
/// text (findMathSpans() with code masked, SYNTAX §11). htmlToMarkdown() has escaped the dollars
/// of the text that would pair with them ("Cena $5,$x$" -> "Cena \$5,$x$"); a letter or digit
/// right after one ("$x$2") still pairs the delimiters differently: then the formulas of that
/// line become code spans. Bounded: after two such rounds all of them do.
void checkFormulas(QStringList &lines, qsizetype from, qsizetype to)
{
    struct Formula {
        qsizetype from = 0;
        qsizetype to = 0;
        qsizetype line = 0;
    };
    for (int round = 0;; ++round) {
        QString text;
        QList<Formula> formulas;
        for (qsizetype k = from; k < to; ++k) {
            if (k > from)
                text += u'\n';
            qsizetype open = -1;
            for (const QChar c : std::as_const(lines[k])) {
                if (c == QChar(FormulaOpen)) {
                    open = text.size();
                } else if (c == QChar(FormulaClose)) {
                    if (open >= 0)
                        formulas.append({open, text.size(), k});
                    open = -1;
                } else {
                    text += c;
                }
            }
        }
        if (formulas.isEmpty())
            return;
        QSet<std::pair<qsizetype, qsizetype>> spans;
        for (const markdown::MathSpan &span : markdown::findMathSpans(markdown::maskInlineCode(text)))
            spans.insert({span.from, span.to});
        QSet<qsizetype> wrong;
        for (const Formula &f : std::as_const(formulas)) {
            if (round >= 2 || !spans.contains({f.from, f.to}))
                wrong.insert(f.line);
        }
        if (wrong.isEmpty())
            return;
        for (const qsizetype k : std::as_const(wrong))
            lines[k] = formulasAsCode(lines[k]);
    }
}

/// The importer's formulas checked in their paragraphs (checkFormulas()), the formula marks
/// removed. Fenced code is left alone.
QString finishFormulas(const QString &markdown)
{
    if (!markdown.contains(QChar(FormulaOpen)))
        return QString(markdown).remove(QChar(FormulaClose));
    QStringList lines = markdown.split(u'\n');
    std::optional<Fence> fence;
    qsizetype paragraph = -1; // first line of the current paragraph
    for (qsizetype k = 0; k <= lines.size(); ++k) {
        bool text = false;
        if (k < lines.size()) {
            const QString &line = lines[k];
            if (fence) {
                if (const auto f = fenceLine(line); f && f->fence.ch == fence->ch && f->fence.length >= fence->length
                                                    && f->info.isEmpty() && f->rest.trimmed().isEmpty())
                    fence.reset();
            } else if (const auto f = fenceLine(line)) {
                fence = f->fence;
            } else {
                text = !line.trimmed().isEmpty();
            }
        }
        if (text) {
            if (paragraph < 0)
                paragraph = k;
            continue;
        }
        if (paragraph >= 0)
            checkFormulas(lines, paragraph, k);
        paragraph = -1;
    }
    return lines.join(u'\n').remove(QChar(FormulaOpen)).remove(QChar(FormulaClose));
}

} // namespace

QString languageForMime(const QString &mime)
{
    const LanguageTables &t = languageTables();
    const QString m = mime.trimmed().toLower().remove(u' ');
    if (const auto it = t.byMime.constFind(m); it != t.byMime.constEnd())
        return *it;
    const QString base = m.section(u';', 0, 0);
    if (const auto it = t.byMime.constFind(base); it != t.byMime.constEnd())
        return *it;
    return languageFromSubtype(base.section(u'/', -1));
}

QString languageForCodeClass(const QString &cls)
{
    const LanguageTables &t = languageTables();
    QString c = cls.trimmed().toLower();
    if (const auto it = t.byClass.constFind(c); it != t.byClass.constEnd())
        return *it;
    if (t.languages.contains(c))
        return c;
    if (c.startsWith(u"application-javascript"_s))
        return u"javascript"_s;
    for (const QString &prefix : {u"text-x-"_s, u"application-x-"_s, u"text-"_s, u"application-"_s}) {
        if (c.startsWith(prefix)) {
            c.remove(0, prefix.size());
            break;
        }
    }
    if (const qsizetype env = c.indexOf(u"-env-"_s); env > 0)
        c.truncate(env);
    return languageFromSubtype(c);
}

QString noteBody(const QString &page)
{
    if (const auto content = firstElement(page, u"div", u"ck-content"))
        return page.sliced(content->contentStart, content->contentEnd - content->contentStart);
    if (const auto body = firstElement(page, u"body")) {
        const QString inner = page.sliced(body->contentStart, body->contentEnd - body->contentStart);
        // The title heading of a page without the content wrapper.
        return transformElements(inner, u"h1", [](const Element &h, QStringView) -> std::optional<QString> {
            if (htmlAttribute(h.attributes, u"data-trilium-h1"_s))
                return QString();
            return std::nullopt;
        });
    }
    return page;
}

QString htmlNoteToMarkdown(const QString &html, const Resolver &resolver)
{
    Prepass prepass(resolver);
    return finishFormulas(escapeHashtags(prepass.convert(html)));
}

QString markdownNoteToMarkdown(const QString &markdown, const QString &title, const Resolver &resolver)
{
    // The footnotes of the export: "1.  <sup><strong><a href="#fnref…">^</a></strong></sup>", or
    // for notes imported from Markdown "1.  [**<sup>^</sup>**](#fnref…)", the text indented below.
    static const QRegularExpression footnoteItem(
            u"^\\s*(\\d{1,6})\\.\\s+(?:<sup>\\s*<strong>\\s*<a\\s+href=\"#fnref[^\"]*\"\\s*>\\s*\\^\\s*</a>\\s*</"
            u"strong>"
            u"\\s*</sup>|\\[\\*\\*<sup>\\^</sup>\\*\\*\\]\\(#fnref[^)\\s]*\\))\\s*(.*)$"_s,
            QRegularExpression::CaseInsensitiveOption);

    QString text = markdown;
    text.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    text.remove(QChar(FormulaOpen)).remove(QChar(FormulaClose)); // only the importer's own marks
    QStringList lines = text.split(u'\n');
    if (!title.isEmpty() && !lines.isEmpty() && lines.first().trimmed() == u"# "_s + title.trimmed())
        lines.removeFirst();

    QStringList out;
    std::optional<Fence> fence;
    int islands = MaxIslands;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (fence) {
            if (const auto f = fenceLine(line); f && f->fence.ch == fence->ch && f->fence.length >= fence->length
                                                && f->info.isEmpty() && f->rest.trimmed().isEmpty())
                fence.reset();
            out.append(line);
            continue;
        }
        if (const auto f = fenceLine(line)) {
            fence = f->fence;
            out.append(f->prefix + QString(f->fence.length, f->fence.ch) + languageForCodeClass(f->info) + f->rest);
            continue;
        }
        if (const auto m = footnoteItem.match(line); m.hasMatch()) {
            QStringList body;
            if (!m.captured(2).trimmed().isEmpty())
                body.append(m.captured(2).trimmed());
            qsizetype j = i + 1;
            for (; j < lines.size(); ++j) {
                const QString &next = lines[j];
                if (next.trimmed().isEmpty()) {
                    body.append(QString());
                    continue;
                }
                if (!next.startsWith(u"    "_s) && !next.startsWith(u'\t'))
                    break;
                body.append(next.startsWith(u'\t') ? next.sliced(1) : next.sliced(4));
            }
            i = j - 1;
            const QStringList defLines = normalizeNewlines(body.join(u'\n')).split(u'\n');
            QString def = u"[^"_s + m.captured(1) + u"]: "_s + convertText(defLines.first(), resolver, islands);
            for (qsizetype k = 1; k < defLines.size(); ++k)
                def += defLines[k].isEmpty() ? u"\n"_s : u"\n    "_s + convertText(defLines[k], resolver, islands);
            out.append(def);
            continue;
        }
        // A block of HTML (figure, include-note box, <details>, table): converted as a whole.
        const QString trimmed = line.trimmed();
        const qsizetype lead = trimmed.isEmpty() ? 0 : line.indexOf(trimmed.front());
        if (trimmed.startsWith(u'<') && blockTags().contains(tagNameAt(trimmed, 0))) {
            const QString name = tagNameAt(trimmed, 0);
            // The block ends with the line that closes its first element.
            const auto depthChange = [&](const QString &part) {
                int d = 0;
                bool closing = false;
                for (qsizetype t = findTag(part, name, 0, &closing); t >= 0; t = findTag(part, name, t + 1, &closing))
                    d += closing ? -1 : 1;
                return d;
            };
            QString html = trimmed;
            qsizetype j = i;
            for (int depth = depthChange(trimmed); depth > 0 && j + 1 < lines.size() && j - i < MaxHtmlBlockLines;) {
                html += u'\n' + lines[++j];
                depth += depthChange(lines[j]);
            }
            i = j;
            const QString prefix = line.first(lead);
            const QString md = htmlNoteToMarkdown(html, resolver);
            if (!out.isEmpty() && !out.last().trimmed().isEmpty())
                out.append(QString());
            for (const QString &l : md.split(u'\n'))
                out.append(l.isEmpty() ? QString() : prefix + l);
            if (i + 1 < lines.size() && !lines[i + 1].trimmed().isEmpty())
                out.append(QString());
            continue;
        }
        if (const auto math = displayMathLine(line)) {
            const auto &[prefix, blocks] = *math;
            for (qsizetype k = 0; k < blocks.size(); ++k) {
                if (k > 0)
                    out.append(QString(prefix).remove(u' ').remove(u'\t'));
                for (const QString &l : displayMath(blocks[k]).split(u'\n'))
                    out.append(prefix + l);
            }
            continue;
        }
        // A to-do item without text (the empty checkbox of a day template) is left out; with
        // nested items below it, it stays as a plain item.
        static const QRegularExpression emptyTask(
                u"^([ \\t]*(?:[-*+]|\\d{1,9}[.)]))[ \\t]+\\[[ xX]\\][\\s\\x{00A0}]*$"_s);
        if (const auto m = emptyTask.match(line); m.hasMatch()) {
            qsizetype next = i + 1;
            while (next < lines.size() && lines[next].trimmed().isEmpty())
                ++next;
            const auto indentOf = [](const QString &l) {
                qsizetype n = 0;
                while (n < l.size() && (l[n] == u' ' || l[n] == u'\t'))
                    ++n;
                return n;
            };
            if (next < lines.size() && indentOf(lines[next]) > indentOf(line))
                out.append(m.captured(1));
            continue;
        }
        out.append(convertMarkers(convertText(line, resolver, islands)));
    }
    text = separateOrderedLists(out).join(u'\n');

    // Markdown links and images into the export.
    text = replaceLinks(text, [&](const FoundLink &l) -> std::optional<QString> {
        if (l.kind != LinkKind::Markdown || l.hasUrlScheme)
            return std::nullopt;
        QString dest = l.destination;
        if (dest.startsWith(u'<') && dest.endsWith(u'>'))
            dest = dest.sliced(1, dest.size() - 2);
        if (l.image)
            return resolver.image ? std::optional<QString>(resolver.image(dest, QString(l.display).remove(u'\\')))
                                  : std::nullopt;
        return resolver.link ? resolver.link(dest, l.display, false) : std::nullopt;
    });
    // Links CommonMark does not accept: old exports wrote attachment names with spaces raw
    // (`[lista zakupów.txt](Ogród planbudżet_lista zakupów.txt)`).
    static const QRegularExpression loose(u"(!?)\\[([^\\]\\n]*)\\]\\(([^()\\n]+)\\)"_s);
    text = mapLinesOutsideCode(text, [&](const QString &line, const QString &masked) -> QString {
        if (!masked.contains(u"]("_s))
            return line;
        QString result;
        qsizetype last = 0;
        for (auto it = loose.globalMatch(masked); it.hasNext();) {
            const auto m = it.next();
            const QString dest = line.sliced(m.capturedStart(3), m.capturedLength(3)).trimmed();
            static const QRegularExpression scheme(u"^[A-Za-z][A-Za-z0-9+.-]{1,31}:"_s);
            if (scheme.match(dest).hasMatch() || escapedAt(line, m.capturedStart()))
                continue;
            const QString label = line.sliced(m.capturedStart(2), m.capturedLength(2));
            std::optional<QString> md;
            if (m.capturedLength(1) > 0)
                md = resolver.image ? std::optional<QString>(resolver.image(dest, QString(label).remove(u'\\')))
                                    : std::nullopt;
            else if (resolver.link)
                md = resolver.link(dest, label, false);
            if (!md)
                continue;
            result += QStringView(line).sliced(last, m.capturedStart() - last);
            result += *md;
            last = m.capturedEnd();
        }
        if (last == 0)
            return line;
        return result + QStringView(line).sliced(last);
    });
    return finishFormulas(escapeHashtags(tidy(text)));
}

} // namespace brulion::io::detail::trilium
