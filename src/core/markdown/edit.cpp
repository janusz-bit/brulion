#include "markdown/edit.h"

#include "dates/local.h"
#include "dates/names.h"
#include "dates/recurrence.h"
#include "markdown/parse.h"
#include "markdown/structure_p.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "markdown/tokens_p.h"

#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// A text replacement in line coordinates.
struct Edit {
    qsizetype from = 0;
    qsizetype to = 0;
    QString insert;
};

/// Apply non-overlapping edits (any order).
QString applyEdits(QStringView text, QList<Edit> edits)
{
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.from < b.from; });
    QString out;
    qsizetype last = 0;
    for (const Edit &e : std::as_const(edits)) {
        if (e.from < last)
            continue;
        out += text.sliced(last, e.from - last);
        out += e.insert;
        last = e.to;
    }
    out += text.sliced(std::min(last, text.size()));
    return out;
}

/// A removal of [from, to) that also swallows the whitespace before it.
Edit removal(QStringView text, qsizetype from, qsizetype to)
{
    qsizetype f = from;
    while (f > 0 && isSpaceOrTab(text[f - 1]))
        --f;
    return Edit{f, to, QString()};
}

/// Replace document line `line` by `newLines` (joined with the line's terminator).
QString replaceLine(const QString &content, const QList<text::Line> &lines, int line, const QStringList &newLines)
{
    const qsizetype start = lines[line].start;
    const qsizetype end = start + lines[line].text.size();
    const QString eol = text::preferredEol(lines, line);
    return content.first(start) + newLines.join(eol) + content.sliced(end);
}

QString quotePrefix(QStringView text)
{
    return text.first(quotePrefixOf(text).length).toString();
}

QString isoDate(QDate d)
{
    return d.toString(u"yyyy-MM-dd"_s);
}

/// Where a status change applies: a document line as seen by its container.
struct Target {
    Structure st;
    int node = -1;
    int line = 0;           ///< document line to rewrite
    QString text;           ///< that line inside its container (quote prefixes removed)
    QString masked;         ///< comment-masked `text`
    qsizetype offset = 0;   ///< document offset of text[0]
    qsizetype prefix = 0;   ///< characters of container prefix before `text` in the line
    qsizetype insertAt = 0; ///< where `- [ ] ` goes for a plain line (index in `text`)
};

const OwnLine *ownLineAt(const Node &node, int line)
{
    for (const OwnLine &l : node.own) {
        if (l.view.line == line)
            return &l;
    }
    return nullptr;
}

bool isCodeLine(const Node &node, int line)
{
    if (node.kind == BlockKind::Code || node.kind == BlockKind::Html)
        return true;
    const OwnLine *own = ownLineAt(node, line);
    return own && (own->role == LineRole::Code || own->role == LineRole::Fence);
}

/// Lines that must not become tasks: headings, rules, table rows, a callout's header line and
/// syntax-only lines (setext underlines, standalone `^id` lines).
bool isStructuralLine(const Node &node, int line)
{
    if (node.kind == BlockKind::Heading || node.kind == BlockKind::Rule || node.kind == BlockKind::Table)
        return true;
    if (node.kind == BlockKind::Callout && line == node.line)
        return true;
    const OwnLine *own = ownLineAt(node, line);
    return own && own->role == LineRole::Meta;
}

/// Is the fenced code block closed by a fence line (not by the end of its container)?
bool isClosedFence(const Node &node)
{
    return std::count_if(node.own.begin(), node.own.end(), [](const OwnLine &l) { return l.role == LineRole::Fence; })
           >= 2;
}

/// Is blank `line` part of an unclosed fenced code block (which runs to the end of its
/// container; the parser leaves its trailing blank lines out of the block's range)?
bool inUnclosedFence(const Structure &st, int line)
{
    int prev = -1;
    int prevParent = -1;
    for (const auto &[n, parent] : preOrder(st)) {
        const Node &node = st.nodes[n];
        if (node.endLine < line && (prev < 0 || node.endLine >= st.nodes[prev].endLine)) {
            prev = n;
            prevParent = parent;
        }
    }
    if (prev < 0)
        return false;
    const Node &code = st.nodes[prev];
    return code.kind == BlockKind::Code && !isClosedFence(code)
           && (prevParent < 0 || st.nodes[prevParent].endLine >= line);
}

bool insideComment(const QList<Range> &comments, qsizetype pos)
{
    for (const Range &r : comments) {
        if (r.first >= pos)
            return false;
        if (pos < r.second)
            return true;
    }
    return false;
}

/// Resolve the line a task edit applies to (a list item's first line), or nullopt.
std::optional<Target> resolveTarget(const QString &content, int line)
{
    Target t;
    t.st = parseStructure(content);
    const Structure &st = t.st;
    if (line < 0 || line >= st.lines.size())
        return std::nullopt;
    t.node = nodeAtLine(st, line);
    const Node *node = t.node >= 0 ? &st.nodes[t.node] : nullptr;
    if (node && (node->kind == BlockKind::Frontmatter || isCodeLine(*node, line) || isStructuralLine(*node, line)))
        return std::nullopt;
    const QStringView lineMasked = st.maskedText(st.lineView(line));
    if (isBlank(lineMasked.sliced(quotePrefixOf(lineMasked).length)) && inUnclosedFence(st, line))
        return std::nullopt;
    t.line = node && node->kind == BlockKind::ListItem ? node->line : line;
    const OwnLine *own = node ? ownLineAt(*node, t.line) : nullptr;
    const LineView view = own ? own->view : st.lineView(t.line);
    t.text = st.text(view).toString();
    t.masked = st.maskedText(view).toString();
    t.offset = view.offset;
    t.prefix = view.offset - st.lines[t.line].start;
    // Plain lines get `- [ ] ` after their quote markers (all nesting levels) and indentation,
    // and after a byte order mark (it must stay the first character of the file).
    const qsizetype bom = t.offset == 0 && t.text.startsWith(QChar(0xfeff)) ? 1 : 0;
    const qsizetype from = std::max(own && node->kind == BlockKind::Quote ? own->start : 0, bom);
    const qsizetype q = from + quotePrefixOf(QStringView(t.text).sliced(from)).length;
    t.insertAt = q + indentOf(QStringView(t.text).sliced(q)).chars;
    // A plain line inside a multi-line `%%comment%%` is hidden: a task there would be invisible.
    if (!matchListMarker(maskInline(t.masked)) && insideComment(st.comments, t.offset + t.insertAt))
        return std::nullopt;
    return t;
}

/// Index in the target's text where a `%%comment%%` that continues past the end of the line
/// starts (including the whitespace before it), or the text length.
qsizetype hiddenTailStart(const Target &t)
{
    const qsizetype lineEnd = t.offset + t.text.size();
    for (const Range &r : t.st.comments) {
        if (r.first >= lineEnd)
            break;
        if (r.first >= t.offset && r.second > lineEnd) {
            qsizetype k = r.first - t.offset;
            while (k > 0 && isSpaceOrTab(t.text[k - 1]))
                --k;
            return k;
        }
    }
    return t.text.size();
}

std::optional<TaskMarker> markerOf(const QString &text, const QString &masked)
{
    const QString m = maskInline(masked);
    const auto lm = matchListMarker(m);
    return lm ? matchTaskMarker(text, m, lm->contentStart) : std::nullopt;
}

/// Append a token at the end of a line, before a trailing block id.
QString appendToken(const QString &text, const QString &token)
{
    if (const auto id = blockIdAtEnd(maskInline(text))) {
        const QString before = trimEnd(QStringView(text).first(id->from)).toString();
        return before + u' ' + token + u' ' + QStringView(text).sliced(id->from).trimmed().toString();
    }
    return trimEnd(text).toString() + u' ' + token;
}

/// Rewriting a line normalises its 🔁 rule to the canonical English form (SYNTAX §7).
std::optional<Edit> canonicalRecurrenceEdit(const Token &t)
{
    if (t.kind != TokenKind::Recurrence || t.canonical.isEmpty() || t.canonical == t.value || t.valueFrom < 0)
        return std::nullopt;
    return Edit{t.valueFrom, t.valueTo, t.canonical};
}

/// Remove / add ✅ and ❌ dates for a status change (and canonicalise the rule).
QString updateCompletionDates(const QString &text, std::optional<TaskStatus> prev, TaskStatus status, QDate today)
{
    QList<Edit> edits;
    for (const Token &t : findTokens(maskInline(text))) {
        if (t.kind == TokenKind::Done && !(status == TaskStatus::Done && prev == TaskStatus::Done))
            edits.append(removal(text, t.from, t.to));
        if (t.kind == TokenKind::Cancelled && !(status == TaskStatus::Cancelled && prev == TaskStatus::Cancelled))
            edits.append(removal(text, t.from, t.to));
        if (const auto e = canonicalRecurrenceEdit(t))
            edits.append(*e);
    }
    QString out = applyEdits(text, edits);
    if (status == TaskStatus::Done && prev != TaskStatus::Done)
        out = appendToken(out, tokens::Done.toString() + u' ' + isoDate(today));
    if (status == TaskStatus::Cancelled && prev != TaskStatus::Cancelled)
        out = appendToken(out, tokens::Cancelled.toString() + u' ' + isoDate(today));
    return out;
}

/// Rewrite the marker of a line (or turn the line into a task) and update completion dates.
QString rewriteStatus(const Target &t, const std::optional<TaskMarker> &marker, TaskStatus status, QDate today)
{
    const QString &text = t.text;
    QString out;
    if (marker && marker->kind == TaskMarker::Kind::Checkbox) {
        const QChar previous = marker->marker.isEmpty() ? QChar(u' ') : marker->marker[0];
        out = text.first(marker->from + 1) + checkboxCharFor(status, previous) + text.sliced(marker->from + 2);
    } else if (marker) {
        out = text.first(marker->from) + keywordFor(status, marker->marker) + text.sliced(marker->to);
    } else {
        const QString box = u'[' + QString(statusChar(status)) + u"] "_s;
        if (const auto lm = matchListMarker(t.masked)) {
            const QString sep = lm->contentStart == lm->markerEnd ? u" "_s : QString();
            out = text.first(lm->contentStart) + sep + box + text.sliced(lm->contentStart);
        } else {
            const QString sep = t.insertAt > 0 && text[t.insertAt - 1] == u'>' ? u" "_s : QString();
            out = text.first(t.insertAt) + sep + u"- "_s + box + text.sliced(t.insertAt);
        }
    }
    return updateCompletionDates(out, marker ? std::optional(marker->status) : std::nullopt, status, today);
}

/// How a recurring task repeats.
struct Repeat {
    dates::RecurrenceRule rule;
    bool catchUp = false; ///< Logseq `++` repeater: move on until the date is after today
};

std::optional<Repeat> recurrenceRuleOf(const QList<Token> &lineTokens, const QList<Token> &planning)
{
    for (const Token &t : lineTokens) {
        if (t.kind == TokenKind::Recurrence && !t.value.isEmpty()) {
            if (const auto rule = dates::parseRecurrence(t.value))
                return Repeat{*rule, false};
            return std::nullopt;
        }
    }
    for (const Token &p : planning) {
        if (p.repeater.isEmpty())
            continue;
        if (const auto rule = dates::repeaterToRule(p.repeater))
            return Repeat{*rule, p.repeater.startsWith(u"++")};
    }
    return std::nullopt;
}

/// Upper bound of catch-up steps (a daily `++1d` repeater ~27 years behind).
constexpr int MaxCatchUp = 10'000;

std::optional<DateSpec> nextDate(const Repeat &repeat, const DateSpec &from, QDate today)
{
    auto next = dates::nextOccurrence(repeat.rule, from, today);
    for (int k = 0; repeat.catchUp && next && next->date <= today && k < MaxCatchUp; ++k) {
        const auto after = dates::nextOccurrence(repeat.rule, *next, today);
        if (!after || *after == *next)
            break;
        next = after;
    }
    return next;
}

struct PlanningLine {
    int line = 0;
    QString text;
    QList<Token> tokens;
};

/// Planning lines (SCHEDULED: / DEADLINE:) of a list item, with their planning tokens.
QList<PlanningLine> planningLinesOf(const Target &t)
{
    QList<PlanningLine> out;
    if (t.node < 0 || t.st.nodes[t.node].kind != BlockKind::ListItem)
        return out;
    static const QRegularExpression re(u"^[ \\t>]*(?:(?:SCHEDULED|DEADLINE):[ \\t]*<[^>]*>[ \\t]*)+$"_s);
    const Node &node = t.st.nodes[t.node];
    for (qsizetype k = 1; k < node.own.size(); ++k) {
        const OwnLine &own = node.own[k];
        if (own.role != LineRole::Text)
            continue;
        const QString docText = t.st.lines[own.view.line].text;
        const QString masked = maskInline(docText);
        if (!re.match(masked).hasMatch())
            continue;
        PlanningLine p{own.view.line, docText, {}};
        for (const Token &tok : findTokens(masked)) {
            if (tok.kind == TokenKind::Planning)
                p.tokens.append(tok);
        }
        out.append(p);
    }
    return out;
}

std::optional<DateSpec> shiftSpec(const DateSpec &spec, qint64 days)
{
    const QDate d = spec.date.addDays(days);
    if (!dates::isSupportedDate(d))
        return std::nullopt;
    return DateSpec{d, spec.time};
}

/// Shift the date inside a Logseq planning timestamp, updating an English weekday name.
std::optional<Edit> shiftPlanning(const QString &text, const Token &tok, qint64 days)
{
    if (tok.valueFrom < 0 || !tok.date.isValid())
        return std::nullopt;
    const auto shifted = shiftSpec(DateSpec{tok.date, std::nullopt}, days);
    if (!shifted)
        return std::nullopt;
    QString inner = text.sliced(tok.valueFrom, tok.valueTo - tok.valueFrom);
    const qsizetype at = inner.indexOf(tok.value);
    if (at >= 0)
        inner.replace(at, tok.value.size(), shifted->toString());
    static const QRegularExpression weekday(u"^(\\s*\\d{4}-\\d{2}-\\d{2}\\s+)(\\p{L}+\\.?)"_s);
    const auto m = weekday.match(inner);
    if (m.hasMatch()) {
        const QString wd =
                dates::weekdayName(shifted->date.dayOfWeek(), dates::UiLocale::English, dates::NameForm::Short);
        inner.replace(m.capturedStart(2), m.capturedLength(2), wd);
    }
    return Edit{tok.valueFrom, tok.valueTo, inner};
}

/// Next occurrence of a recurring task (Obsidian Tasks semantics): the reference date (due,
/// else scheduled, else start, else the first reminder) moves to the rule's next date and every
/// other date keeps its distance to it. Returns the new lines (task line + copied planning
/// lines) or nullopt.
std::optional<QStringList> nextInstance(const Target &t, const QString &text, const Repeat &repeat,
                                        const QList<PlanningLine> &planning, QDate today)
{
    const QString masked = maskInline(text);
    const auto lm = matchListMarker(masked);
    const auto marker = lm ? matchTaskMarker(text, masked, lm->contentStart) : std::nullopt;
    const QList<Token> lineTokens = findTokens(masked);
    const auto first = [&](TokenKind kind) -> const Token * {
        for (const Token &k : lineTokens) {
            if (k.kind == kind && k.date.isValid())
                return &k;
        }
        return nullptr;
    };
    const auto plan = [&](TokenKind field) -> const Token * {
        for (const PlanningLine &p : planning) {
            for (const Token &k : p.tokens) {
                if (k.field == field && k.date.isValid())
                    return &k;
            }
        }
        return nullptr;
    };
    const Token *ref = first(TokenKind::Due);
    for (const auto &candidate :
         {plan(TokenKind::Due), first(TokenKind::Scheduled), plan(TokenKind::Scheduled), first(TokenKind::Start),
          first(TokenKind::Reminder), first(TokenKind::ReminderCompat)}) {
        if (!ref)
            ref = candidate;
    }
    qint64 days = 0;
    if (ref) {
        const DateSpec refValue = ref->spec();
        const auto next = nextDate(repeat, refValue, today);
        if (!next || *next == refValue)
            return std::nullopt;
        days = refValue.date.daysTo(next->date);
    }
    QList<Edit> edits;
    if (marker && marker->kind == TaskMarker::Kind::Checkbox)
        edits.append(Edit{marker->from + 1, marker->from + 2, QString(statusChar(TaskStatus::Todo))});
    else if (marker)
        edits.append(Edit{marker->from, marker->to, keywordFor(TaskStatus::Todo, marker->marker)});
    for (const Token &k : lineTokens) {
        switch (k.kind) {
        case TokenKind::Recurrence:
            if (const auto e = canonicalRecurrenceEdit(k))
                edits.append(*e);
            break;
        case TokenKind::Done:
        case TokenKind::Cancelled: edits.append(removal(text, k.from, k.to)); break;
        case TokenKind::Created: edits.append(Edit{k.valueFrom, k.valueTo, isoDate(today)}); break;
        case TokenKind::Due:
        case TokenKind::Scheduled:
        case TokenKind::Start:
        case TokenKind::Reminder:
        case TokenKind::ReminderCompat: {
            const auto shifted = shiftSpec(k.spec(), days);
            if (!shifted)
                return std::nullopt;
            edits.append(Edit{k.valueFrom, k.valueTo, shifted->toString()});
            break;
        }
        default: break;
        }
    }
    if (const auto id = blockIdAtEnd(masked))
        edits.append(removal(text, id->from, id->to));
    const QString docLine = t.st.lines[t.line].text;
    QStringList out{docLine.first(t.prefix) + trimEnd(applyEdits(text, edits)).toString()};
    for (const PlanningLine &p : planning) {
        QList<Edit> pe;
        for (const Token &tok : p.tokens) {
            if (const auto e = shiftPlanning(p.text, tok, days))
                pe.append(*e);
        }
        out.append(applyEdits(p.text, pe));
    }
    return out;
}

/// Rewrite the target's line for `status` (plus the next occurrence of a recurring task).
QString applyStatus(const QString &content, const Target &t, const std::optional<TaskMarker> &marker, TaskStatus status,
                    QDate today)
{
    if (marker && marker->status == status)
        return content;
    // Edit only the visible part of the line; a comment running past its end is kept as is.
    const qsizetype tailAt = hiddenTailStart(t);
    const QString tail = t.text.sliced(tailAt);
    Target vt = t;
    if (!tail.isEmpty()) {
        vt.text = t.text.first(tailAt);
        vt.masked = t.masked.first(tailAt);
    }
    const QString visible = rewriteStatus(vt, marker, status, today);
    const bool glue = !tail.isEmpty() && !isSpaceOrTab(charAt(visible, visible.size() - 1)) && !isSpaceOrTab(tail[0]);
    const QString docLine = t.st.lines[t.line].text;
    QStringList out{docLine.first(t.prefix) + visible + (glue ? u" "_s : QString()) + tail};
    if (status == TaskStatus::Done && (!marker || marker->status != TaskStatus::Done)) {
        const QList<Token> lineTokens = findTokens(maskInline(vt.text));
        const QList<PlanningLine> planning = planningLinesOf(t);
        QList<Token> planningTokens;
        for (const PlanningLine &p : planning)
            planningTokens.append(p.tokens);
        if (const auto repeat = recurrenceRuleOf(lineTokens, planningTokens)) {
            // A line that only now became a task is copied in its converted form.
            if (const auto inst = nextInstance(t, marker ? vt.text : visible, *repeat, planning, today))
                out = *inst + out;
        }
    }
    return replaceLine(content, t.st.lines, t.line, out);
}

} // namespace

QString setTaskStatus(const QString &content, int line, TaskStatus status, QDate today)
{
    const auto t = resolveTarget(content, line);
    return t ? applyStatus(content, *t, markerOf(t->text, t->masked), status, today) : content;
}

QString toggleTask(const QString &content, int line, QDate today)
{
    const auto t = resolveTarget(content, line);
    if (!t)
        return content;
    const auto marker = markerOf(t->text, t->masked);
    const bool open = marker && marker->status != TaskStatus::Done && marker->status != TaskStatus::Cancelled;
    return applyStatus(content, *t, marker, open ? TaskStatus::Done : TaskStatus::Todo, today);
}

QString removeTaskMarker(const QString &content, int line)
{
    const auto t = resolveTarget(content, line);
    if (!t)
        return content;
    const auto marker = markerOf(t->text, t->masked);
    // Edit only the visible part of the line; a comment running past its end is kept as is.
    const qsizetype tailAt = hiddenTailStart(*t);
    if (!marker || marker->contentStart > tailAt)
        return content;
    const QString visible = t->text.first(tailAt);
    QList<Edit> edits{Edit{marker->from, marker->contentStart, QString()}};
    for (const Token &tok : findTokens(maskInline(visible))) {
        if (tok.kind != TokenKind::Done && tok.kind != TokenKind::Cancelled)
            continue;
        Edit e = removal(visible, tok.from, tok.to);
        if (e.from <= marker->contentStart) {
            // A date right after the marker: the whitespace before it is the marker's own (it
            // goes with the marker), so the whitespace after it goes with the date.
            e.from = marker->contentStart;
            while (e.to < visible.size() && isSpaceOrTab(visible[e.to]))
                ++e.to;
        }
        edits.append(e);
    }
    QString out = applyEdits(visible, edits);
    // The text left must not open a block over the lines after it: "- [ ] ```js" as "- ```js"
    // would take the item's children into a code block, "- [ ] $$" a formula up to a later `$$`
    // line (their tasks and reminders gone). Its first character is escaped instead, so the item
    // keeps its text as text ("- \```js").
    const QStringView rest = QStringView(out).sliced(marker->from);
    if (fenceOpen(rest) || rest.trimmed() == u"$$")
        out.insert(marker->from + indentOf(rest).chars, u'\\');
    const QString tail = t->text.sliced(tailAt);
    const bool glue = !tail.isEmpty() && !isSpaceOrTab(charAt(out, out.size() - 1)) && !isSpaceOrTab(tail[0]);
    const QString docLine = t->st.lines[t->line].text;
    return replaceLine(content, t->st.lines, t->line,
                       {docLine.first(t->prefix) + out + (glue ? u" "_s : QString()) + tail});
}

QString generateBlockId()
{
    static constexpr char16_t Alphabet[] = u"abcdefghijklmnopqrstuvwxyz0123456789";
    QString out;
    out.reserve(6);
    for (int k = 0; k < 6; ++k)
        out += QChar(Alphabet[QRandomGenerator::global()->bounded(36)]);
    return out;
}

namespace {

/// Prefix of the container around a quote whose lines start with `prefix` (one '>' less).
QString outerPrefix(const QString &prefix)
{
    const QString p = prefix.first(std::max<qsizetype>(0, prefix.lastIndexOf(u'>')));
    return p.contains(u'>') ? p : QString();
}

/// Candidate documents with `^id` written for the block, in order of preference.
QStringList idPlacements(const QString &content, const Structure &st, const Block &block, const QString &id)
{
    const QList<text::Line> &lines = st.lines;
    const int nodeIdx = nodeAtLine(st, block.line);
    const Node *node = nodeIdx >= 0 ? &st.nodes[nodeIdx] : nullptr;
    const auto append = [&](int ln) {
        return replaceLine(content, lines, ln, {trimEnd(lines[ln].text).toString() + u" ^"_s + id});
    };
    // `^id` on its own line after line `last` (optionally after a blank line), then a blank
    // line before following text.
    const auto idLineAfter = [&](int last, const QString &prefix, bool blankBefore) {
        const QString blank = trimEnd(prefix).toString();
        QStringList out{lines[last].text};
        if (blankBefore)
            out.append(blank);
        out.append(prefix + u'^' + id);
        if (last + 1 < lines.size()) {
            const QString next = lines[last + 1].text.trimmed();
            if (!next.isEmpty() && next != prefix.trimmed())
                out.append(blank);
        }
        return replaceLine(content, lines, last, out);
    };
    QString prefix;
    if (node && !node->own.isEmpty()) {
        const LineView &first = node->own.first().view;
        prefix = lines[first.line].text.first(first.offset - lines[first.line].start);
    }
    QStringList out;
    switch (block.kind) {
    case BlockKind::ListItem:
        if (node && !node->own.isEmpty() && node->own.first().role == LineRole::Fence) {
            // First line opens a code fence: the id goes on its own line after the item's content.
            const int last = block.ownEndLine;
            const QString indent(std::max(node->contentCol, 0), u' ');
            const QString docText = lines[last].text;
            out.append(replaceLine(content, lines, last, {docText, quotePrefix(docText) + indent + u'^' + id}));
            break;
        }
        out.append(append(block.line));
        if (block.ownEndLine != block.line)
            out.append(append(block.ownEndLine));
        break;
    case BlockKind::Heading:
    case BlockKind::Callout: out.append(append(block.line)); break;
    case BlockKind::Quote:
        if (!block.children.isEmpty()) {
            // A line right after the quote would be a lazy continuation of its last block.
            out.append(idLineAfter(block.endLine, outerPrefix(quotePrefix(lines[block.line].text)), true));
            break;
        }
        out.append(append(block.ownEndLine));
        if (block.ownEndLine != block.line)
            out.append(append(block.line));
        break;
    case BlockKind::Paragraph:
        out.append(append(block.ownEndLine));
        if (block.ownEndLine != block.line)
            out.append(append(block.line));
        break;
    case BlockKind::Html:
        // HTML blocks of CommonMark types 6/7 only end at a blank line.
        out.append(idLineAfter(block.endLine, prefix, true));
        break;
    case BlockKind::Code:
        // An id line after an unclosed fence would be code.
        if (node && isClosedFence(*node))
            out.append(idLineAfter(block.endLine, prefix, false));
        break;
    case BlockKind::Frontmatter: break;
    case BlockKind::Table:
    case BlockKind::Rule:
        out.append(idLineAfter(block.endLine, prefix, false));
        out.append(idLineAfter(block.endLine, prefix, true));
        break;
    }
    return out;
}

} // namespace

EnsuredBlockId ensureBlockId(const QString &content, int line, const std::function<bool(const QString &)> &isTaken)
{
    const ParsedNote note = parseNote(QString(), content);
    const int b = blockAtLine(note, line);
    if (b < 0 || note.blocks[b].kind == BlockKind::Frontmatter)
        return {content, QString()};
    const Block &block = note.blocks[b];
    if (!block.id.isEmpty())
        return {content, block.id};
    QString id = generateBlockId();
    while (note.blockIds.contains(id) || (isTaken && isTaken(id)))
        id = generateBlockId();
    const Structure st = parseStructure(content);
    for (const QString &candidate : idPlacements(content, st, block, id)) {
        const ParsedNote parsed = parseNote(QString(), candidate);
        const auto it = parsed.blockIds.constFind(id);
        if (it == parsed.blockIds.constEnd())
            continue;
        const Block &found = parsed.blocks[*it];
        if (found.line == block.line && found.kind == block.kind)
            return {candidate, id};
    }
    return {content, QString()};
}

std::optional<TextRange> listItemRange(const QString &content, int line)
{
    const Structure st = parseStructure(content);
    if (line < 0 || line >= st.lines.size())
        return std::nullopt;
    int item = -1;
    const QList<int> *level = &st.roots;
    for (;;) {
        int hit = -1;
        for (int n : *level) {
            if (st.nodes[n].line <= line && line <= st.nodes[n].endLine) {
                hit = n;
                break;
            }
        }
        if (hit < 0)
            break;
        if (st.nodes[hit].kind == BlockKind::ListItem)
            item = hit;
        level = &st.nodes[hit].children;
    }
    if (item < 0)
        return std::nullopt;
    const Node &n = st.nodes[item];
    return TextRange{st.lines[n.line].start, st.lines[n.endLine].start + st.lines[n.endLine].text.size()};
}

} // namespace brulion::markdown
