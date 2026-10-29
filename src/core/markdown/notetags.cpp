// findNoteTags(): every tag occurrence of a note with its document range (SYNTAX §3) — the
// inline and property tags the parser collects, plus the items of the frontmatter's tag key
// located in the YAML source. Used to rename tags (rename.cpp) and by the index's clients.
#include "markdown/frontmatter.h"
#include "markdown/frontmatter_p.h"
#include "markdown/inline_p.h"
#include "markdown/lines_p.h"
#include "markdown/parse.h"
#include "markdown/parse_p.h"
#include "markdown/properties.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// The ranges of splitTopLevelCommas(): split on commas outside `[[…]]`, `((…))` and quotes.
QList<Range> topLevelCommaRanges(QStringView s)
{
    QList<Range> out;
    int depth = 0;
    bool quoted = false;
    qsizetype start = 0;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (quoted) {
            if (c == u'"')
                quoted = false;
            continue;
        }
        const QChar next = charAt(s, i + 1);
        if (c == u'"') {
            quoted = true;
        } else if ((c == u'[' && next == u'[') || (c == u'(' && next == u'(')) {
            ++depth;
            ++i;
        } else if (((c == u']' && next == u']') || (c == u')' && next == u')')) && depth > 0) {
            --depth;
            ++i;
        } else if (c == u',' && depth == 0) {
            out.append({start, i});
            start = i + 1;
        }
    }
    out.append({start, s.size()});
    return out;
}

/// `range` of `s` without surrounding whitespace.
Range trimmed(QStringView s, Range range)
{
    auto [a, b] = range;
    while (a < b && s[a].isSpace())
        ++a;
    while (b > a && s[b - 1].isSpace())
        --b;
    return {a, b};
}

bool isQuote(QChar c)
{
    return c == u'"' || c == u'\'';
}

/// Inside matching quotes: the range between them; else `range` itself.
Range unquoted(QStringView s, Range range)
{
    const auto [a, b] = range;
    if (b - a >= 2 && isQuote(s[a]) && s[b - 1] == s[a])
        return trimmed(s, {a + 1, b - 1});
    return range;
}

QStringView slice(QStringView s, Range r)
{
    return s.sliced(r.first, r.second - r.first);
}

// ---- The frontmatter's tag key ----------------------------------------------------------------

/// A line of the YAML source with its offset in the document.
struct YamlLine {
    qsizetype offset = 0; ///< document offset of text[0]
    QStringView text;     ///< without the line break
    int line = 0;         ///< document line
};

QList<YamlLine> yamlLines(QStringView yaml, qsizetype base, int firstLine)
{
    QList<YamlLine> out;
    qsizetype pos = 0;
    int line = firstLine;
    while (pos < yaml.size()) {
        qsizetype end = pos;
        while (end < yaml.size() && !isLineBreak(yaml[end]))
            ++end;
        out.append(YamlLine{base + pos, yaml.sliced(pos, end - pos), line++});
        pos = end < yaml.size() && yaml[end] == u'\r' && charAt(yaml, end + 1) == u'\n' ? end + 2 : end + 1;
    }
    return out;
}

int yamlIndent(QStringView line)
{
    int n = 0;
    while (n < line.size() && (line[n] == u' ' || line[n] == u'\t'))
        ++n;
    return n;
}

/// Collects the items of the tag key (namesFrom()'s rules) with their document ranges.
class FrontmatterScan {
public:
    explicit FrontmatterScan(FrontmatterTags &out) : m_out(out) { }

    /// One list item (`- x`, a flow item): `range` of `line` (quotes included). False when its
    /// form is not one the scan can place.
    bool listItem(const YamlLine &line, Range range)
    {
        range = trimmed(line.text, range);
        if (range.first == range.second)
            return true; // null item: namesFrom() skips it
        const QChar first = line.text[range.first];
        if (first == u'{' || first == u'|' || first == u'>' || first == u'&' || first == u'*' || first == u'!')
            return false;
        if (first == u'[') // a nested list: `[[Dom]]` is the link [[Dom]] (namesFrom() reads "Dom")
            return linkItem(line, range);
        if (!quotedOk(line.text, range))
            return false;
        const Range inner = unquoted(line.text, range);
        static const QRegularExpression severalTags(u"^#\\S+(\\s+#\\S+)+$"_s);
        if (severalTags.matchView(slice(line.text, inner)).hasMatch())
            return words(line, inner);
        return name(line, inner);
    }

    /// A scalar value (`tags: a, b c`, `tags: "a b"`): split on commas, then words.
    bool scalar(const YamlLine &line, Range range)
    {
        range = trimmed(line.text, range);
        if (range.first == range.second)
            return true;
        if (!quotedOk(line.text, range))
            return false;
        const Range inner = unquoted(line.text, range);
        const QStringView value = slice(line.text, inner);
        for (const Range &part : topLevelCommaRanges(value)) {
            const Range r = trimmed(value, part);
            const Range abs{inner.first + r.first, inner.first + r.second};
            const QStringView t = slice(line.text, abs);
            if (t.startsWith(u"[[") || t.startsWith(u"#[["))
                name(line, abs);
            else if (!words(line, abs))
                return false;
        }
        return true;
    }

private:
    /// A quoted item must be closed and free of escapes (their offsets would not map).
    static bool quotedOk(QStringView s, Range range)
    {
        const auto [a, b] = range;
        if (!isQuote(s[a]))
            return true;
        if (b - a < 2 || s[b - 1] != s[a])
            return false;
        const QStringView inner = s.sliced(a + 1, b - a - 2);
        return !inner.contains(s[a]) && (s[a] != u'"' || !inner.contains(u'\\'));
    }

    /// Whitespace-separated words of `range`, each a name.
    bool words(const YamlLine &line, Range range)
    {
        qsizetype i = range.first;
        while (i < range.second) {
            while (i < range.second && line.text[i].isSpace())
                ++i;
            const qsizetype start = i;
            while (i < range.second && !line.text[i].isSpace())
                ++i;
            if (i > start)
                name(line, {start, i});
        }
        return true;
    }

    /// One name: cleanName() of the text, its range without '#', quotes and brackets.
    bool name(const YamlLine &line, Range range)
    {
        const QStringView text = slice(line.text, range);
        const QString cleaned = cleanName(text);
        if (cleaned.isEmpty())
            return true;
        Range r = unquoted(line.text, range);
        if (r.first < r.second && line.text[r.first] == u'#')
            r = trimmed(line.text, {r.first + 1, r.second});
        const bool link = slice(line.text, r).startsWith(u"[[");
        if (link)
            r = trimmed(line.text, {r.first + 2, std::max(r.first + 2, r.second - 2)});
        if (slice(line.text, r) != cleaned)
            return false;
        add(line, r, cleaned, link);
        return true;
    }

    /// A nested flow list item: `[Page]` (namesFrom() reads "Page"; right after the outer list's
    /// `[` it is the source of the link `[[Page]]`). Deeper nesting is skipped, as namesFrom()
    /// skips it.
    bool linkItem(const YamlLine &line, Range range)
    {
        const QStringView text = slice(line.text, range);
        if (text.startsWith(u"[[") && text.endsWith(u"]]"))
            return true;
        if (!text.endsWith(u']') || text.size() < 3)
            return false;
        const Range r = trimmed(line.text, {range.first + 1, range.second - 1});
        const QStringView inner = slice(line.text, r);
        if (inner.isEmpty() || inner.contains(u'[') || inner.contains(u']') || inner.contains(u',')
            || isQuote(inner[0]))
            return false;
        const QString cleaned = cleanName(inner);
        if (cleaned.isEmpty() || inner != cleaned)
            return false;
        add(line, r, cleaned, range.first > 0 && line.text[range.first - 1] == u'[');
        return true;
    }

    void add(const YamlLine &line, Range r, const QString &cleaned, bool link)
    {
        NoteTag tag;
        tag.name = cleaned.normalized(QString::NormalizationForm_C);
        tag.from = line.offset + r.first;
        tag.to = line.offset + r.second;
        tag.line = line.line;
        tag.form = TagForm::Frontmatter;
        m_out.items.append(FrontmatterTagItem{tag, link});
        m_names.append(cleaned);
    }

    FrontmatterTags &m_out;

public:
    QStringList m_names;
};

/// A flow list `[a, "b", [[c]]]` starting at `open` of line `k`, possibly over several lines.
bool scanFlow(const QList<YamlLine> &lines, qsizetype k, qsizetype open, FrontmatterScan &scan)
{
    int depth = 0;
    QChar quote;
    for (; k < lines.size(); ++k, open = 0) {
        const YamlLine &line = lines[k];
        qsizetype start = depth == 1 ? open : -1; // an item may start on a continuation line
        for (qsizetype i = open; i < line.text.size(); ++i) {
            const QChar c = line.text[i];
            if (!quote.isNull()) {
                if (c == quote)
                    quote = QChar();
                continue;
            }
            if (isQuote(c)) {
                quote = c;
            } else if (c == u'[') {
                if (++depth == 1)
                    start = i + 1;
            } else if (c == u']') {
                if (--depth == 0) {
                    if (start >= 0 && !scan.listItem(line, {start, i}))
                        return false;
                    return true;
                }
            } else if (c == u',' && depth == 1) {
                if (start < 0 || !scan.listItem(line, {start, i}))
                    return false;
                start = i + 1;
            } else if (c == u'#' && (i == 0 || isSpaceOrTab(line.text[i - 1]))) {
                return false; // a comment inside the list
            }
        }
        if (!quote.isNull())
            return false; // a quoted item over two lines
        // The item continues on the next line: only an item ending here is placed.
        if (start >= 0 && !slice(line.text, trimmed(line.text, {start, line.text.size()})).isEmpty())
            return false;
    }
    return false;
}

} // namespace

namespace detail {

QList<NoteTag> propertyTagItems(QStringView value, qsizetype valueOffset, int line,
                                const std::function<bool(qsizetype)> &hashAt)
{
    QList<NoteTag> out;
    for (const Range &part : topLevelCommaRanges(value)) {
        Range r = unquoted(value, trimmed(value, part));
        if (r.first >= r.second)
            continue;
        if (value[r.first] == u'#') {
            if (hashAt && hashAt(valueOffset + r.first))
                continue; // an inline #tag: reported by the inline scan
            r = trimmed(value, {r.first + 1, r.second});
        }
        if (slice(value, r).startsWith(u"[["))
            continue; // a [[link]]: renameLinks() rewrites it
        const QString name = cleanName(slice(value, part));
        if (name.isEmpty() || slice(value, r) != name)
            continue;
        NoteTag tag;
        tag.name = name.normalized(QString::NormalizationForm_C);
        tag.from = valueOffset + r.first;
        tag.to = valueOffset + r.second;
        tag.line = line;
        tag.form = TagForm::Property;
        out.append(tag);
    }
    return out;
}

FrontmatterTags frontmatterTagItems(QStringView content)
{
    FrontmatterTags out;
    const auto fm = findFrontmatter(content);
    if (!fm) {
        out.complete = true;
        return out;
    }
    const QVariantMap data = parseYaml(fm->yaml);
    for (const QString &want : {u"tags"_s, u"tag"_s}) {
        for (auto it = data.constBegin(); it != data.constEnd() && out.key.isEmpty(); ++it) {
            if (it.key().toLower() == want)
                out.key = it.key();
        }
        if (!out.key.isEmpty())
            break;
    }
    if (out.key.isEmpty()) {
        out.complete = true;
        return out;
    }
    const QStringList expected = namesFrom(data.value(out.key), true);
    const QList<YamlLine> lines = yamlLines(fm->yaml, fm->yamlStart, 1);
    // The key's (last) line at the top level: `key:` then a blank or the end.
    qsizetype keyLine = -1;
    for (qsizetype k = 0; k < lines.size(); ++k) {
        const QStringView t = lines[k].text;
        if (t.startsWith(out.key) && charAt(t, out.key.size()) == u':'
            && (out.key.size() + 1 == t.size() || isSpaceOrTab(t[out.key.size() + 1])))
            keyLine = k;
    }
    if (keyLine < 0)
        return out;
    FrontmatterScan scan(out);
    const YamlLine &line = lines[keyLine];
    qsizetype v = out.key.size() + 1;
    while (v < line.text.size() && isSpaceOrTab(line.text[v]))
        ++v;
    const qsizetype valueEnd = yamlCommentStart(line.text, v);
    const QStringView value = trimEndBlanks(line.text.sliced(v, valueEnd - v));
    bool ok = true;
    if (value.isEmpty()) {
        // A block list on the following lines (`- a`, also at the key's indentation).
        for (qsizetype k = keyLine + 1; k < lines.size() && ok; ++k) {
            const QStringView t = lines[k].text;
            const int indent = yamlIndent(t);
            const QStringView rest = t.sliced(indent);
            if (rest.trimmed().isEmpty() || rest.startsWith(u'#'))
                continue;
            const bool item = rest == u"-" || rest.startsWith(u"- ") || rest.startsWith(u"-\t");
            if (!item) {
                ok = indent == 0; // the next key; anything else is a form not handled here
                break;
            }
            const qsizetype from = indent + 1;
            ok = scan.listItem(lines[k], {from, yamlCommentStart(t, from)});
        }
    } else if (value.startsWith(u'[')) {
        ok = scanFlow(lines, keyLine, v, scan);
    } else if (value.startsWith(u'{') || value.startsWith(u'|') || value.startsWith(u'>') || value.startsWith(u'&')
               || value.startsWith(u'*') || value.startsWith(u'!')) {
        ok = false;
    } else {
        ok = scan.scalar(line, {v, v + value.size()});
        // A plain scalar continued on the next (indented) line is not placed.
        if (ok && keyLine + 1 < lines.size() && yamlIndent(lines[keyLine + 1].text) > 0
            && !lines[keyLine + 1].text.trimmed().isEmpty())
            ok = false;
    }
    out.complete = ok && scan.m_names == expected;
    if (!out.complete)
        out.items.clear();
    return out;
}

} // namespace detail

QList<NoteTag> findNoteTags(QStringView content)
{
    QList<NoteTag> tags;
    parseNoteWith(QString(), content, VaultConfig(), StructureOptions(), &tags);
    for (const FrontmatterTagItem &item : frontmatterTagItems(content).items) {
        if (!item.link)
            tags.append(item.tag);
    }
    std::stable_sort(tags.begin(), tags.end(), [](const NoteTag &a, const NoteTag &b) { return a.from < b.from; });
    return tags;
}

} // namespace brulion::markdown
