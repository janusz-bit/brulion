#include "blocktext.h"

#include "calc/table.h"
#include "markdown/frontmatter.h"
#include "markdown/inline.h"
#include "markdown/notemode.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

QStringView firstLineOf(QStringView body)
{
    const qsizetype nl = body.indexOf(u'\n');
    return nl < 0 ? body : body.first(nl);
}

/// "# Title" / "### T" at the start of a text (ATX heading marker followed by a space or end).
int atxLevel(QStringView line)
{
    qsizetype k = 0;
    while (k < line.size() && k < 3 && line[k] == u' ')
        ++k;
    int level = 0;
    while (k < line.size() && line[k] == u'#' && level < 7) {
        ++level;
        ++k;
    }
    if (level == 0 || level > 6)
        return 0;
    if (k < line.size() && line[k] != u' ' && line[k] != u'\t')
        return 0;
    return level;
}

/// "> [!note]- Title" -> {type, folded}.
std::optional<std::pair<QString, bool>> calloutHeader(QStringView line)
{
    static const QRegularExpression re(u"^[ ]{0,3}>[ \\t]?\\[!([A-Za-z0-9_-]+)\\]([+-]?)"_s);
    const auto m = re.matchView(line);
    if (!m.hasMatch())
        return std::nullopt;
    return std::pair{m.captured(1).toLower(), m.capturedView(2) == u"-"};
}

bool allLinesQuoted(QStringView body)
{
    for (const text::Line &l : text::splitLines(body)) {
        const QString t = l.text.trimmed();
        if (!t.isEmpty() && !t.startsWith(u'>'))
            return false;
    }
    return true;
}

bool looksLikeTable(QStringView body)
{
    const QList<text::Line> lines = text::splitLines(body);
    if (lines.size() < 2)
        return false;
    if (!lines[0].text.trimmed().startsWith(u'|') && !lines[0].text.contains(u'|'))
        return false;
    return calc::parseDelimiterRow(lines[1].text).has_value() && calc::parseTable(body).has_value();
}

bool isImageExtension(QStringView target)
{
    // The same pictures paths::isPicturePath opens as a picture (PageView).
    return paths::isPicturePath(target.toString());
}

bool isAttachmentTarget(QStringView target)
{
    const qsizetype dot = target.lastIndexOf(u'.');
    if (dot < 0)
        return false;
    const QStringView ext = target.sliced(dot);
    return !ext.contains(u'/') && ext.compare(u".md", Qt::CaseInsensitive) != 0;
}

} // namespace

QString viewName(BlockView view)
{
    switch (view) {
    case BlockView::Text: return u"text"_s;
    case BlockView::Heading: return u"heading"_s;
    case BlockView::Code: return u"code"_s;
    case BlockView::Calc: return u"calc"_s;
    case BlockView::Query: return u"query"_s;
    case BlockView::Math: return u"math"_s;
    case BlockView::Table: return u"table"_s;
    case BlockView::Quote: return u"quote"_s;
    case BlockView::Callout: return u"callout"_s;
    case BlockView::Rule: return u"rule"_s;
    case BlockView::Frontmatter: return u"frontmatter"_s;
    case BlockView::Html: return u"html"_s;
    }
    return u"text"_s;
}

QString kindName(BlockKind kind)
{
    switch (kind) {
    case BlockKind::ListItem: return u"listItem"_s;
    case BlockKind::Paragraph: return u"paragraph"_s;
    case BlockKind::Heading: return u"heading"_s;
    case BlockKind::Code: return u"code"_s;
    case BlockKind::Table: return u"table"_s;
    case BlockKind::Quote: return u"quote"_s;
    case BlockKind::Callout: return u"callout"_s;
    case BlockKind::Rule: return u"rule"_s;
    case BlockKind::Html: return u"html"_s;
    case BlockKind::Frontmatter: return u"frontmatter"_s;
    }
    return u"paragraph"_s;
}

QString taskStatusName(std::optional<TaskStatus> status)
{
    if (!status)
        return {};
    switch (*status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

std::optional<TaskStatus> taskStatusFromName(QStringView name)
{
    if (name == u"todo")
        return TaskStatus::Todo;
    if (name == u"doing")
        return TaskStatus::Doing;
    if (name == u"done")
        return TaskStatus::Done;
    if (name == u"cancelled" || name == u"canceled")
        return TaskStatus::Cancelled;
    if (name == u"deferred")
        return TaskStatus::Deferred;
    return std::nullopt;
}

namespace {

bool isSpaceOrTab(QChar c)
{
    return c == u' ' || c == u'\t';
}

/// An opening code fence line (markdown's fenceOpen()): 3+ backticks or tildes after any
/// spaces and tabs; a backtick fence's info string has no backtick. Returns the fence character
/// and the run length, or nullopt.
std::optional<std::pair<QChar, int>> openingFence(QStringView line)
{
    qsizetype k = 0;
    while (k < line.size() && isSpaceOrTab(line[k]))
        ++k;
    if (k >= line.size() || (line[k] != u'`' && line[k] != u'~'))
        return std::nullopt;
    const QChar marker = line[k];
    qsizetype n = 0;
    while (k + n < line.size() && line[k + n] == marker)
        ++n;
    if (n < 3 || (marker == u'`' && line.sliced(k + n).contains(u'`')))
        return std::nullopt;
    return std::pair{marker, int(n)};
}

/// The run length when `line` is a closing fence of `marker` (markdown's isFenceClose(): spaces
/// and tabs before and after a run of 3+ markers and nothing else — a no-break space or any
/// other character makes it a code line), else 0.
int closingFenceRun(QStringView line, QChar marker)
{
    qsizetype k = 0;
    while (k < line.size() && isSpaceOrTab(line[k]))
        ++k;
    qsizetype run = 0;
    while (k + run < line.size() && line[k + run] == marker)
        ++run;
    if (run < 3)
        return 0;
    for (qsizetype j = k + run; j < line.size(); ++j) {
        if (!isSpaceOrTab(line[j]))
            return 0;
    }
    return int(run);
}

/// Lines a code block keeps hidden after its closing fence: blank lines, a standalone block id
/// (`^kod123`, Obsidian; Brulion writes it there for code blocks) and `key:: value` properties
/// (Logseq's `id:: …`, `collapsed:: true`).
bool isMetadataLine(QStringView line)
{
    qsizetype k = 0;
    while (k < line.size() && line[k].isSpace())
        ++k;
    const QStringView t = line.sliced(k);
    return t.isEmpty() || !markdown::standaloneBlockId(t).isEmpty() || markdown::matchPropertyLine(t).has_value();
}

} // namespace

std::optional<Fence> parseFence(QStringView body)
{
    const QList<text::Line> lines = text::splitLines(body);
    const QString &first = lines.first().text;
    // Brulion's parser accepts a fence at any indentation (also inside list items), unlike
    // CommonMark's three spaces; the editor has to agree or a code block would render as text.
    const auto open = openingFence(first);
    if (!open)
        return std::nullopt;
    Fence f;
    f.openLine = first;
    f.marker = open->first;
    f.length = open->second;
    qsizetype k = 0;
    while (k < first.size() && isSpaceOrTab(first[k]))
        ++k;
    const QString info = first.sliced(k + f.length).trimmed();
    f.lang = info.section(u' ', 0, 0, QString::SectionSkipEmpty).toLower();
    // Strip attribute braces of "{.lang}" style info strings.
    if (f.lang.startsWith(u'{'))
        f.lang.clear();
    f.innerOffset = int(lines.size() > 1 ? lines[1].start : first.size());
    // The first closing line ends the block, as in the core parser: lines after it are not code
    // (a block id, properties or more text of a list item).
    qsizetype close = -1;
    for (qsizetype i = 1; i < lines.size() && close < 0; ++i) {
        if (closingFenceRun(lines[i].text, f.marker) >= f.length)
            close = i;
    }
    QStringList inner;
    const qsizetype end = close >= 0 ? close : lines.size();
    for (qsizetype i = 1; i < end; ++i)
        inner.append(lines[i].text);
    f.inner = inner.join(u'\n');
    if (close >= 0) {
        f.closed = true;
        f.closeLine = lines[close].text;
        QStringList after;
        for (qsizetype i = close + 1; i < lines.size(); ++i) {
            after.append(lines[i].text);
            f.onlyMetadataAfter = f.onlyMetadataAfter && isMetadataLine(lines[i].text);
        }
        f.after = after.join(u'\n');
    }
    return f;
}

namespace {

/// A fence line rewritten to `length` markers, keeping its indentation and info string.
QString fenceLineWithLength(QStringView line, QChar marker, int length)
{
    qsizetype start = 0;
    while (start < line.size() && isSpaceOrTab(line[start]))
        ++start;
    qsizetype end = start;
    while (end < line.size() && line[end] == marker)
        ++end;
    return line.first(start).toString() + QString(length, marker) + line.sliced(end).toString();
}

/// The longest run of `marker` on a line of `inner` that would close a fence of that length or
/// shorter; the opening fence has to be longer.
int longestClosingFence(QStringView inner, QChar marker)
{
    int longest = 0;
    for (const text::Line &line : text::splitLines(inner))
        longest = std::max(longest, closingFenceRun(line.text, marker));
    return longest;
}

} // namespace

BlockShape shapeOf(const markdown::BlockEntry &e)
{
    BlockShape s;
    s.task = e.taskStatus;
    s.editText = e.body;
    s.headingLevel = e.headingLevel;
    s.lang = e.lang;
    const auto fenced = [&](const Fence &f) {
        s.lang = f.lang;
        // ```math / ```latex / ```tex: a formula block (docs/SYNTAX.md §11).
        const bool math = f.lang == u"math" || f.lang == u"latex" || f.lang == u"tex";
        s.view = math                 ? BlockView::Math
                 : f.lang == u"calc"  ? BlockView::Calc
                 : f.lang == u"query" ? BlockView::Query
                                      : BlockView::Code;
        s.editOffset = f.innerOffset;
        if (f.closed && !f.onlyMetadataAfter) {
            // More text after the closing fence (a list item with a description, a second fence):
            // hiding it would hide text, so everything after the opening line is edited as it is
            // — the closing fence included, never widened (bodyWithEditText()).
            s.editText = e.body.sliced(f.innerOffset);
            s.editSuffix = 0;
            return;
        }
        s.editText = f.inner;
        // Body = open + '\n' + inner + '\n' + close + after ("```\n```": no inner line at all);
        // a block id or properties after the closing fence stay hidden with it.
        s.editSuffix = int(e.body.size() - f.innerOffset - f.inner.size());
    };
    // A lone opening fence ("```calc" being typed) stays text until Enter turns it into a block.
    const auto blockFence = [&]() -> std::optional<Fence> {
        auto f = parseFence(e.body);
        if (f && !f->closed && !e.body.contains(u'\n'))
            return std::nullopt;
        return f;
    };
    switch (e.kind) {
    case BlockKind::Heading:
        s.view = BlockView::Heading;
        return s;
    case BlockKind::Code:
        if (const auto f = blockFence())
            fenced(*f);
        else if (parseFence(e.body))
            s.view = BlockView::Text;
        else
            s.view = BlockView::Code;
        return s;
    case BlockKind::Table: s.view = BlockView::Table; return s;
    case BlockKind::Quote: s.view = BlockView::Quote; return s;
    case BlockKind::Callout: {
        s.view = BlockView::Callout;
        if (const auto h = calloutHeader(firstLineOf(e.body))) {
            s.calloutType = h->first;
            s.calloutFolded = h->second;
        }
        return s;
    }
    case BlockKind::Rule: s.view = BlockView::Rule; return s;
    case BlockKind::Html: s.view = BlockView::Html; return s;
    case BlockKind::Frontmatter: {
        s.view = BlockView::Frontmatter;
        const QList<text::Line> lines = text::splitLines(e.body);
        if (lines.size() >= 2 && lines.first().text.trimmed() == u"---") {
            const QString last = lines.last().text.trimmed();
            const bool closed = last == u"---" || last == u"...";
            s.editOffset = int(lines.first().text.size() + 1);
            const qsizetype innerEnd = closed ? lines.last().start - (lines.size() > 2 ? 1 : 0) : e.body.size();
            if (closed && lines.size() == 2) {
                // "---\n---": empty YAML.
                s.editOffset = int(lines.first().text.size());
                s.editText.clear();
                s.editSuffix = int(e.body.size() - s.editOffset);
            } else {
                s.editText = e.body.mid(s.editOffset, innerEnd - s.editOffset);
                s.editSuffix = int(e.body.size() - s.editOffset - s.editText.size());
            }
        }
        return s;
    }
    case BlockKind::Paragraph: s.view = BlockView::Text; return s;
    case BlockKind::ListItem: break;
    }

    // List items: the body may itself be a fence, a table, a heading, a quote or a callout
    // (Logseq writes everything inside list items).
    if (const auto f = blockFence()) {
        fenced(*f);
        return s;
    }
    if (looksLikeTable(e.body)) {
        s.view = BlockView::Table;
        return s;
    }
    const QStringView first = firstLineOf(e.body);
    if (first.trimmed().startsWith(u'>') && allLinesQuoted(e.body)) {
        if (const auto h = calloutHeader(first)) {
            s.view = BlockView::Callout;
            s.calloutType = h->first;
            s.calloutFolded = h->second;
        } else {
            s.view = BlockView::Quote;
        }
        return s;
    }
    if (const auto marker = markdown::matchTaskMarker(e.body, 0)) {
        s.task = marker->status;
        s.editOffset = int(marker->contentStart);
        s.editText = e.body.sliced(marker->contentStart);
    }
    if (const int level = atxLevel(firstLineOf(s.editText)); level > 0) {
        s.view = BlockView::Heading;
        s.headingLevel = level;
    }
    return s;
}

QString bodyWithEditText(const markdown::BlockEntry &e, const QString &editText)
{
    const BlockShape s = shapeOf(e);
    if (editText == s.editText)
        return e.body; // nothing edited: no hidden byte is rewritten either
    QString head = e.body.left(s.editOffset);
    QString tail = e.body.right(s.editSuffix);
    QString out;
    if (s.view == BlockView::Code || s.view == BlockView::Calc || s.view == BlockView::Query
        || s.view == BlockView::Math || s.view == BlockView::Frontmatter) {
        // The fence lines are hidden, so every line typed between them is code. A line that would
        // close the fence (``` alone on its line, say, when writing about fenced code) must not
        // end the block: widen the opening and closing fence past the longest such line. Keeping
        // the wider fence is harmless when that line is removed again. Only a fence the editor
        // hides as closed is widened: in a fence that is not closed yet a typed closing line
        // closes it, and a body with text after its closing fence shows that fence as text.
        const auto f = parseFence(e.body);
        if (f && f->closed && f->onlyMetadataAfter) {
            const int needed = longestClosingFence(editText, f->marker) + 1;
            if (needed > f->length) {
                // head = the opening line + '\n'; tail = ['\n'] + the closing line + ['\n' + after].
                head = fenceLineWithLength(f->openLine, f->marker, needed) + head.sliced(f->openLine.size());
                if (closingFenceRun(f->closeLine, f->marker) < needed) {
                    const qsizetype at = tail.startsWith(u'\n') ? 1 : 0;
                    tail = tail.first(at) + fenceLineWithLength(f->closeLine, f->marker, needed)
                           + tail.sliced(at + f->closeLine.size());
                }
            }
        }
        // Keep the hidden fence lines on lines of their own.
        if (!head.isEmpty() && !head.endsWith(u'\n'))
            head += u'\n';
        out = head + editText;
        if (!tail.isEmpty())
            out += (tail.startsWith(u'\n') ? QString() : u"\n"_s) + tail;
    } else {
        if (!head.isEmpty() && !editText.isEmpty() && !head.endsWith(u' ') && !head.endsWith(u'\t'))
            head += u' ';
        out = head + editText + tail;
    }
    return out;
}

QString withoutTaskMarker(const markdown::BlockEntry &e)
{
    if (e.kind != BlockKind::ListItem)
        return e.body;
    if (const auto marker = markdown::matchTaskMarker(e.body, 0))
        return e.body.sliced(marker->contentStart);
    return e.body;
}

QString plainTextOf(const markdown::BlockEntry &e, int maxLength)
{
    const BlockShape s = shapeOf(e);
    QString t;
    switch (s.view) {
    case BlockView::Code:
    case BlockView::Calc:
    case BlockView::Query:
    case BlockView::Math: t = s.editText; break;
    case BlockView::Rule: return u"―"_s;
    default: t = markdown::stripTokens(s.editText); break;
    }
    // Heading markers, quote markers, link brackets.
    static const QRegularExpression heading(u"^\\s{0,3}#{1,6}\\s+"_s);
    static const QRegularExpression quote(u"(^|\\n)\\s*>\\s?(\\[![^\\]]*\\][+-]?\\s*)?"_s);
    static const QRegularExpression wikiAlias(u"!?\\[\\[(?:[^\\]|]*\\|)?([^\\]]*)\\]\\]"_s);
    static const QRegularExpression mdLink(u"!?\\[([^\\]]*)\\]\\([^)]*\\)"_s);
    static const QRegularExpression emphasis(u"(\\*\\*|__|~~|==|`)"_s);
    static const QRegularExpression props(u"(^|\\n)[A-Za-z0-9_-]+::[^\\n]*"_s);
    t.replace(heading, QString());
    t.replace(quote, u"\\1"_s);
    t.replace(wikiAlias, u"\\1"_s);
    t.replace(mdLink, u"\\1"_s);
    t.replace(emphasis, QString());
    t.replace(props, u"\\1"_s);
    t = t.simplified();
    if (maxLength > 0 && t.size() > maxLength)
        t = t.left(maxLength - 1).trimmed() + u'…';
    return t;
}

// ---------------------------------------------------------------------------------------------

QVariantMap queryInfo(QStringView source)
{
    QVariantList filters;
    QStringList terms;
    QString current;
    bool quoted = false;
    QStringList words;
    for (const QChar c : source) {
        if (c == u'"') {
            quoted = !quoted;
            current += c;
        } else if (!quoted && (c == u' ' || c == u'\t' || c == u'\n' || c == u'\r')) {
            if (!current.isEmpty())
                words.append(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.isEmpty())
        words.append(current);
    static const QRegularExpression filterRe(u"^(-?)([A-Za-z]+):(.+)$"_s);
    for (const QString &w : std::as_const(words)) {
        const auto m = filterRe.match(w);
        if (m.hasMatch()) {
            filters.append(QVariantMap{{u"key"_s, m.captured(2).toLower()},
                                       {u"value"_s, m.captured(3)},
                                       {u"negated"_s, !m.capturedView(1).isEmpty()}});
        } else {
            QString t = w;
            if (t.size() >= 2 && t.startsWith(u'"') && t.endsWith(u'"'))
                t = t.sliced(1, t.size() - 2);
            terms.append(t);
        }
    }
    return {{u"source"_s, source.toString()}, {u"filters"_s, filters}, {u"terms"_s, terms}};
}

QVariantList frontmatterProperties(const QString &body)
{
    QVariantList out;
    const auto valueText = [](const QVariant &v) -> QString {
        if (v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QStringList) {
            QStringList parts;
            for (const QVariant &x : v.toList())
                parts.append(x.toString());
            return parts.join(u", ");
        }
        if (v.typeId() == QMetaType::QVariantMap)
            return u"…"_s;
        return v.toString();
    };
    if (body.trimmed().startsWith(u"---")) {
        QString content = body;
        if (!content.endsWith(u'\n'))
            content += u'\n';
        const markdown::FrontmatterResult fm = markdown::parseFrontmatter(content);
        for (const QString &key : fm.keys) {
            // The note mode is the page header's mode toggle, not a property to show.
            if (key == markdown::NoteModeKey)
                continue;
            out.append(QVariantMap{{u"key"_s, key}, {u"value"_s, valueText(fm.data.value(key))}});
        }
        return out;
    }
    for (const text::Line &l : text::splitLines(body)) {
        const QString line = l.text.trimmed();
        if (const auto pm = markdown::matchPropertyLine(line)) {
            out.append(QVariantMap{{u"key"_s, pm->key},
                                   {u"value"_s, line.mid(pm->valueFrom, pm->valueTo - pm->valueFrom)}});
        }
    }
    return out;
}

bool onlyNoteModeKey(const QString &body)
{
    if (!body.trimmed().startsWith(u"---"))
        return false;
    QString content = body;
    if (!content.endsWith(u'\n'))
        content += u'\n';
    const auto fm = markdown::findFrontmatter(content);
    if (!fm)
        return false;
    // One line `brulion: …` at column 0 and blank lines: a comment, a nested value or text the
    // YAML parser skips is the user's and keeps the block visible.
    int lines = 0;
    for (const text::Line &l : text::splitLines(fm->yaml)) {
        if (l.text.trimmed().isEmpty())
            continue;
        if (++lines > 1 || l.text.front().isSpace())
            return false;
    }
    QStringList keys;
    markdown::parseYaml(fm->yaml, &keys);
    return keys == QStringList{markdown::NoteModeKey.toString()};
}

QList<Embed> embedsOf(QStringView body)
{
    QList<Embed> out;
    const markdown::InlineScan scan = markdown::extractInline(body);
    for (const Link &link : scan.links) {
        if (link.kind == LinkKind::Embed && isAttachmentTarget(link.target)) {
            out.append({link.target, link.display, isImageExtension(link.target)});
        } else if (link.kind == LinkKind::Markdown && link.from >= 0 && link.from < body.size()
                   && body[link.from] == u'!' && !link.target.contains(u"://")) {
            out.append({link.target, link.display, isImageExtension(link.target)});
        }
    }
    return out;
}

QList<Transclusion> transclusionsOf(QStringView body)
{
    QList<Transclusion> out;
    if (!body.contains(u"![[") && !body.contains(u"!(("))
        return out;
    const markdown::InlineScan scan = markdown::extractInline(body);
    for (const Link &link : scan.links) {
        Transclusion t;
        if (link.kind == LinkKind::BlockEmbed) {
            if (link.blockId.isEmpty())
                continue;
            t.kind = Transclusion::Kind::Block;
            t.blockId = link.blockId;
            t.blockRef = true;
        } else if (link.kind == LinkKind::Embed && !isAttachmentTarget(link.target)) {
            t.target = link.target;
            t.display = link.display;
            if (!link.blockId.isEmpty()) {
                t.kind = Transclusion::Kind::Block;
                t.blockId = link.blockId;
            } else if (!link.heading.isEmpty()) {
                t.kind = Transclusion::Kind::Heading;
                t.heading = link.heading;
            } else if (link.target.isEmpty()) {
                continue; // "![[#]]"
            }
        } else {
            continue;
        }
        out.append(t);
    }
    return out;
}

bool onlyEmbeds(QStringView body, bool transclusions)
{
    if (!body.contains(u"![") && !body.contains(u"!(("))
        return false; // no embed at all: no need to scan (called for every rendered block)
    const markdown::InlineScan scan = markdown::extractInline(body);
    QString rest = body.toString();
    bool any = false;
    // Blank out the embeds from the end so offsets stay valid.
    QList<Link> links = scan.links;
    std::sort(links.begin(), links.end(), [](const Link &a, const Link &b) { return a.from > b.from; });
    for (const Link &link : std::as_const(links)) {
        const bool attachment = (link.kind == LinkKind::Embed && isAttachmentTarget(link.target))
                                || (link.kind == LinkKind::Markdown && link.from < body.size() && body[link.from] == u'!');
        const bool transclusion = transclusions
                                  && ((link.kind == LinkKind::Embed && !isAttachmentTarget(link.target)
                                       && (!link.target.isEmpty() || !link.heading.isEmpty() || !link.blockId.isEmpty()))
                                      || link.kind == LinkKind::BlockEmbed);
        if ((!attachment && !transclusion) || link.from < 0 || link.to > rest.size())
            continue;
        rest.remove(link.from, link.to - link.from);
        any = true;
    }
    return any && markdown::stripTokens(rest).trimmed().isEmpty();
}

// ---------------------------------------------------------------------------------------------

QString replaceTokenRange(const QString &body, qsizetype from, qsizetype to, const QString &token)
{
    if (from < 0 || to < from || to > body.size())
        return body;
    if (!token.isEmpty())
        return body.left(from) + token + body.mid(to);
    // Remove with one adjacent space.
    qsizetype a = from;
    qsizetype b = to;
    if (a > 0 && (body[a - 1] == u' ' || body[a - 1] == u'\t'))
        --a;
    else if (b < body.size() && (body[b] == u' ' || body[b] == u'\t'))
        ++b;
    return body.left(a) + body.mid(b);
}

std::optional<std::pair<qsizetype, qsizetype>> findFirstLineToken(const QString &body, markdown::TokenKind kind)
{
    const QString first = firstLineOf(body).toString();
    const QString masked = markdown::maskInlineCode(first);
    for (const markdown::Token &t : markdown::findTokens(masked)) {
        if (t.kind == kind)
            return std::pair{t.from, t.to};
    }
    return std::nullopt;
}

QString setFirstLineToken(const QString &body, markdown::TokenKind kind, const QString &token, bool append)
{
    if (!append) {
        if (const auto range = findFirstLineToken(body, kind))
            return replaceTokenRange(body, range->first, range->second, token);
    }
    if (token.isEmpty())
        return body;
    const qsizetype nl = body.indexOf(u'\n');
    const QString first = nl < 0 ? body : body.left(nl);
    const QString rest = nl < 0 ? QString() : body.mid(nl);
    qsizetype at = first.size();
    // Before a trailing block id (" ^abc").
    if (const auto id = markdown::blockIdAtEnd(first))
        at = id->from;
    // Before trailing whitespace.
    while (at > 0 && (first[at - 1] == u' ' || first[at - 1] == u'\t'))
        --at;
    QString before = first.left(at);
    const QString after = first.mid(at);
    const bool needSpace = !before.isEmpty() && !before.endsWith(u' ') && !before.endsWith(u'\t');
    QString line = before + (needSpace ? u" "_s : QString()) + token;
    if (!after.isEmpty() && !after.startsWith(u' ') && !after.startsWith(u'\t'))
        line += u' ';
    line += after;
    return line + rest;
}

QString insertTokenAt(const QString &text, int cursor, const QString &token, int *newCursor)
{
    const int c = std::clamp(cursor, 0, int(text.size()));
    const QString before = text.left(c);
    const QString after = text.mid(c);
    const bool spaceBefore = !before.isEmpty() && !before.back().isSpace();
    const bool spaceAfter = after.isEmpty() || !after.front().isSpace();
    const QString inserted = (spaceBefore ? u" "_s : QString()) + token + (spaceAfter ? u" "_s : QString());
    if (newCursor)
        *newCursor = int(before.size() + inserted.size());
    return before + inserted + after;
}

// ---------------------------------------------------------------------------------------------

QVariantMap CompletionTrigger::toMap() const
{
    return {{u"mode"_s, mode}, {u"filter"_s, filter}, {u"from"_s, from},
            {u"to"_s, to},     {u"token"_s, token},   {u"closing"_s, closing}};
}

CompletionTrigger CompletionTrigger::fromMap(const QVariantMap &map)
{
    CompletionTrigger t;
    t.mode = map.value(u"mode"_s).toString();
    t.filter = map.value(u"filter"_s).toString();
    t.from = map.value(u"from"_s, -1).toInt();
    t.to = map.value(u"to"_s, -1).toInt();
    t.token = map.value(u"token"_s).toString();
    t.closing = map.value(u"closing"_s).toString();
    return t;
}

CompletionTrigger completionTriggerAt(QStringView text, int cursor)
{
    CompletionTrigger none;
    if (cursor < 0 || cursor > text.size())
        return none;
    const qsizetype lineStart = text.first(cursor).lastIndexOf(u'\n') + 1;
    const QStringView before = text.sliced(lineStart, cursor - lineStart);
    qsizetype lineEnd = text.indexOf(u'\n', cursor);
    if (lineEnd < 0)
        lineEnd = text.size();
    const QStringView after = text.sliced(cursor, lineEnd - cursor);

    // Nothing inside inline code: the masked line differs from the line at the cursor.
    {
        const QString lineText = text.sliced(lineStart, lineEnd - lineStart).toString();
        const QString masked = markdown::maskInlineCode(lineText);
        const qsizetype k = cursor - lineStart - 1;
        if (k >= 0 && masked[k] != lineText[k])
            return none;
        if (k >= 0 && lineText.left(k + 1).count(u'`') % 2 == 1)
            return none;
    }

    const auto result = [&](const QString &mode, qsizetype fromInLine, qsizetype filterStart, const QString &token) {
        CompletionTrigger t;
        t.mode = mode;
        t.from = int(lineStart + fromInLine);
        t.filter = before.sliced(filterStart).toString();
        t.to = cursor;
        t.token = token;
        return t;
    };

    // [[pages / ![[embeds
    {
        static const QRegularExpression re(u"(!?)\\[\\[([^\\[\\]|\\n]*)$"_s);
        const auto m = re.matchView(before);
        if (m.hasMatch()) {
            CompletionTrigger t = result(u"pages"_s, m.capturedStart(2), m.capturedStart(2),
                                         m.capturedView(1).isEmpty() ? u"[["_s : u"![["_s);
            static const QRegularExpression rest(u"^([^\\[\\]|\\n]*)(\\]\\]|\\|)"_s);
            const auto r = rest.matchView(after);
            if (r.hasMatch()) {
                t.to = int(cursor + r.capturedLength(1));
                t.closing = r.captured(2);
            }
            return t;
        }
    }
    // ((block refs
    {
        static const QRegularExpression re(u"(!?)\\(\\(([^()\\n]*)$"_s);
        const auto m = re.matchView(before);
        if (m.hasMatch()) {
            const qsizetype open = m.capturedStart(0);
            const bool afterWord = open > 0 && text::isWordChar(before[open - 1]);
            if (!afterWord || !m.capturedView(2).isEmpty()) {
                CompletionTrigger t = result(u"blocks"_s, m.capturedStart(2), m.capturedStart(2),
                                             m.capturedView(1).isEmpty() ? u"(("_s : u"!(("_s);
                static const QRegularExpression rest(u"^([^()\\n]*)(\\)\\))"_s);
                const auto r = rest.matchView(after);
                if (r.hasMatch()) {
                    t.to = int(cursor + r.capturedLength(1));
                    t.closing = r.captured(2);
                }
                return t;
            }
        }
    }
    // Natural-language dates after a date token.
    {
        static const QRegularExpression re = text::unicodeRegex(
                u"(📅|⏳|🛫|⏰!?)\\x{FE0F}?[ \\t]+([^\\n📅⏳🛫⏰✅❌➕🔁]*)$"_s);
        const auto m = re.matchView(before);
        if (m.hasMatch()) {
            static const QRegularExpression canonical(u"^\\d{4}-\\d{2}-\\d{2}"_s);
            if (!canonical.matchView(m.capturedView(2)).hasMatch())
                return result(u"date"_s, m.capturedStart(2), m.capturedStart(2), m.captured(1));
            return none;
        }
    }
    // @date
    {
        static const QRegularExpression re = text::unicodeRegex(u"(?:^|[\\s(])@([^\\s@][^@\\n]*)?$"_s);
        const auto m = re.matchView(before);
        if (m.hasMatch()) {
            const qsizetype at = before.lastIndexOf(u'@');
            return result(u"date"_s, at, at + 1, u"@"_s);
        }
    }
    // #tags
    {
        static const QRegularExpression re = text::unicodeRegex(u"(?:^|[\\s(])#([\\p{L}\\p{N}\\p{M}_\\-/]*)$"_s);
        const auto m = re.matchView(before);
        if (m.hasMatch()) {
            const qsizetype hash = m.capturedStart(1) - 1;
            const bool atBlockStart = lineStart + hash == 0;
            if (!(atBlockStart && m.capturedLength(1) == 0)) {
                CompletionTrigger t = result(u"tags"_s, m.capturedStart(1), m.capturedStart(1), u"#"_s);
                static const QRegularExpression rest = text::unicodeRegex(u"^[\\p{L}\\p{N}\\p{M}_\\-/]*"_s);
                t.to = int(cursor + rest.matchView(after).capturedLength(0));
                return t;
            }
        }
    }
    // /slash
    {
        static const QRegularExpression re = text::unicodeRegex(u"(?:^|\\s)/([\\p{L}\\p{N}\\p{M}]*)$"_s);
        const auto m = re.matchView(before);
        if (m.hasMatch())
            return result(u"slash"_s, m.capturedStart(1) - 1, m.capturedStart(1), u"/"_s);
    }
    return none;
}

VaultConfig vaultConfigFromMap(const QVariantMap &m)
{
    VaultConfig c;
    if (m.contains(u"journalsFolder"_s))
        c.journalsFolder = m.value(u"journalsFolder"_s).toString();
    const QString fmt = m.value(u"journalFileFormat"_s).toString();
    if (!fmt.isEmpty())
        c.journalFileFormat = fmt;
    if (m.contains(u"newPagesFolder"_s))
        c.newPagesFolder = m.value(u"newPagesFolder"_s).toString();
    const QString att = m.value(u"attachmentsFolder"_s).toString();
    if (!att.isEmpty())
        c.attachmentsFolder = att;
    const QString templates = m.value(u"templatesFolder"_s).toString();
    if (!templates.isEmpty())
        c.templatesFolder = templates;
    const QTime t = QTime::fromString(m.value(u"defaultReminderTime"_s).toString(), u"HH:mm"_s);
    if (t.isValid())
        c.defaultReminderTime = t;
    const int ws = m.value(u"weekStartsOn"_s).toInt();
    if (ws >= 1 && ws <= 7)
        c.weekStartsOn = Qt::DayOfWeek(ws);
    if (const auto mode = markdown::parseNoteMode(m.value(u"journalMode"_s).toString()))
        c.journalMode = *mode;
    if (const auto mode = markdown::parseNoteMode(m.value(u"pageMode"_s).toString()))
        c.pageMode = *mode;
    return c;
}

// ---------------------------------------------------------------------------------------------

const QList<SlashCommand> &slashCommands()
{
    // A table: one command per entry (clang-format would put every field on its own line).
    // clang-format off
    static const QList<SlashCommand> commands{
            {u"task"_s, u"Zadanie"_s, u"Task"_s, u"square-check-big"_s, u"[ ]"_s, u"- [ ] zadanie"_s, u"- [ ] task"_s,
             {u"todo"_s, u"checkbox"_s, u"zrobić"_s}},
            {u"reminder"_s, u"Przypomnienie…"_s, u"Reminder…"_s, u"bell"_s, u"⏰"_s,
             u"⏰ data godzina"_s, u"⏰ date time"_s,
             {u"przypomnij"_s, u"powiadomienie"_s, u"remind"_s, u"notification"_s}},
            {u"alarm"_s, u"Alarm…"_s, u"Alarm…"_s, u"alarm-clock"_s, u"⏰!"_s, u"⏰! data godzina"_s, u"⏰! date time"_s,
             {u"budzik"_s, u"głośny"_s, u"wake"_s}},
            {u"due"_s, u"Termin…"_s, u"Due date…"_s, u"calendar"_s, u"📅"_s, u"📅 data"_s, u"📅 date"_s,
             {u"deadline"_s, u"termin"_s, u"due"_s}},
            {u"scheduled"_s, u"Zaplanuj…"_s, u"Scheduled…"_s, u"hourglass"_s, u"⏳"_s, u"⏳ data"_s, u"⏳ date"_s,
             {u"zaplanowane"_s, u"schedule"_s, u"plan"_s}},
            {u"recurrence"_s, u"Powtarzaj…"_s, u"Repeat…"_s, u"repeat"_s, u"🔁"_s, u"🔁 co tydzień"_s, u"🔁 every week"_s,
             {u"powtarzanie"_s, u"cykliczne"_s, u"co tydzień"_s, u"recurring"_s, u"every"_s}},
            {u"today"_s, u"Dziś"_s, u"Today"_s, u"calendar-check"_s, u"[[…]]"_s,
             u"[[dzisiejsza data]]"_s, u"[[today's date]]"_s,
             {u"dzisiaj"_s, u"dziennik"_s, u"journal"_s}},
            {u"tomorrow"_s, u"Jutro"_s, u"Tomorrow"_s, u"calendar-plus"_s, u"[[…]]"_s,
             u"[[jutrzejsza data]]"_s, u"[[tomorrow's date]]"_s,
             {u"jutro"_s}},
            {u"date"_s, u"Data…"_s, u"Date…"_s, u"calendar-days"_s, u"[[…]]"_s, u"[[data]]"_s, u"[[date]]"_s,
             {u"kalendarz"_s, u"dzień"_s, u"calendar"_s, u"day"_s}},
            {u"table"_s, u"Tabela"_s, u"Table"_s, u"table"_s, u"| |"_s,
             u"| kolumna | kolumna |"_s, u"| column | column |"_s,
             {u"tabelka"_s, u"grid"_s, u"arkusz"_s, u"spreadsheet"_s}},
            {u"calc"_s, u"Obliczenia"_s, u"Calculation"_s, u"calculator"_s, u"```calc"_s,
             u"blok ```calc"_s, u"```calc block"_s,
             {u"kalkulator"_s, u"liczenie"_s, u"oblicz"_s}},
            {u"math"_s, u"Wzór matematyczny"_s, u"Math formula"_s, u"sigma"_s, u"```math"_s,
             u"blok ```math (LaTeX)"_s, u"```math block (LaTeX)"_s,
             {u"wzór"_s, u"równanie"_s, u"latex"_s, u"formuła"_s, u"math"_s, u"matematyka"_s,
              u"formula"_s, u"equation"_s, u"tex"_s}},
            {u"query"_s, u"Zapytanie"_s, u"Query"_s, u"filter"_s, u"```query"_s,
             u"blok ```query"_s, u"```query block"_s,
             {u"kwerenda"_s, u"wyszukaj"_s, u"lista zadań"_s, u"filtr"_s, u"search"_s}},
            {u"h1"_s, u"Nagłówek 1"_s, u"Heading 1"_s, u"heading"_s, u"#"_s, u"# Nagłówek"_s, u"# Heading"_s,
             {u"tytuł"_s, u"title"_s, u"h1"_s}},
            {u"h2"_s, u"Nagłówek 2"_s, u"Heading 2"_s, u"heading"_s, u"##"_s, u"## Nagłówek"_s, u"## Heading"_s,
             {u"h2"_s}},
            {u"h3"_s, u"Nagłówek 3"_s, u"Heading 3"_s, u"heading"_s, u"###"_s, u"### Nagłówek"_s, u"### Heading"_s,
             {u"h3"_s}},
            {u"quote"_s, u"Cytat"_s, u"Quote"_s, u"quote"_s, u">"_s, u"> cytat"_s, u"> quote"_s, {u"blockquote"_s}},
            {u"callout"_s, u"Callout"_s, u"Callout"_s, u"info"_s, u"> [!note]"_s,
             u"> [!note] tytuł"_s, u"> [!note] title"_s,
             {u"ramka"_s, u"notatka"_s, u"admonition"_s, u"note"_s, u"wskazówka"_s, u"tip"_s}},
            {u"code"_s, u"Kod"_s, u"Code block"_s, u"code"_s, u"```"_s, u"```język"_s, u"```language"_s,
             {u"snippet"_s, u"fence"_s, u"program"_s}},
            {u"template"_s, u"Szablon…"_s, u"Template…"_s, u"sparkles"_s, u"{{ }}"_s,
             u"z folderu szablonów"_s, u"from the templates folder"_s,
             {u"wzór"_s, u"szablony"_s}},
            {u"image"_s, u"Obraz…"_s, u"Image…"_s, u"image"_s, u"![[…]]"_s, u"![[obraz.png]]"_s, u"![[image.png]]"_s,
             {u"zdjęcie"_s, u"obrazek"_s, u"foto"_s, u"grafika"_s, u"załącznik"_s, u"plik"_s, u"picture"_s,
              u"photo"_s, u"attachment"_s, u"file"_s}},
            {u"embed"_s, u"Osadź"_s, u"Embed"_s, u"copy"_s, u"![[ ]]"_s, u"![[Strona]]"_s, u"![[Page]]"_s,
             {u"osadź stronę"_s, u"sekcja"_s, u"transkluzja"_s, u"transclude"_s, u"section"_s, u"page"_s}},
            {u"blockRef"_s, u"Odwołanie do bloku"_s, u"Block reference"_s, u"link"_s, u"(( ))"_s,
             u"((id bloku))"_s, u"((block id))"_s,
             {u"blok"_s, u"odwołanie"_s, u"reference"_s, u"ref"_s}},
            {u"priorityHigh"_s, u"Priorytet wysoki"_s, u"High priority"_s, u"arrow-up"_s, u"⏫"_s,
             u"⏫ przy zadaniu"_s, u"⏫ on a task"_s,
             {u"pilne"_s, u"ważne"_s, u"urgent"_s, u"priority"_s}},
            {u"priorityLow"_s, u"Priorytet niski"_s, u"Low priority"_s, u"arrow-down"_s, u"🔽"_s,
             u"🔽 przy zadaniu"_s, u"🔽 on a task"_s,
             {u"priority"_s}},
            {u"rule"_s, u"Linia pozioma"_s, u"Divider"_s, u"minus"_s, u"---"_s, u"---"_s, u"---"_s,
             {u"separator"_s, u"linia"_s, u"hr"_s, u"rule"_s}},
            {u"time"_s, u"Aktualna godzina"_s, u"Current time"_s, u"clock"_s, u"HH:mm"_s, u"GG:MM"_s, u"HH:MM"_s,
             {u"godzina"_s, u"teraz"_s, u"now"_s}},
    };
    // clang-format on
    return commands;
}

const SlashCommand *slashCommand(QStringView id)
{
    for (const SlashCommand &c : slashCommands()) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

QList<const SlashCommand *> filterSlashCommands(QStringView filter)
{
    const QString q = text::foldKey(filter).trimmed();
    QList<std::pair<int, const SlashCommand *>> ranked;
    int order = 0;
    for (const SlashCommand &c : slashCommands()) {
        ++order;
        if (q.isEmpty()) {
            ranked.append({order, &c});
            continue;
        }
        int best = -1;
        const auto consider = [&](const QString &name, int base) {
            const QString f = text::foldKey(name);
            int rank = -1;
            if (f.startsWith(q))
                rank = 0;
            else if (f.contains(u' ' + q) || f.contains(u'-' + q))
                rank = 1;
            else if (f.contains(q))
                rank = 2;
            if (rank >= 0) {
                rank += base;
                if (best < 0 || rank < best)
                    best = rank;
            }
        };
        consider(c.namePl, 0);
        consider(c.nameEn, 0);
        consider(c.id, 0);
        for (const QString &k : c.keywords)
            consider(k, 3);
        if (best >= 0)
            ranked.append({best * 1000 + order, &c});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<const SlashCommand *> out;
    for (const auto &r : std::as_const(ranked))
        out.append(r.second);
    return out;
}

QString slashTemplate(QStringView id, bool english, int *cursor)
{
    QString text;
    int c = 0;
    if (id == u"table") {
        const QString a = english ? u"Column 1"_s : u"Kolumna 1"_s;
        const QString b = english ? u"Column 2"_s : u"Kolumna 2"_s;
        text = u"| "_s + a + u" | "_s + b + u" |\n| --- | --- |\n|  |  |"_s;
        c = 2;
    } else if (id == u"calc") {
        text = u"```calc\n\n```"_s;
        c = 8;
    } else if (id == u"math") {
        text = u"```math\n\n```"_s;
        c = 8;
    } else if (id == u"query") {
        text = u"```query\ntype:tasks status:open\n```"_s;
        c = 9;
    } else if (id == u"code") {
        text = u"```\n\n```"_s;
        c = 4;
    } else if (id == u"rule") {
        text = u"---"_s;
        c = 3;
    }
    if (cursor)
        *cursor = c;
    return text;
}

// ---------------------------------------------------------------------------------------------

namespace {

/// Lengths of the common prefix and (non-overlapping) common suffix of two texts.
std::pair<qsizetype, qsizetype> commonEnds(QStringView a, QStringView b)
{
    const qsizetype max = std::min(a.size(), b.size());
    qsizetype p = 0;
    while (p < max && a[p] == b[p])
        ++p;
    qsizetype s = 0;
    while (s < max - p && a[a.size() - 1 - s] == b[b.size() - 1 - s])
        ++s;
    return {p, s};
}

} // namespace

int snapToCharacter(QStringView text, int pos)
{
    pos = std::clamp(pos, 0, int(text.size()));
    if (pos > 0 && pos < text.size() && text[pos].isLowSurrogate() && text[pos - 1].isHighSurrogate())
        ++pos;
    return pos;
}

QString restoreLineEndings(const QString &original, const QString &edited, const QString &eol)
{
    // `original` as a TextEdit shows it, and where each of its characters comes from.
    QString shown;
    shown.reserve(original.size());
    QList<qsizetype> source;
    source.reserve(original.size() + 1);
    for (qsizetype i = 0; i < original.size(); ++i) {
        source.append(i);
        if (original[i] == u'\r') {
            shown.append(u'\n');
            if (i + 1 < original.size() && original[i + 1] == u'\n')
                ++i;
        } else {
            shown.append(original[i]);
        }
    }
    source.append(original.size());
    if (shown == edited)
        return original;
    const auto [p, s] = commonEnds(shown, edited);
    QString middle = edited.mid(p, edited.size() - p - s);
    if (eol != u"\n") {
        // Bare line feeds only (an `edited` with its own "\r\n" keeps them).
        QString out;
        out.reserve(middle.size() + middle.size() / 16);
        for (qsizetype i = 0; i < middle.size(); ++i) {
            if (middle[i] == u'\n' && (i == 0 ? p == 0 || edited[p - 1] != u'\r' : middle[i - 1] != u'\r'))
                out += eol;
            else
                out += middle[i];
        }
        middle = out;
    }
    return original.left(source[p]) + middle + original.mid(source[shown.size() - s]);
}

TextDelta TextDelta::between(const QString &before, const QString &after)
{
    const auto [p, s] = commonEnds(before, after);
    TextDelta d;
    d.from = p;
    d.removed = before.mid(p, before.size() - p - s);
    d.inserted = after.mid(p, after.size() - p - s);
    d.beforeSize = before.size();
    d.afterSize = after.size();
    return d;
}

std::optional<TextDelta> TextDelta::compose(const TextDelta &first, const TextDelta &second, const QString &middle)
{
    const std::optional<QString> a = first.revert(middle);
    const std::optional<QString> c = second.apply(middle);
    if (!a || !c)
        return std::nullopt;
    return between(*a, *c);
}

std::optional<QString> TextDelta::apply(const QString &before) const
{
    if (before.size() != beforeSize || QStringView(before).sliced(from, removed.size()) != removed)
        return std::nullopt;
    return before.left(from) + inserted + before.mid(from + removed.size());
}

std::optional<QString> TextDelta::revert(const QString &after) const
{
    if (after.size() != afterSize || QStringView(after).sliced(from, inserted.size()) != inserted)
        return std::nullopt;
    return after.left(from) + removed + after.mid(from + inserted.size());
}

} // namespace brulion::editor
