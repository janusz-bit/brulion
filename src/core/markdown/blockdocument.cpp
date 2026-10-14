#include "markdown/blockdocument.h"

#include "markdown/edit.h"
#include "markdown/frontmatter.h"
#include "markdown/lines_p.h"
#include "markdown/parse_p.h"
#include "markdown/structure_p.h"
#include "markdown/tasks.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

bool endsWithEol(QStringView s)
{
    return s.endsWith(u'\n') || s.endsWith(u'\r');
}

int countLineBreaks(QStringView s)
{
    int n = 0;
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'\n' || (s[i] == u'\r' && charAt(s, i + 1) != u'\n'))
            ++n;
    }
    return n;
}

/// "\r\n" and "\r" become "\n".
QString normalizeNewlines(const QString &text)
{
    if (!text.contains(u'\r'))
        return text;
    QString out = text;
    out.replace(u"\r\n"_s, u"\n"_s);
    out.replace(u'\r', u'\n');
    return out;
}

/// The first line terminator inside `s`, or an empty string.
QString firstEol(QStringView s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s[i] == u'\n')
            return u"\n"_s;
        if (s[i] == u'\r')
            return charAt(s, i + 1) == u'\n' ? u"\r\n"_s : u"\r"_s;
    }
    return {};
}

QString joinLineRange(const QList<text::Line> &lines, int from, int to, bool withLastEol)
{
    QString out;
    for (int k = from; k < to; ++k) {
        out += lines[k].text;
        if (k + 1 < to || withLastEol)
            out += lines[k].eol;
    }
    return out;
}

/// A prefix that can be followed by text ("-" -> "- ").
QString spacedPrefix(const QString &prefix)
{
    if (prefix.isEmpty() || isSpaceOrTab(prefix.back()))
        return prefix;
    return prefix + u' ';
}

/// The list marker of a (possibly unspaced) prefix such as "- ", "  3. " or "-".
std::optional<ListMarker> markerOfPrefix(const QString &prefix)
{
    const QString probe = spacedPrefix(prefix) + u'x';
    return matchListMarker(probe);
}

/// Indentation for continuation lines derived from a list prefix: "\t- " -> "\t  ".
QString defaultContIndent(const QString &prefix)
{
    QString out = spacedPrefix(prefix);
    for (QChar &c : out) {
        if (!isSpaceOrTab(c))
            c = u' ';
    }
    return out;
}

/// The task marker that a new block continuing `body` starts with ("[ ] ", "TODO ", "").
QString taskContinuation(const QString &body)
{
    const auto marker = matchTaskMarker(body, 0);
    if (!marker)
        return {};
    if (marker->kind == TaskMarker::Kind::Checkbox)
        return u"[ ] "_s;
    return keywordFor(TaskStatus::Todo, marker->marker) + u' ';
}

QStringView trimStartBlanks(QStringView s)
{
    return s.sliced(indentOf(s).chars);
}

/// Re-indent one line from indentation `from` to `to` (visual delta for other indentation).
QString reindentLine(const QString &line, const QString &from, const QString &to)
{
    if (line.startsWith(from))
        return to + line.sliced(from.size());
    const int delta = columnsOf(to) - columnsOf(from);
    if (delta > 0)
        return (to.startsWith(from) ? to.sliced(from.size()) : QString(delta, u' ')) + line;
    if (delta < 0)
        return line.sliced(charsForColumns(line, -delta));
    return line;
}

/// Does a block of kind `b` directly after a block of kind `a` need a blank line between them
/// to stay a separate block? (A paragraph after a list item would be a lazy continuation, …)
bool needsBlankLine(BlockKind a, BlockKind b)
{
    switch (a) {
    case BlockKind::Paragraph:
    case BlockKind::Html:
    case BlockKind::Quote:
    case BlockKind::Callout:
    case BlockKind::Table: return true;
    case BlockKind::ListItem: return b != BlockKind::ListItem;
    default: return false;
    }
}

} // namespace

BlockDocument::BlockDocument() = default;

BlockDocument::BlockDocument(const QString &content)
{
    setContent(content);
}

void BlockDocument::setContent(const QString &content)
{
    m_items.clear();
    m_leading.clear();
    m_bom.clear();
    QString text = content;
    if (!text.isEmpty() && text[0].unicode() == 0xfeff) {
        m_bom = text.first(1);
        text = text.sliced(1);
    }
    m_eol = text::detectEol(text);
    m_finalNewline = text.isEmpty() || endsWithEol(text);

    const Structure st = parseStructure(text);
    const QList<text::Line> &lines = st.lines;
    const int lineCount = int(lines.size());

    // Block starts: top-level blocks and list items nested in top-level lists (callouts and
    // quotes are one block with everything inside them).
    QList<bool> included(st.nodes.size(), false);
    QList<int> starts;
    for (const auto &[n, parent] : preOrder(st)) {
        const Node &node = st.nodes[n];
        const bool listChild = parent >= 0 && node.kind == BlockKind::ListItem
                               && st.nodes[parent].kind == BlockKind::ListItem && included[parent];
        if (parent < 0 || listChild) {
            included[n] = true;
            starts.append(node.line);
        }
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    // Lines before the first block: blank lines are kept aside; text there (a comment) becomes
    // a paragraph block so it stays editable.
    const int firstStart = starts.isEmpty() ? lineCount : starts.first();
    int firstText = -1;
    for (int k = 0; k < firstStart; ++k) {
        if (!isBlank(lines[k].text)) {
            firstText = k;
            break;
        }
    }
    if (firstText >= 0)
        starts.prepend(firstText);
    m_leading = joinLineRange(lines, 0, firstText >= 0 ? firstText : firstStart, true);

    for (qsizetype s = 0; s < starts.size(); ++s) {
        const int from = starts[s];
        const int to = s + 1 < starts.size() ? starts[s + 1] : lineCount;
        int last = to - 1;
        while (last > from && isBlank(lines[last].text))
            --last;
        m_items.append(makeItem(joinLineRange(lines, from, last + 1, false), lines[last].eol,
                                joinLineRange(lines, last + 1, to, true)));
    }
    for (int i = 0; i < count(); ++i)
        derive(i);
    recompute();
}

QString BlockDocument::toString() const
{
    QString out = m_bom + m_leading;
    for (const Item &it : m_items) {
        out += it.own;
        out += it.eol;
        out += it.entry.trailing;
    }
    return out;
}

QString BlockDocument::blockSource(int i) const
{
    return m_items[i].own;
}

int BlockDocument::indexOfUid(quint64 uid) const
{
    for (int i = 0; i < count(); ++i) {
        if (m_items[i].entry.uid == uid)
            return i;
    }
    return -1;
}

int BlockDocument::indexAtLine(int line) const
{
    if (line < 0)
        return -1;
    int found = -1;
    for (int i = 0; i < count() && m_items[i].entry.line <= line; ++i)
        found = i;
    if (found >= 0 && found == count() - 1) {
        const Item &it = m_items[found];
        const int end =
                it.entry.line + countLineBreaks(it.own) + countLineBreaks(it.eol) + countLineBreaks(it.entry.trailing);
        if (line > end)
            return -1;
    }
    return found;
}

int BlockDocument::parent(int i) const
{
    return i >= 0 && i < count() ? m_items[i].entry.parent : -1;
}

RowRange BlockDocument::subtreeRange(int i) const
{
    if (i < 0 || i >= count())
        return {};
    return RowRange{i, subtreeEnd(i) - i};
}

bool BlockDocument::hasChildren(int i) const
{
    return i >= 0 && i + 1 < count() && m_items[i + 1].entry.parent == i;
}

// ---------------------------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------------------------

BlockDocument::Item BlockDocument::makeItem(const QString &own, const QString &eol, const QString &trailing)
{
    Item it;
    it.entry.uid = m_nextUid++;
    it.own = own;
    it.eol = eol;
    it.entry.trailing = trailing;
    return it;
}

void BlockDocument::derive(int i)
{
    Item &it = m_items[i];
    BlockEntry &e = it.entry;
    // A block is parsed on its own; frontmatter and page properties are only possible at the
    // top of the document.
    StructureOptions options;
    options.frontmatter = i == 0 && m_leading.isEmpty();
    options.pageProperties = true; // only frontmatter blocks before this one (checked backwards: O(1) in practice)
    for (int j = i - 1; j >= 0 && options.pageProperties; --j)
        options.pageProperties = m_items[j].entry.kind == BlockKind::Frontmatter;
    it.frontmatterAllowed = options.frontmatter;
    it.propertiesAllowed = options.pageProperties;
    const ParsedNote note = parseNoteWith(QString(), it.own, VaultConfig(), options);
    const Block *b = note.blocks.isEmpty() ? nullptr : &note.blocks.first();
    e.kind = b ? b->kind : BlockKind::Paragraph;
    e.headingLevel = b ? b->headingLevel : 0;
    e.lang = b ? b->lang : QString();
    e.taskStatus = b && b->task ? std::optional(b->task->status) : std::nullopt;
    e.blockId = b ? b->id : QString();

    const QList<text::Line> lines = text::splitLines(it.own);
    const auto lm = e.kind == BlockKind::ListItem ? matchListMarker(lines.first().text) : std::nullopt;
    if (!lm) {
        if (e.kind == BlockKind::ListItem)
            e.kind = BlockKind::Paragraph;
        e.prefix.clear();
        it.markerCol = -1;
        it.contIndent.clear();
        QStringList body;
        for (const text::Line &l : lines)
            body.append(l.text);
        e.body = body.join(u'\n');
        return;
    }
    const QString &first = lines.first().text;
    e.prefix = first.first(lm->contentStart);
    it.markerCol = lm->indent.col;
    QStringList body{first.sliced(lm->contentStart)};
    QString cont;
    bool contFound = false;
    for (qsizetype k = 1; k < lines.size(); ++k) {
        const QString &t = lines[k].text;
        const qsizetype c = charsForColumns(t, lm->contentCol);
        body.append(t.sliced(c));
        if (!contFound && !isBlank(t)) {
            contFound = true;
            if (columnsOf(QStringView(t).first(c)) >= lm->contentCol)
                cont = t.first(c);
        }
    }
    it.contIndent = cont.isEmpty() ? defaultContIndent(e.prefix) : cont;
    e.body = body.join(u'\n');
}

void BlockDocument::setOwn(int i, const QString &own)
{
    if (m_items[i].own == own)
        return;
    m_items[i].own = own;
    derive(i);
    touch(i);
}

void BlockDocument::touch(int i)
{
    ++m_items[i].version;
}

QString BlockDocument::serializeBody(const Item &item, const QString &body) const
{
    const QStringList lines = normalizeNewlines(body).split(u'\n');
    QString eol = firstEol(item.own);
    if (eol.isEmpty())
        eol = item.eol.isEmpty() ? m_eol : item.eol;
    if (item.entry.kind != BlockKind::ListItem)
        return lines.join(eol);
    QString out = (lines.first().isEmpty() ? item.entry.prefix : spacedPrefix(item.entry.prefix)) + lines.first();
    const QString cont = item.contIndent.isEmpty() ? defaultContIndent(item.entry.prefix) : item.contIndent;
    for (qsizetype k = 1; k < lines.size(); ++k)
        out += eol + (lines[k].isEmpty() ? QString() : cont + lines[k]);
    return out;
}

/// Re-derive blocks whose position-dependent parse options changed: an edit before them (a
/// removed first block, an edited frontmatter, …) can turn `key:: value` lines into page
/// properties or back into a paragraph, exactly as a fresh parse of the text would.
void BlockDocument::rederivePositional()
{
    bool onlyFrontmatterBefore = true;
    for (int i = 0; i < count(); ++i) {
        Item &it = m_items[i];
        const bool frontmatter = i == 0 && m_leading.isEmpty();
        if (frontmatter != it.frontmatterAllowed || onlyFrontmatterBefore != it.propertiesAllowed) {
            const BlockEntry old = it.entry;
            derive(i);
            const BlockEntry &now = m_items[i].entry;
            if (now.kind != old.kind || now.body != old.body || now.prefix != old.prefix
                || now.headingLevel != old.headingLevel || now.blockId != old.blockId
                || now.taskStatus != old.taskStatus)
                touch(i);
        }
        onlyFrontmatterBefore = onlyFrontmatterBefore && m_items[i].entry.kind == BlockKind::Frontmatter;
    }
}

void BlockDocument::recompute()
{
    rederivePositional();
    QList<int> stack;
    int line = countLineBreaks(m_leading);
    for (int i = 0; i < count(); ++i) {
        Item &it = m_items[i];
        BlockEntry &e = it.entry;
        const int oldDepth = e.depth;
        if (e.kind != BlockKind::ListItem) {
            stack.clear();
            e.depth = 0;
            e.parent = -1;
        } else {
            while (!stack.isEmpty() && it.markerCol <= m_items[stack.last()].markerCol)
                stack.removeLast();
            e.parent = stack.isEmpty() ? -1 : stack.last();
            e.depth = int(stack.size());
            stack.append(i);
        }
        const quint64 parentUid = e.parent >= 0 ? m_items[e.parent].entry.uid : 0;
        if (e.depth != oldDepth || parentUid != it.parentUid)
            touch(i);
        it.parentUid = parentUid;
        e.line = line;
        line += countLineBreaks(it.own) + countLineBreaks(it.eol) + countLineBreaks(e.trailing);
    }
    for (int i = 0; i < count(); ++i) {
        const bool kids = i + 1 < count() && m_items[i + 1].entry.parent == i;
        if (kids != m_items[i].hasChildren) {
            m_items[i].hasChildren = kids;
            touch(i);
        }
    }
}

BlockDocument::Snapshot BlockDocument::snapshot() const
{
    Snapshot s;
    s.uids.reserve(count());
    s.versions.reserve(count());
    for (const Item &it : m_items) {
        s.uids.append(it.entry.uid);
        s.versions.append(it.version);
    }
    return s;
}

/// A `---` rule on the very first line with any `---` / `...` line below it would make
/// everything between them YAML frontmatter on reload: a blank line before it keeps it a rule.
void BlockDocument::protectLeadingRule()
{
    if (m_items.isEmpty() || !m_leading.isEmpty() || m_items.first().entry.kind == BlockKind::Frontmatter
        || !m_items.first().own.startsWith(u"---"))
        return;
    if (findFrontmatter(toString()))
        m_leading = m_eol;
}

EditResult BlockDocument::finish(const Snapshot &before, int focus, int cursor)
{
    protectLeadingRule();
    recompute();
    EditResult r;
    r.focus = focus;
    r.cursor = cursor;
    QHash<quint64, int> oldVersion;
    for (qsizetype k = 0; k < before.uids.size(); ++k)
        oldVersion.insert(before.uids[k], before.versions[k]);
    QSet<quint64> now;
    for (const Item &it : std::as_const(m_items))
        now.insert(it.entry.uid);

    // 1. Removed rows (runs from the end so earlier indices stay valid).
    QList<quint64> work = before.uids;
    for (int k = int(work.size()) - 1; k >= 0;) {
        if (now.contains(work[k])) {
            --k;
            continue;
        }
        const int end = k;
        while (k >= 0 && !now.contains(work[k]))
            --k;
        r.changes.append({BlockChange::Type::Removed, k + 1, end - k, -1});
        work.remove(k + 1, end - k);
    }
    // 2. Moves: bring the remaining rows into their new order (always moving rows up).
    QList<quint64> target;
    for (const Item &it : std::as_const(m_items)) {
        if (oldVersion.contains(it.entry.uid))
            target.append(it.entry.uid);
    }
    for (int p = 0; p < target.size(); ++p) {
        if (work[p] == target[p])
            continue;
        const int q = int(work.indexOf(target[p], p + 1));
        int len = 1;
        while (p + len < target.size() && q + len < work.size() && work[q + len] == target[p + len])
            ++len;
        r.changes.append({BlockChange::Type::Moved, q, len, p});
        const QList<quint64> moved = work.mid(q, len);
        work.remove(q, len);
        for (int k = 0; k < len; ++k)
            work.insert(p + k, moved[k]);
        p += len - 1;
    }
    // 3. Inserted rows (runs in ascending order of their final position).
    for (int i = 0; i < count();) {
        if (oldVersion.contains(m_items[i].entry.uid)) {
            ++i;
            continue;
        }
        const int first = i;
        while (i < count() && !oldVersion.contains(m_items[i].entry.uid))
            ++i;
        r.changes.append({BlockChange::Type::Inserted, first, i - first, -1});
    }
    // 4. Changed rows.
    const auto changed = [&](int i) {
        const auto it = oldVersion.constFind(m_items[i].entry.uid);
        return it != oldVersion.constEnd() && *it != m_items[i].version;
    };
    for (int i = 0; i < count();) {
        if (!changed(i)) {
            ++i;
            continue;
        }
        const int first = i;
        while (i < count() && changed(i))
            ++i;
        r.changes.append({BlockChange::Type::Changed, first, i - first, -1});
    }
    return r;
}

// ---------------------------------------------------------------------------------------------
// Outline helpers
// ---------------------------------------------------------------------------------------------

/// Number of frontmatter / page-property blocks at the top: they only work there, so they never
/// move and nothing is moved or inserted above them.
int BlockDocument::pinnedCount() const
{
    int n = 0;
    while (n < count() && m_items[n].entry.kind == BlockKind::Frontmatter)
        ++n;
    return n;
}

bool BlockDocument::isListItem(int i) const
{
    return m_items[i].entry.kind == BlockKind::ListItem;
}

bool BlockDocument::isTextBlock(int i) const
{
    const BlockKind k = m_items[i].entry.kind;
    return k == BlockKind::ListItem || k == BlockKind::Paragraph || k == BlockKind::Heading;
}

int BlockDocument::subtreeEnd(int i) const
{
    if (!isListItem(i))
        return i + 1;
    const int depth = m_items[i].entry.depth;
    int j = i + 1;
    while (j < count() && isListItem(j) && m_items[j].entry.depth > depth)
        ++j;
    return j;
}

int BlockDocument::prevSibling(int i) const
{
    const int depth = m_items[i].entry.depth;
    const int parent = m_items[i].entry.parent;
    for (int j = i - 1; j >= 0; --j) {
        const BlockEntry &e = m_items[j].entry;
        if (e.depth < depth)
            return -1;
        if (e.depth == depth)
            return e.parent == parent ? j : -1;
    }
    return -1;
}

int BlockDocument::nextSibling(int i) const
{
    const int j = subtreeEnd(i);
    if (j >= count())
        return -1;
    const BlockEntry &e = m_items[j].entry;
    return e.depth == m_items[i].entry.depth && e.parent == m_items[i].entry.parent ? j : -1;
}

QString BlockDocument::leadingWhitespace(int i) const
{
    if (!isListItem(i))
        return {};
    const QString &own = m_items[i].own;
    return own.first(indentOf(own).chars);
}

QString BlockDocument::indentUnit() const
{
    for (int i = 0; i < count(); ++i) {
        const int p = m_items[i].entry.parent;
        if (!isListItem(i) || p < 0)
            continue;
        const QString ws = leadingWhitespace(i);
        const QString pws = leadingWhitespace(p);
        if (ws.size() > pws.size() && ws.startsWith(pws))
            return ws.sliced(pws.size());
    }
    return u"\t"_s;
}

QString BlockDocument::childIndent(int parent) const
{
    QString unit = indentUnit();
    if (!unit.contains(u'\t')) {
        // With spaces a child must reach the parent's content column (CommonMark).
        const auto lm = markerOfPrefix(m_items[parent].entry.prefix);
        const int width = lm ? lm->contentCol - lm->indent.col : 2;
        if (unit.size() < width)
            unit = QString(width, u' ');
    }
    return leadingWhitespace(parent) + unit;
}

QString BlockDocument::siblingPrefix(int ref, bool increment) const
{
    const QString prefix = spacedPrefix(m_items[ref].entry.prefix);
    const auto lm = markerOfPrefix(prefix);
    if (!lm)
        return u"- "_s;
    if (!lm->ordered)
        return prefix;
    int number = lm->number;
    if (increment) {
        // Lazy numbering ("1. 1. 1.") keeps the same number.
        bool lazy = false;
        const int prev = prevSibling(ref);
        const int next = nextSibling(ref);
        for (int other : {prev, next}) {
            if (other < 0 || !isListItem(other))
                continue;
            const auto olm = markerOfPrefix(m_items[other].entry.prefix);
            if (olm && olm->ordered && olm->number == number)
                lazy = true;
        }
        if (!lazy)
            ++number;
    }
    return prefix.first(lm->markerFrom) + QString::number(number) + prefix.sliced(lm->markerEnd - 1);
}

QString BlockDocument::childPrefix(int parent) const
{
    if (hasChildren(parent))
        return siblingPrefix(parent + 1, false);
    const auto lm = markerOfPrefix(m_items[parent].entry.prefix);
    const QString marker = lm && lm->ordered ? u"1"_s + lm->bullet : lm ? QString(lm->bullet) : u"-"_s;
    return childIndent(parent) + marker + u' ';
}

void BlockDocument::shiftIndent(int first, int end, const QString &from, const QString &to)
{
    if (from == to)
        return;
    for (int i = first; i < end; ++i) {
        QString out;
        for (const text::Line &l : text::splitLines(m_items[i].own))
            out += (isBlank(l.text) ? l.text : reindentLine(l.text, from, to)) + l.eol;
        setOwn(i, out);
    }
}

void BlockDocument::renumberChildren(int parent, quint64 restartUid)
{
    QList<int> children;
    for (int i = parent + 1; i < count(); ++i) {
        const BlockEntry &e = m_items[i].entry;
        if (parent >= 0 && e.depth <= m_items[parent].entry.depth)
            break;
        if (e.parent == parent && (parent >= 0 || e.depth == 0))
            children.append(i);
    }
    // Runs of consecutive ordered items with the same delimiter.
    qsizetype k = 0;
    while (k < children.size()) {
        const auto lm = isListItem(children[k]) ? markerOfPrefix(at(children[k]).prefix) : std::nullopt;
        if (!lm || !lm->ordered) {
            ++k;
            continue;
        }
        QList<int> run{children[k]};
        QList<int> numbers{lm->number};
        qsizetype j = k + 1;
        for (; j < children.size(); ++j) {
            const auto next = isListItem(children[j]) ? markerOfPrefix(at(children[j]).prefix) : std::nullopt;
            if (!next || !next->ordered || next->bullet != lm->bullet)
                break;
            run.append(children[j]);
            numbers.append(next->number);
        }
        k = j;
        const bool lazy = run.size() >= 2
                          && std::all_of(numbers.begin(), numbers.end(), [&](int n) { return n == numbers.first(); });
        if (lazy)
            continue;
        // A run that an operation has just started (its first item is `restartUid`) counts from 1.
        // Otherwise the run keeps its smallest number as start (a moved first item keeps "1.").
        const int start =
                restartUid && at(run.first()).uid == restartUid ? 1 : *std::min_element(numbers.begin(), numbers.end());
        for (qsizetype r = 0; r < run.size(); ++r) {
            const int want = start + int(r);
            if (numbers[r] == want)
                continue;
            const int i = run[r];
            const QString &own = m_items[i].own;
            const auto mlm = matchListMarker(own);
            if (!mlm)
                continue;
            setOwn(i, own.first(mlm->markerFrom) + QString::number(want) + own.sliced(mlm->markerEnd - 1));
        }
    }
}

void BlockDocument::renumberAround(quint64 uid)
{
    const int i = indexOfUid(uid);
    if (i < 0)
        return;
    renumberChildren(m_items[i].entry.parent);
    if (isListItem(i))
        renumberChildren(i);
}

void BlockDocument::ensureSeparated(int i)
{
    if (i < 0 || i + 1 >= count())
        return;
    // An indented non-list block after a list item would continue that item: unindent it.
    if (isListItem(i) && !isListItem(i + 1) && at(i + 1).kind != BlockKind::Frontmatter) {
        const QString &next = m_items[i + 1].own;
        const qsizetype ws = indentOf(next).chars;
        if (ws > 0)
            shiftIndent(i + 1, i + 2, next.first(ws), QString());
    }
    Item &it = m_items[i];
    if (!it.entry.trailing.isEmpty() || !needsBlankLine(it.entry.kind, m_items[i + 1].entry.kind))
        return;
    if (it.eol.isEmpty())
        it.eol = m_eol;
    it.entry.trailing = m_eol;
    touch(i);
}

void BlockDocument::fixEnds()
{
    if (!m_items.isEmpty() && !m_leading.isEmpty() && !endsWithEol(m_leading))
        m_leading += m_eol;
    for (int i = 0; i + 1 < count(); ++i) {
        Item &it = m_items[i];
        if (it.eol.isEmpty())
            it.eol = m_eol;
        if (!it.entry.trailing.isEmpty() && !endsWithEol(it.entry.trailing)) {
            it.entry.trailing += m_eol;
            touch(i);
        }
    }
    if (m_items.isEmpty())
        return;
    Item &last = m_items.last();
    const bool tailEol = endsWithEol(last.entry.trailing.isEmpty() ? last.eol : last.entry.trailing);
    if (m_finalNewline && !tailEol) {
        if (last.entry.trailing.isEmpty())
            last.eol = m_eol;
        else
            last.entry.trailing += m_eol;
    } else if (!m_finalNewline && tailEol && last.entry.trailing.isEmpty()) {
        last.eol.clear();
    }
}

void BlockDocument::insertItem(int pos, Item item, bool takeTrailingFromPrevious)
{
    if (takeTrailingFromPrevious && pos > 0) {
        Item &prev = m_items[pos - 1];
        item.entry.trailing = prev.entry.trailing;
        item.eol = prev.eol.isEmpty() ? QString() : m_eol;
        if (!prev.entry.trailing.isEmpty())
            touch(pos - 1);
        prev.entry.trailing.clear();
        if (prev.eol.isEmpty())
            prev.eol = m_eol;
    } else {
        item.eol = m_eol;
        item.entry.trailing.clear();
    }
    m_items.insert(pos, std::move(item));
}

void BlockDocument::removeItems(int first, int end)
{
    m_items.remove(first, end - first);
}

void BlockDocument::swapRanges(int a0, int b0, int b1)
{
    const QString wsA = leadingWhitespace(a0);
    const QString wsB = leadingWhitespace(b0);
    const bool bothLists = isListItem(a0) && isListItem(b0);
    QList<Item> a = m_items.mid(a0, b0 - a0);
    QList<Item> b = m_items.mid(b0, b1 - b0);
    // Separators stay where they are: the one between the two ranges and the one after them.
    const QString sepA = a.last().entry.trailing;
    const QString eolA = a.last().eol;
    const QString sepB = b.last().entry.trailing;
    const QString eolB = b.last().eol;
    if (b.last().entry.trailing != sepA)
        ++b.last().version;
    b.last().entry.trailing = sepA;
    b.last().eol = eolA;
    if (a.last().entry.trailing != sepB)
        ++a.last().version;
    a.last().entry.trailing = sepB;
    a.last().eol = eolB;
    const int nb = int(b.size());
    for (int k = 0; k < nb; ++k)
        m_items[a0 + k] = b[k];
    for (int k = 0; k < a.size(); ++k)
        m_items[a0 + nb + k] = a[k];
    // Keep each position's indentation so siblings stay siblings.
    if (bothLists && wsA != wsB) {
        shiftIndent(a0, a0 + nb, wsB, wsA);
        shiftIndent(a0 + nb, b1, wsA, wsB);
    }
}

// ---------------------------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------------------------

EditResult BlockDocument::setBody(int i, const QString &text)
{
    if (i < 0 || i >= count())
        return {};
    const QString body = normalizeNewlines(text);
    if (body == at(i).body)
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    setOwn(i, serializeBody(m_items[i], body));
    recompute();
    renumberAround(uid);
    ensureSeparated(i - 1);
    ensureSeparated(i);
    fixEnds();
    return finish(before, -1, -1);
}

EditResult BlockDocument::split(int i, int cursor, bool collapsed)
{
    if (i < 0 || i >= count() || !isTextBlock(i))
        return {};
    const Snapshot before = snapshot();
    const BlockEntry e = at(i);
    const int c = std::clamp(cursor, 0, int(e.body.size()));
    // Line breaks at the split point are dropped: Enter on an empty line of a multi-line block
    // must not leave blank lines at the end of one block and the start of the other.
    QStringView headView = QStringView(e.body).first(c);
    QStringView tailView = QStringView(e.body).sliced(c);
    while (headView.endsWith(u'\n'))
        headView.chop(1);
    while (tailView.startsWith(u'\n'))
        tailView = tailView.sliced(1);
    const QString head = headView.toString();
    const QString tail = tailView.toString();

    if (e.kind == BlockKind::ListItem) {
        const QString cont = taskContinuation(e.body);
        Item x = makeItem(QString(), m_eol, QString());
        x.entry.kind = BlockKind::ListItem;
        if (c == 0 && !e.body.isEmpty()) {
            // Enter at the start: an empty block above, the caret stays in this one.
            x.entry.prefix = e.prefix;
            x.contIndent = m_items[i].contIndent;
            x.own = serializeBody(x, cont);
            insertItem(i, std::move(x), false);
            derive(i);
            recompute();
            renumberAround(at(i).uid);
            ensureSeparated(i - 1);
            fixEnds();
            return finish(before, indexOfUid(e.uid), 0);
        }
        setOwn(i, serializeBody(m_items[i], head));
        const bool asChild = !collapsed && hasChildren(i);
        x.entry.prefix = asChild ? childPrefix(i) : siblingPrefix(i, true);
        x.contIndent = defaultContIndent(x.entry.prefix);
        x.own = serializeBody(x, cont.isEmpty() ? tail : cont + trimStartBlanks(tail));
        const quint64 newUid = x.entry.uid;
        const int pos = asChild ? i + 1 : subtreeEnd(i);
        insertItem(pos, std::move(x), true);
        derive(pos);
        recompute();
        renumberAround(newUid);
        ensureSeparated(pos - 1);
        ensureSeparated(pos);
        fixEnds();
        return finish(before, indexOfUid(newUid), int(cont.size()));
    }

    // Paragraph / heading: the text after the cursor becomes a paragraph.
    if (c == 0 && !e.body.isEmpty()) {
        insertItem(i, makeItem(QString(), m_eol, QString()), false);
        derive(i);
        recompute();
        ensureSeparated(i - 1);
        ensureSeparated(i);
        fixEnds();
        return finish(before, i + 1, 0);
    }
    setOwn(i, serializeBody(m_items[i], head));
    Item x = makeItem(QString(), m_eol, QString());
    x.own = serializeBody(x, tail);
    insertItem(i + 1, std::move(x), true);
    derive(i + 1);
    recompute();
    ensureSeparated(i);
    ensureSeparated(i + 1);
    fixEnds();
    return finish(before, i + 1, 0);
}

EditResult BlockDocument::insertAfter(int i, const QString &body, int depth, bool listItem)
{
    if (i < -1 || i >= count())
        return {};
    const Snapshot before = snapshot();
    const QString text = normalizeNewlines(body);
    Item x = makeItem(QString(), m_eol, QString());
    const quint64 uid = x.entry.uid;
    int pos = 0;
    if (!listItem) {
        if (i >= 0) {
            int root = i;
            while (m_items[root].entry.parent >= 0)
                root = m_items[root].entry.parent;
            pos = subtreeEnd(root);
        }
        x.own = serializeBody(x, text);
    } else {
        x.entry.kind = BlockKind::ListItem;
        if (i < 0 || !isListItem(i)) {
            pos = i + 1;
            int ref = -1;
            for (int j = 0; j < count() && ref < 0; ++j) {
                if (isListItem(j) && at(j).depth == 0)
                    ref = j;
            }
            x.entry.prefix = ref >= 0 ? siblingPrefix(ref, false) : u"- "_s;
            if (const auto lm = markerOfPrefix(x.entry.prefix); lm && lm->ordered)
                x.entry.prefix = x.entry.prefix.first(lm->markerFrom) + u'1' + x.entry.prefix.sliced(lm->markerEnd - 1);
        } else if (depth > at(i).depth) {
            pos = i + 1;
            x.entry.prefix = childPrefix(i);
        } else {
            int anc = i;
            while (at(anc).depth > std::max(depth, 0) && at(anc).parent >= 0)
                anc = at(anc).parent;
            pos = subtreeEnd(anc);
            x.entry.prefix = siblingPrefix(anc, true);
        }
        x.contIndent = defaultContIndent(x.entry.prefix);
        x.own = serializeBody(x, text);
    }
    // Never before or between the frontmatter / page-property blocks (they only work at the top).
    const bool clamped = pos < pinnedCount();
    pos = std::max(pos, pinnedCount());
    insertItem(pos, std::move(x), pos > 0 && !clamped);
    derive(pos);
    recompute();
    renumberAround(uid);
    ensureSeparated(pos - 1);
    ensureSeparated(pos);
    fixEnds();
    const int focus = indexOfUid(uid);
    return finish(before, focus, int(at(focus).body.size()));
}

EditResult BlockDocument::mergeWithPrevious(int i)
{
    if (i <= 0 || i >= count())
        return {};
    const int p = i - 1;
    const Snapshot before = snapshot();
    const quint64 prevUid = at(p).uid;
    const int caret = isTextBlock(p) ? int(at(p).body.size()) : -1;
    if (isTextBlock(i) && at(i).body.isEmpty()) {
        Item &prev = m_items[p];
        if (m_items[i].eol.isEmpty() && prev.entry.trailing.isEmpty())
            prev.eol.clear();
        removeItems(i, i + 1);
    } else if (isTextBlock(i) && isTextBlock(p)) {
        const QString joined = at(p).body + at(i).body;
        Item &prev = m_items[p];
        prev.entry.trailing = m_items[i].entry.trailing;
        prev.eol = m_items[i].eol;
        setOwn(p, serializeBody(prev, joined));
        touch(p);
        removeItems(i, i + 1);
    } else {
        EditResult r;
        r.focus = p;
        return r;
    }
    recompute();
    renumberAround(prevUid);
    const int np = indexOfUid(prevUid);
    ensureSeparated(np);
    fixEnds();
    return finish(before, np, caret);
}

EditResult BlockDocument::remove(int i, bool withChildren)
{
    if (i < 0 || i >= count())
        return {};
    const Snapshot before = snapshot();
    const int parentIdx = at(i).parent;
    const quint64 parentUid = parentIdx >= 0 ? at(parentIdx).uid : 0;
    const int end = withChildren ? subtreeEnd(i) : i + 1;
    const bool wasLast = end == count();
    const bool noFinalBreak = wasLast && m_items[end - 1].eol.isEmpty();
    removeItems(i, end);
    if (wasLast && i > 0 && noFinalBreak && m_items[i - 1].entry.trailing.isEmpty())
        m_items[i - 1].eol.clear();
    recompute();
    if (parentUid)
        renumberChildren(indexOfUid(parentUid));
    else
        renumberChildren(-1);
    if (!withChildren && i < count())
        renumberAround(at(i).uid);
    ensureSeparated(i - 1);
    fixEnds();
    const int focus = i > 0 ? i - 1 : (i < count() ? i : -1);
    return finish(before, focus, focus >= 0 && isTextBlock(focus) ? int(at(focus).body.size()) : -1);
}

EditResult BlockDocument::convertToList(int i)
{
    if (i < 0 || i >= count() || isListItem(i) || at(i).kind == BlockKind::Frontmatter || at(i).kind == BlockKind::Rule)
        return {};
    const Snapshot before = snapshot();
    QString out;
    const QList<text::Line> lines = text::splitLines(m_items[i].own);
    for (qsizetype k = 0; k < lines.size(); ++k) {
        const QString &t = lines[k].text;
        if (k == 0)
            out += u"- "_s + trimStartBlanks(t);
        else
            out += isBlank(t) ? t : u"  "_s + t;
        out += lines[k].eol;
    }
    setOwn(i, out);
    recompute();
    renumberChildren(-1);
    ensureSeparated(i - 1);
    ensureSeparated(i);
    fixEnds();
    return finish(before, i, -1);
}

EditResult BlockDocument::indent(int i)
{
    if (i < 0 || i >= count())
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    if (!isListItem(i)) {
        if (at(i).kind == BlockKind::Frontmatter || at(i).kind == BlockKind::Rule)
            return {};
        QString out;
        const QList<text::Line> lines = text::splitLines(m_items[i].own);
        for (qsizetype k = 0; k < lines.size(); ++k) {
            const QString &t = lines[k].text;
            out += (k == 0 ? u"- "_s + trimStartBlanks(t) : isBlank(t) ? t : u"  "_s + t) + lines[k].eol;
        }
        setOwn(i, out);
        recompute();
        ensureSeparated(i - 1);
        ensureSeparated(i);
    }
    const int s = prevSibling(i);
    if (s >= 0 && isListItem(s)) {
        const int parentIdx = at(i).parent;
        const quint64 oldParentUid = parentIdx >= 0 ? at(parentIdx).uid : 0;
        const quint64 siblingUid = at(s).uid;
        QString newWs;
        int lastChild = -1;
        for (int j = s + 1; j < i; ++j) {
            if (at(j).parent == s)
                lastChild = j;
        }
        newWs = lastChild >= 0 ? leadingWhitespace(lastChild) : childIndent(s);
        shiftIndent(i, subtreeEnd(i), leadingWhitespace(i), newWs);
        recompute();
        renumberChildren(oldParentUid ? indexOfUid(oldParentUid) : -1);
        renumberChildren(indexOfUid(siblingUid), uid);
    }
    fixEnds();
    return finish(before, indexOfUid(uid), -1);
}

EditResult BlockDocument::outdent(int i)
{
    if (i < 0 || i >= count() || !isListItem(i) || at(i).parent < 0)
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    const int p = at(i).parent;
    const quint64 parentUid = at(p).uid;
    const quint64 firstChildBefore = hasChildren(i) ? at(i + 1).uid : 0;
    const QString newWs = leadingWhitespace(p);
    shiftIndent(i, subtreeEnd(i), leadingWhitespace(i), newWs);
    recompute();
    renumberChildren(indexOfUid(parentUid));
    const int ni = indexOfUid(uid);
    renumberChildren(at(ni).parent);
    // Former following siblings that now start this item's children are numbered from 1.
    const quint64 firstChild = hasChildren(ni) ? at(ni + 1).uid : 0;
    renumberChildren(ni, firstChild != firstChildBefore ? firstChild : 0);
    fixEnds();
    return finish(before, indexOfUid(uid), -1);
}

EditResult BlockDocument::moveUp(int i)
{
    if (i < 0 || i >= count())
        return {};
    const int s = prevSibling(i);
    if (s < 0 || s < pinnedCount())
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    const int end = subtreeEnd(i);
    swapRanges(s, i, end);
    recompute();
    const int ni = indexOfUid(uid);
    renumberAround(uid);
    ensureSeparated(s - 1);
    ensureSeparated(s + (end - i) - 1);
    ensureSeparated(end - 1);
    fixEnds();
    return finish(before, ni, -1);
}

EditResult BlockDocument::moveDown(int i)
{
    if (i < 0 || i >= count())
        return {};
    const int t = nextSibling(i);
    if (t < 0 || i < pinnedCount())
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    const int end = subtreeEnd(t);
    swapRanges(i, t, end);
    recompute();
    renumberAround(uid);
    ensureSeparated(i - 1);
    ensureSeparated(i + (end - t) - 1);
    ensureSeparated(end - 1);
    fixEnds();
    return finish(before, indexOfUid(uid), -1);
}

EditResult BlockDocument::applyTaskEdit(int i, int lineInBlock, const QString &newOwn)
{
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    const QList<text::Line> lines = text::splitLines(newOwn);
    const int inserted = int(lines.size()) - int(text::splitLines(m_items[i].own).size());
    if (inserted > 0 && lineInBlock == 0 && matchListMarker(lines.first().text)) {
        // The next occurrence of a recurring list item was inserted above: it becomes its own
        // block (inside a quote or callout it stays part of that block).
        setOwn(i, joinLineRange(lines, inserted, int(lines.size()), false));
        insertItem(i, makeItem(joinLineRange(lines, 0, inserted, false), m_eol, QString()), false);
        derive(i);
    } else {
        setOwn(i, newOwn);
    }
    recompute();
    renumberAround(uid);
    const int ni = indexOfUid(uid);
    ensureSeparated(ni - 1);
    ensureSeparated(ni);
    fixEnds();
    return finish(before, ni, -1);
}

EditResult BlockDocument::toggleTask(int i, QDate today, int lineInBlock)
{
    if (i < 0 || i >= count())
        return {};
    const QString out = markdown::toggleTask(m_items[i].own, lineInBlock, today);
    if (out == m_items[i].own)
        return {};
    return applyTaskEdit(i, lineInBlock, out);
}

EditResult BlockDocument::setTaskStatus(int i, TaskStatus status, QDate today, int lineInBlock)
{
    if (i < 0 || i >= count())
        return {};
    const QString out = markdown::setTaskStatus(m_items[i].own, lineInBlock, status, today);
    if (out == m_items[i].own)
        return {};
    return applyTaskEdit(i, lineInBlock, out);
}

BlockIdResult BlockDocument::ensureBlockId(int i, const std::function<bool(const QString &)> &isTaken)
{
    if (i < 0 || i >= count())
        return {};
    if (!at(i).blockId.isEmpty())
        return {at(i).blockId, {}};
    QSet<QString> used;
    for (const Item &it : std::as_const(m_items)) {
        if (!it.entry.blockId.isEmpty())
            used.insert(it.entry.blockId);
    }
    const EnsuredBlockId r = markdown::ensureBlockId(
            m_items[i].own, 0, [&](const QString &id) { return used.contains(id) || (isTaken && isTaken(id)); });
    if (r.id.isEmpty())
        return {};
    const Snapshot before = snapshot();
    setOwn(i, r.content);
    recompute();
    fixEnds();
    return {r.id, finish(before, -1, -1)};
}

} // namespace brulion::markdown
