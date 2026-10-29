#include "markdown/parse.h"
#include "markdown/parse_p.h"

#include "dates/recurrence.h"
#include "markdown/frontmatter.h"
#include "markdown/inline_p.h"
#include "markdown/journal.h"
#include "markdown/properties.h"
#include "markdown/structure_p.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "markdown/tokens_p.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// How a block line is used after classification.
enum class LineClass { Text, Property, Planning, Drawer, Code, Blank, Meta };

/// Per-line analysis of a block.
struct LineInfo {
    const OwnLine *own = nullptr;
    LineClass cls = LineClass::Text;
    QString masked; ///< inline-masked text (Text lines only, same length as the view)
    QList<Token> tokens;
    QList<Range> extra; ///< ranges (line coordinates) removed from the text besides tokens
};

/// Shared per-note state while building blocks.
struct Ctx {
    const Structure *st = nullptr;
    QTime defaultTime;
    QList<Link> links;
    QList<NoteTag> *tags = nullptr; ///< collects tag occurrences (findNoteTags()) when set
};

bool isPlanningLine(QStringView content)
{
    // ^[ \t]*(?:(?:SCHEDULED|DEADLINE):[ \t]*<[^>\r\n]*>[ \t]*)+$
    qsizetype i = 0;
    int items = 0;
    for (;;) {
        while (isSpaceOrTab(charAt(content, i)))
            ++i;
        if (i == content.size())
            return items > 0;
        const QStringView rest = content.sliced(i);
        if (rest.startsWith(u"SCHEDULED:"))
            i += 10;
        else if (rest.startsWith(u"DEADLINE:"))
            i += 9;
        else
            return false;
        while (isSpaceOrTab(charAt(content, i)))
            ++i;
        if (charAt(content, i) != u'<')
            return false;
        const qsizetype close = content.indexOf(u'>', i);
        if (close < 0)
            return false;
        i = close + 1;
        ++items;
    }
}

/// Classify own lines: Logseq drawers (`:LOGBOOK:` … `:END:`) and roles.
QList<LineInfo> classifyLines(const Structure &st, const Node &node)
{
    QList<LineInfo> infos;
    infos.reserve(node.own.size());
    const bool drawers = node.kind == BlockKind::ListItem || node.kind == BlockKind::Paragraph;
    bool inDrawer = false;
    for (qsizetype k = 0; k < node.own.size(); ++k) {
        const OwnLine &l = node.own[k];
        LineInfo info;
        info.own = &l;
        switch (l.role) {
        case LineRole::Fence:
        case LineRole::Code: info.cls = LineClass::Code; break;
        case LineRole::Blank: info.cls = LineClass::Blank; break;
        case LineRole::Meta: info.cls = LineClass::Meta; break;
        case LineRole::Text: {
            info.cls = LineClass::Text;
            const QStringView m = st.maskedText(l.view);
            if (drawers && (inDrawer || (k > 0 && m.contains(u':')))) {
                const QStringView content = m.sliced(std::min(l.start, m.size()));
                if (inDrawer) {
                    info.cls = LineClass::Drawer;
                    if (isDrawerEnd(content))
                        inDrawer = false;
                } else if (isDrawerStart(content) && !isDrawerEnd(content)) {
                    info.cls = LineClass::Drawer;
                    inDrawer = true;
                }
            }
            break;
        }
        }
        infos.append(std::move(info));
    }
    return infos;
}

/// Masked text of a line for inline scanning: container syntax before the block's content
/// (indentation, list marker, quote marker) is blanked, so `>#tag` is a tag like `> #tag`.
QString scanText(const Structure &st, const OwnLine &own)
{
    const QStringView m = st.maskedText(own.view);
    const qsizetype start = std::min(own.start, m.size());
    return start > 0 ? QString(start, u' ') + m.sliced(start) : m.toString();
}

struct ScannedText {
    QString orig;
    QString masked;
    Segments seg;
    QList<LineInfo *> lines; ///< the scanned lines, in the order of `seg`
};

/// Store each scanned line's part of `masked` (the joined text, masked) as its masked text.
void setLineMasks(const ScannedText &scan, const QString &masked)
{
    for (qsizetype k = 0; k < scan.lines.size(); ++k)
        scan.lines[k]->masked = masked.sliced(scan.seg.pos[k], scan.lines[k]->own->view.length);
}

/// Join the scanned lines, mask inline code across them and store each line's masked text.
ScannedText maskScannedLines(const Structure &st, QList<LineInfo> &infos)
{
    ScannedText out;
    qsizetype pos = 0;
    QString maskedJoined;
    QList<LineInfo *> &scanned = out.lines;
    for (LineInfo &info : infos) {
        if (info.cls != LineClass::Text)
            continue;
        const LineView &v = info.own->view;
        if (!scanned.isEmpty()) {
            out.orig += u'\n';
            maskedJoined += u'\n';
        }
        out.seg.pos.append(pos);
        out.seg.offset.append(v.offset);
        out.seg.line.append(v.line);
        out.orig += st.text(v);
        maskedJoined += scanText(st, *info.own);
        scanned.append(&info);
        pos += v.length + 1;
    }
    out.masked = maskInline(maskedJoined);
    setLineMasks(out, out.masked);
    return out;
}

/// The formulas of a block's scanned text (findMathSpans(), SYNTAX §11) as the editor finds them
/// in the block's body: where the scan leaves lines out (blank lines, code, drawers), the body has
/// lines that end an inline formula like a blank line, so a blank line stands in for them.
QList<MathSpan> blockFormulas(const ScannedText &scan)
{
    if (!scan.masked.contains(u'$'))
        return {};
    QString text;
    QList<qsizetype> added; // positions in `text` of the line breaks added for left-out lines
    for (qsizetype k = 0; k < scan.seg.pos.size(); ++k) {
        if (k > 0 && scan.seg.line[k] != scan.seg.line[k - 1] + 1) {
            added.append(text.size());
            text += u'\n';
        }
        const qsizetype from = scan.seg.pos[k];
        const qsizetype to = k + 1 < scan.seg.pos.size() ? scan.seg.pos[k + 1] : scan.masked.size();
        text += QStringView(scan.masked).sliced(from, to - from);
    }
    QList<MathSpan> spans = findMathSpans(added.isEmpty() ? QStringView(scan.masked) : QStringView(text));
    if (added.isEmpty())
        return spans;
    const auto back = [&added](qsizetype p) {
        return p - (std::lower_bound(added.cbegin(), added.cend(), p) - added.cbegin());
    };
    for (MathSpan &s : spans) {
        s.from = back(s.from);
        s.to = back(s.to);
        s.contentFrom = back(s.contentFrom);
        s.contentTo = back(s.contentTo);
    }
    return spans;
}

/// The formulas of a table's rows: each cell is rendered on its own (a formula never spans cells
/// or rows: "| $15 |" and "| 10$ |" are prices), a formula cell (`=…`) shows its result.
QList<MathSpan> cellFormulas(const ScannedText &scan)
{
    QList<MathSpan> out;
    if (!scan.masked.contains(u'$'))
        return out;
    const QStringView masked = scan.masked;
    for (qsizetype k = 0; k < scan.seg.pos.size(); ++k) {
        const qsizetype rowFrom = scan.seg.pos[k];
        const qsizetype rowTo = k + 1 < scan.seg.pos.size() ? scan.seg.pos[k + 1] - 1 : masked.size();
        for (qsizetype from = rowFrom; from < rowTo;) {
            qsizetype to = from;
            while (to < rowTo && masked[to] != u'|') {
                to += masked[to] == u'\\' && to + 1 < rowTo ? 2 : 1;
            }
            const QStringView cell = masked.sliced(from, std::min(to, rowTo) - from);
            if (!cell.trimmed().startsWith(u'=')) {
                for (MathSpan s : findMathSpans(cell)) {
                    s.from += from;
                    s.to += from;
                    s.contentFrom += from;
                    s.contentTo += from;
                    out.append(s);
                }
            }
            from = to + 1;
        }
    }
    return out;
}

/// Blank the formulas `formulas` (positions in the scanned text) in each scanned line's masked
/// text: metadata tokens and properties are not read inside formulas (SYNTAX §11). A block's
/// identity and folding are: an `id::` or `collapsed::` line and a block id `^id` at the end of a
/// line stay readable, so a formula that a price on one line makes with a `$` on a later one
/// ("Cena $5 ^abc123" / "razem 15$") never costs the block its id and the references to it.
void maskFormulas(const ScannedText &scan, const QList<MathSpan> &formulas)
{
    if (formulas.isEmpty())
        return;
    QString masked = scan.masked;
    for (const MathSpan &f : formulas) {
        for (qsizetype i = f.from; i < f.to; ++i) {
            if (!isLineBreak(masked[i]))
                masked[i] = u' ';
        }
    }
    const auto restore = [&](qsizetype from, qsizetype to) {
        for (qsizetype i = from; i < to; ++i)
            masked[i] = scan.masked[i];
    };
    for (qsizetype k = 0; k < scan.lines.size(); ++k) {
        const qsizetype from = scan.seg.pos[k];
        const QStringView line = QStringView(scan.masked).sliced(from, scan.lines[k]->own->view.length);
        if (const auto pm = matchPropertyLine(line.sliced(indentOf(line).chars))) {
            const QString key = pm->key.toLower();
            if (key == u"id" || key == u"collapsed") {
                restore(from, from + line.size());
                continue;
            }
        }
        if (const auto id = blockIdAtEnd(line))
            restore(from + id->from, from + id->to);
    }
    setLineMasks(scan, masked);
}

/// Block id at the end of the first or last text line (Obsidian `^id`); records its removal.
QString findBlockId(QList<LineInfo> &infos)
{
    LineInfo *first = nullptr;
    LineInfo *last = nullptr;
    for (LineInfo &info : infos) {
        if (info.cls != LineClass::Text)
            continue;
        if (!first)
            first = &info;
        last = &info;
    }
    for (LineInfo *info : {first, last}) {
        if (!info || !info->masked.contains(u'^'))
            continue;
        const auto hit = blockIdAtEnd(info->masked);
        if (hit && info->masked.lastIndexOf(u'^') >= info->own->start) {
            info->extra.append({hit->from, hit->to});
            return hit->id;
        }
    }
    return {};
}

/// Collected task metadata (first-line tokens and planning items).
struct TaskMeta {
    std::optional<DateSpec> due, scheduled, start;
    std::optional<QDate> created, doneDate, cancelledDate;
    QString recurrence;
    Priority priority = Priority::None;
    Priority logseqPriority = Priority::None;
    QString repeater;
};

void taskMetaFromTokens(const QList<Token> &tokens, TaskMeta &meta)
{
    for (const Token &t : tokens) {
        switch (t.kind) {
        case TokenKind::Due:
            if (!meta.due)
                meta.due = t.spec();
            break;
        case TokenKind::Scheduled:
            if (!meta.scheduled)
                meta.scheduled = t.spec();
            break;
        case TokenKind::Start:
            if (!meta.start)
                meta.start = t.spec();
            break;
        case TokenKind::Created:
            if (!meta.created)
                meta.created = t.date;
            break;
        case TokenKind::Done:
            if (!meta.doneDate)
                meta.doneDate = t.date;
            break;
        case TokenKind::Cancelled:
            if (!meta.cancelledDate)
                meta.cancelledDate = t.date;
            break;
        case TokenKind::Recurrence:
            if (meta.recurrence.isEmpty())
                meta.recurrence = recurrenceOf(t);
            break;
        case TokenKind::Priority:
            if (meta.priority == Priority::None)
                meta.priority = t.priority;
            break;
        case TokenKind::LogseqPriority:
            if (meta.logseqPriority == Priority::None)
                meta.logseqPriority = t.priority;
            break;
        default: break;
        }
    }
}

/// Logseq SCHEDULED: / DEADLINE: items (only fill fields not set by emoji tokens).
void taskMetaFromPlanning(const QList<Token> &tokens, TaskMeta &meta)
{
    for (const Token &t : tokens) {
        if (t.kind != TokenKind::Planning)
            continue;
        std::optional<DateSpec> &field = t.field == TokenKind::Due ? meta.due : meta.scheduled;
        if (!field)
            field = t.spec();
        if (!t.repeater.isEmpty() && meta.repeater.isEmpty())
            meta.repeater = t.repeater;
    }
}

Task buildTask(const TaskMarker &marker, const TaskMeta &meta, const QString &text, int line)
{
    Task task;
    task.status = marker.status;
    task.marker = marker.marker;
    task.text = text;
    task.line = line;
    task.due = meta.due;
    task.scheduled = meta.scheduled;
    task.start = meta.start;
    task.created = meta.created;
    task.doneDate = meta.doneDate;
    task.cancelledDate = meta.cancelledDate;
    task.recurrence = meta.recurrence;
    if (task.recurrence.isEmpty() && !meta.repeater.isEmpty()) {
        if (const auto rule = dates::repeaterToRule(meta.repeater))
            task.recurrence = dates::formatRecurrence(*rule, dates::UiLocale::English);
    }
    task.priority = meta.priority != Priority::None ? meta.priority : meta.logseqPriority;
    return task;
}

/// Ranges (line coordinates, from `begin`) hidden by document-level `%%comments%%` — where the
/// comment-masked view is blank but the text is not.
QList<Range> commentRuns(const Structure &st, const LineView &v, qsizetype begin)
{
    QList<Range> runs;
    const QStringView t = st.text(v);
    const QStringView m = st.maskedText(v);
    if (t == m)
        return runs;
    for (qsizetype i = begin; i < t.size(); ++i) {
        if (m[i] != u' ' || t[i] == u' ')
            continue;
        qsizetype j = i + 1;
        while (j < t.size() && m[j] == u' ' && t[j] != u' ')
            ++j;
        runs.append({i, j});
        i = j;
    }
    return runs;
}

/// Text of one text line: content after `begin` without tokens / extra ranges / comments.
QString cleanLine(const Structure &st, const LineInfo &info, qsizetype begin)
{
    const QStringView text = st.text(info.own->view);
    begin = std::min(begin, text.size());
    const QList<Range> comments = commentRuns(st, info.own->view, begin);
    if (info.tokens.isEmpty() && info.extra.isEmpty() && comments.isEmpty())
        return trimEnd(text.sliced(begin)).toString();
    QList<Range> ranges;
    for (const Token &t : info.tokens) {
        if (t.from >= begin)
            ranges.append({t.from - begin, t.to - begin});
    }
    for (const Range &r : info.extra) {
        if (r.first >= begin)
            ranges.append({r.first - begin, r.second - begin});
    }
    for (const Range &r : comments)
        ranges.append({r.first - begin, r.second - begin});
    const QString removed = removeRanges(text.sliced(begin), ranges);
    return trimEnd(removed).toString();
}

bool isWhitespaceOnly(const QString &s)
{
    for (const QChar c : s) {
        if (!c.isSpace())
            return false;
    }
    return true;
}

/// Drop leading/trailing empty lines and trim the outer whitespace.
QString joinText(const QStringList &parts)
{
    qsizetype a = 0;
    qsizetype b = parts.size();
    while (a < b && isWhitespaceOnly(parts[a]))
        ++a;
    while (b > a && isWhitespaceOnly(parts[b - 1]))
        --b;
    return parts.mid(a, b - a).join(u'\n').trimmed();
}

/// Closing sequence of an ATX heading: `(?:^|[ \t]+)#+[ \t]*$`.
QString stripClosingHashes(const QString &line)
{
    qsizetype end = trimEndBlanks(line).size();
    qsizetype k = end;
    while (k > 0 && line[k - 1] == u'#')
        --k;
    if (k == end)
        return line;
    if (k == 0)
        return QString();
    if (!isSpaceOrTab(line[k - 1]))
        return line;
    while (k > 0 && isSpaceOrTab(line[k - 1]))
        --k;
    return line.first(k);
}

Block blockShell(const Structure &st, const Node &node)
{
    Block block;
    block.kind = node.kind;
    block.line = node.line;
    block.endLine = node.endLine;
    block.ownEndLine = node.ownEndLine;
    block.depth = node.depth;
    QStringList raw;
    raw.reserve(node.own.size());
    for (const OwnLine &l : node.own)
        raw.append(st.lines[l.view.line].text);
    block.raw = raw.join(u'\n');
    block.headingLevel = node.headingLevel;
    block.lang = node.lang;
    if (!node.calloutType.isEmpty()) {
        block.properties.insert(u"callouttype"_s, node.calloutType);
        if (!node.calloutFold.isEmpty())
            block.properties.insert(u"calloutfold"_s, node.calloutFold);
    }
    return block;
}

/// Source of a code / HTML block (fences excluded).
QString codeText(const Structure &st, const Node &node)
{
    QStringList parts;
    for (const OwnLine &l : node.own) {
        if (l.role == LineRole::Code || (node.kind == BlockKind::Html && l.role == LineRole::Text))
            parts.append(st.text(l.view).toString());
    }
    return parts.join(u'\n');
}

/// A table's text is its source rows (formulas and cells kept), without a trailing `^id` line.
QString tableText(const Structure &st, const Node &node)
{
    QStringList rows;
    for (const OwnLine &l : node.own) {
        if (l.role == LineRole::Meta && l.view.line == node.trailingIdLine)
            continue;
        rows.append(st.text(l.view).trimmed().toString());
    }
    return rows.join(u'\n');
}

bool isTagKey(const QString &key)
{
    return key == u"tags" || key == u"tag";
}

struct LineScan {
    QString id;
    QStringList extraTags;
    /// A `tags::` / `tag::` value as written (findNoteTags()): document offset of value[0], line.
    struct TagValue {
        qsizetype offset = 0;
        int line = 0;
        QString value;
    };
    QList<TagValue> tagValues;
};

/// Read `key:: value` property lines (into `props`), mark Logseq planning lines and find the
/// metadata tokens of every text line. Returns a Logseq `id::` (when no `^id` was found) and
/// tags given by `tags::` properties.
LineScan readLines(const Structure &st, const Node &node, QList<LineInfo> &infos,
                   const std::optional<TaskMarker> &marker, Properties &props, QString id)
{
    LineScan out;
    const bool propertyKinds =
            node.kind != BlockKind::Table && node.kind != BlockKind::Heading && node.kind != BlockKind::Callout;
    for (qsizetype k = 0; k < infos.size(); ++k) {
        LineInfo &info = infos[k];
        if (info.cls != LineClass::Text)
            continue;
        const QStringView m = info.masked;
        info.tokens = findTokens(m);
        const bool firstWithMarker = k == 0 && marker.has_value();
        const bool propertyCandidate = propertyKinds && !firstWithMarker && m.contains(u"::");
        const bool planningCandidate = k > 0 && (m.contains(u"SCHEDULED") || m.contains(u"DEADLINE"));
        if (!propertyCandidate && !planningCandidate)
            continue;
        const qsizetype begin = std::min(firstWithMarker ? marker->contentStart : info.own->start, m.size());
        const QStringView content = m.sliced(begin);
        qsizetype lead = 0;
        while (lead < content.size() && content[lead].isSpace())
            ++lead;
        const auto pm = propertyCandidate ? matchPropertyLine(content.sliced(lead)) : std::nullopt;
        if (pm) {
            const QString key = pm->key.toLower();
            const QStringView text = st.text(info.own->view);
            const qsizetype valueStart = begin + lead + pm->key.size() + 2;
            qsizetype valueEnd = text.size();
            for (const Range &r : info.extra) {
                if (r.first >= valueStart && r.first < valueEnd)
                    valueEnd = r.first;
            }
            const QString rawValue =
                    text.sliced(valueStart, std::max<qsizetype>(0, valueEnd - valueStart)).trimmed().toString();
            const QVariant value = parsePropertyValue(key, rawValue);
            props.insert(key, value);
            if (key == u"id" && !rawValue.isEmpty() && id.isEmpty() && node.kind != BlockKind::Frontmatter)
                id = rawValue;
            if (isTagKey(key) && value.typeId() == QMetaType::QStringList) {
                out.extraTags.append(value.toStringList());
                const qsizetype from = std::min(valueStart, valueEnd);
                out.tagValues.append({info.own->view.offset + from, info.own->view.line,
                                      text.sliced(from, valueEnd - from).toString()});
            }
            info.cls = LineClass::Property;
        } else if (planningCandidate && isPlanningLine(content)) {
            info.cls = LineClass::Planning;
        }
    }
    out.id = id;
    return out;
}

struct BlockText {
    QString text;
    QString first;
};

/// Block text from its lines; also the cleaned first line (task text).
BlockText blockText(const Structure &st, const Node &node, const QList<LineInfo> &infos,
                    const std::optional<TaskMarker> &marker)
{
    QStringList parts;
    BlockText out;
    for (qsizetype k = 0; k < infos.size(); ++k) {
        const LineInfo &info = infos[k];
        if (info.cls == LineClass::Blank) {
            parts.append(QString());
        } else if (info.cls == LineClass::Code) {
            if (node.kind == BlockKind::ListItem) {
                const QStringView t = st.text(info.own->view);
                parts.append(t.sliced(std::min(info.own->start, t.size())).toString());
            }
        } else if (info.cls == LineClass::Text) {
            QString line = cleanLine(st, info, k == 0 && marker ? marker->contentStart : info.own->start);
            if (node.kind == BlockKind::Heading)
                line = stripClosingHashes(line);
            if (parts.isEmpty()) {
                qsizetype lead = 0;
                while (lead < line.size() && line[lead].isSpace())
                    ++lead;
                line = line.sliced(lead);
            }
            if (k == 0)
                out.first = line.trimmed();
            parts.append(line);
        }
    }
    out.text = node.kind == BlockKind::Table ? tableText(st, node) : joinText(parts);
    return out;
}

QStringList uniqueTagKeys(const QStringList &names)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &n : names) {
        const QString k = tagKey(n);
        if (k.isEmpty() || seen.contains(k))
            continue;
        seen.insert(k);
        out.append(k);
    }
    return out;
}

/// Reminders of a block (any text line, including property lines).
void blockReminders(const QList<LineInfo> &infos, Block &block, const Ctx &ctx)
{
    QString recurrence;
    bool looked = false;
    for (const LineInfo &info : infos) {
        for (const Token &t : info.tokens) {
            if (t.kind != TokenKind::Reminder && t.kind != TokenKind::ReminderCompat)
                continue;
            if (!looked) {
                looked = true;
                if (block.task)
                    recurrence = block.task->recurrence;
                if (recurrence.isEmpty()) {
                    for (const LineInfo &other : infos) {
                        for (const Token &r : other.tokens) {
                            if (r.kind == TokenKind::Recurrence && !r.value.isEmpty() && recurrence.isEmpty())
                                recurrence = recurrenceOf(r);
                        }
                    }
                }
            }
            Reminder rem;
            rem.at = DateSpec{t.date, t.time.value_or(ctx.defaultTime)};
            rem.alarm = t.alarm;
            rem.recurrence = recurrence;
            rem.text = block.text;
            rem.line = info.own->view.line;
            block.reminders.append(rem);
        }
    }
}

/// findNoteTags(): a block's inline tags and the items of its `tags::` values.
void collectTags(const QList<TagHit> &hits, const LineScan &lines, Ctx &ctx)
{
    const QStringView content = ctx.st->content;
    QSet<qsizetype> hashes;
    for (const TagHit &t : hits) {
        hashes.insert(t.from);
        const bool bracketed = charAt(content, t.from + 1) == u'[';
        ctx.tags->append(NoteTag{t.name, t.from, t.to, t.line, bracketed ? TagForm::Bracketed : TagForm::Hash});
    }
    for (const LineScan::TagValue &v : lines.tagValues)
        ctx.tags->append(propertyTagItems(v.value, v.offset, v.line, [&](qsizetype p) { return hashes.contains(p); }));
}

/// Build one Block (without tree links) from a structural node.
Block buildBlock(const Node &node, Ctx &ctx)
{
    const Structure &st = *ctx.st;
    Block block = blockShell(st, node);
    if (node.kind == BlockKind::Code || node.kind == BlockKind::Html) {
        block.text = codeText(st, node);
        block.id = node.trailingId;
        return block;
    }
    if (node.kind == BlockKind::Frontmatter && !node.own.isEmpty() && node.own.first().role == LineRole::Code)
        return block; // YAML: page-level only

    QList<LineInfo> infos = classifyLines(st, node);
    const ScannedText scan = maskScannedLines(st, infos);
    // Links and tags; what starts inside a formula is part of the formula (SYNTAX §11), for the
    // tokens, properties and block ids of the lines too.
    const ScanResult inl = scanInline(scan.orig, scan.masked, scan.seg,
                                      node.kind == BlockKind::Table ? cellFormulas(scan) : blockFormulas(scan));
    maskFormulas(scan, inl.formulas);
    std::optional<TaskMarker> marker;
    if (node.kind == BlockKind::ListItem && !infos.isEmpty() && infos.first().cls == LineClass::Text) {
        LineInfo &first = infos.first();
        marker = matchTaskMarker(st.text(first.own->view), first.masked, first.own->start);
        if (marker)
            first.extra.append({marker->from, marker->contentStart});
    }

    // Block id: `^id` on the first/last line, a standalone `^id` after the block, or `id::`.
    QString caretId = node.kind == BlockKind::Frontmatter ? QString() : findBlockId(infos);
    if (caretId.isEmpty())
        caretId = node.trailingId;
    const LineScan lines = readLines(st, node, infos, marker, block.properties, caretId);
    block.id = lines.id;

    ctx.links.append(inl.links);
    if (ctx.tags)
        collectTags(inl.tags, lines, ctx);
    if (!inl.tags.isEmpty() || !lines.extraTags.isEmpty()) {
        QStringList names;
        for (const TagHit &t : inl.tags)
            names.append(t.name);
        names.append(lines.extraTags);
        block.tags = uniqueTagKeys(names);
    }

    const BlockText text = blockText(st, node, infos, marker);
    block.text = text.text;
    if (marker) {
        TaskMeta meta;
        taskMetaFromTokens(infos.first().tokens, meta);
        for (const LineInfo &info : std::as_const(infos))
            taskMetaFromPlanning(info.tokens, meta);
        block.task = buildTask(*marker, meta, text.first, node.line);
    }
    blockReminders(infos, block, ctx);
    return block;
}

/// Wiki links / embeds written in YAML frontmatter values (`related: "[[Page]]"`).
QList<Link> frontmatterLinks(const FrontmatterBlock &fm)
{
    const QString masked = maskInline(fm.yaml, MaskOptions{true, false});
    const ScanResult res = scanInline(fm.yaml, masked, segmentsForText(fm.yaml, fm.yamlStart, 1));
    QList<Link> out;
    for (const Link &l : res.links) {
        if (l.kind == LinkKind::Wiki || l.kind == LinkKind::Embed)
            out.append(l);
    }
    return out;
}

QVariant firstOf(const Properties &props, const QString &a, const QString &b)
{
    const auto it = props.constFind(a);
    if (it != props.constEnd())
        return *it;
    return props.value(b);
}

} // namespace

ParsedNote parseNote(const VaultPath &path, QStringView content, const VaultConfig &config)
{
    return detail::parseNoteWith(path, content, config, StructureOptions());
}

ParsedNote detail::parseNoteWith(const VaultPath &path, QStringView content, const VaultConfig &config,
                                 StructureOptions options, QList<NoteTag> *tagSites)
{
    const Structure st = parseStructure(content.toString(), options);
    Ctx ctx;
    ctx.st = &st;
    ctx.tags = tagSites;
    ctx.defaultTime = config.defaultReminderTime.isValid() ? config.defaultReminderTime : QTime(9, 0);

    ParsedNote note;
    note.path = path;
    note.title = paths::pageTitle(path);
    if (st.frontmatter) {
        note.frontmatter = parseYaml(st.frontmatter->yaml);
        ctx.links.append(frontmatterLinks(*st.frontmatter));
    }
    const Properties fmProps = frontmatterProperties(note.frontmatter);
    note.properties = fmProps;
    Properties logseqProps;

    QList<int> blockOfNode(st.nodes.size(), -1);
    QStringList allTags;
    const QList<std::pair<int, int>> order = preOrder(st);
    note.blocks.reserve(order.size()); // exactly: no growth slack (docs/PERFORMANCE.md, L8)
    for (const auto &[n, parentNode] : order) {
        const Node &node = st.nodes[n];
        Block block = buildBlock(node, ctx);
        const int index = int(note.blocks.size());
        blockOfNode[n] = index;
        const int parent = parentNode >= 0 ? blockOfNode[parentNode] : -1;
        block.parent = parent;
        if (node.kind == BlockKind::Frontmatter) {
            if (!node.own.isEmpty() && node.own.first().role == LineRole::Code) {
                block.properties = fmProps;
            } else {
                for (auto it = block.properties.constBegin(); it != block.properties.constEnd(); ++it) {
                    logseqProps.insert(it.key(), it.value());
                    note.properties.insert(it.key(), it.value());
                }
            }
        }
        if (block.kind == BlockKind::Heading && parent < 0 && block.headingLevel > 0)
            note.headings.append(Heading{block.headingLevel, block.text, block.line});
        if (block.task)
            note.tasks.append(*block.task);
        note.reminders.append(block.reminders);
        if (!block.id.isEmpty() && !note.blockIds.contains(block.id))
            note.blockIds.insert(block.id, index);
        allTags.append(block.tags);
        note.blocks.append(std::move(block));
        if (parent >= 0)
            note.blocks[parent].children.append(index);
    }

    QStringList aliases = namesFrom(frontmatterGet(note.frontmatter, {u"aliases"_s, u"alias"_s}), false);
    aliases.append(namesFrom(firstOf(logseqProps, u"alias"_s, u"aliases"_s), false));
    note.aliases = uniqueCaseInsensitive(aliases);

    QStringList tags = namesFrom(frontmatterGet(note.frontmatter, {u"tags"_s, u"tag"_s}), true);
    tags.append(namesFrom(firstOf(logseqProps, u"tags"_s, u"tag"_s), false));
    tags.append(allTags);
    note.tags = uniqueTagKeys(tags);

    note.links = std::move(ctx.links);
    std::stable_sort(note.links.begin(), note.links.end(),
                     [](const Link &a, const Link &b) { return a.from < b.from; });
    note.journalDate = journalDateFromPath(path, config);

    // No growth slack in what the index keeps (docs/PERFORMANCE.md, L8).
    note.links.squeeze();
    note.headings.squeeze();
    note.tasks.squeeze();
    note.reminders.squeeze();
    for (Block &b : note.blocks) {
        if (b.children.capacity() > b.children.size())
            b.children.squeeze();
        if (b.reminders.capacity() > b.reminders.size())
            b.reminders.squeeze();
    }
    return note;
}

int blockAtLine(const ParsedNote &note, int line)
{
    int best = -1;
    QList<int> roots;
    for (int i = 0; i < note.blocks.size(); ++i) {
        if (note.blocks[i].parent < 0)
            roots.append(i);
    }
    const QList<int> *level = &roots;
    for (;;) {
        int hit = -1;
        for (int b : *level) {
            if (note.blocks[b].line <= line && line <= note.blocks[b].endLine) {
                hit = b;
                break;
            }
        }
        if (hit < 0)
            return best;
        best = hit;
        const Block &h = note.blocks[hit];
        const bool inChild = std::any_of(h.children.begin(), h.children.end(), [&](int c) {
            return note.blocks[c].line <= line && line <= note.blocks[c].endLine;
        });
        if (!inChild)
            return hit;
        level = &h.children;
    }
}

} // namespace brulion::markdown
