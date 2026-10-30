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
#include <array>
#include <atomic>

namespace brulion::markdown {

namespace detail {

/// The parts of a document outside its blocks.
struct BlockEdges {
    QString bom;
    QString leading;
    QString eol;
    bool finalNewline = true;
    friend bool operator==(const BlockEdges &, const BlockEdges &) = default;
};

/// What a version keeps of one block: its identity and exact text (everything else is derived).
struct BlockSource {
    quint64 uid = 0;
    QString own;
    QString eol;
    QString trailing;
};

struct BlockVersionData {
    QList<BlockSource> blocks;
    BlockEdges edges;
};

/// A block in both versions of a delta. Index v of each array is a version (0 older, 1 newer):
/// its own text differs in [from, from + text[v].size()) (empty texts when only the block's place,
/// terminator or blank lines changed); `size` is the size of the whole own text.
struct BlockOwnEdit {
    qsizetype from = 0;
    std::array<QString, 2> text;
    std::array<qsizetype, 2> size{};
    std::array<QString, 2> eol;
    std::array<QString, 2> trailing;
};

/// One version's side of a delta.
struct BlockDeltaSide {
    int count = 0; ///< blocks of the whole document
    BlockEdges edges;
    QList<quint64> run;               ///< the run's uids in order (from BlockDeltaData::first)
    QHash<quint64, BlockSource> only; ///< blocks of the run that the other version does not have
};

struct BlockDeltaData {
    int first = 0; ///< the run's first block (the same index in both versions)
    std::array<BlockDeltaSide, 2> side;
    QHash<quint64, BlockOwnEdit> both;
};

} // namespace detail

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// Block uids are unique in the process: an undo step brings blocks back with their uids, and
/// BlockListModel::replaceContent() gives the blocks it keeps their old uids.
quint64 nextUid()
{
    static std::atomic<quint64> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

/// Equal strings, decided by their shared data first (the untouched blocks of two versions share
/// their strings).
bool sameText(const QString &a, const QString &b)
{
    return (a.constData() == b.constData() && a.size() == b.size()) || a == b;
}

bool sameSource(const BlockSource &a, const BlockSource &b)
{
    return a.uid == b.uid && sameText(a.own, b.own) && sameText(a.eol, b.eol) && sameText(a.trailing, b.trailing);
}

/// The lengths of the common prefix and suffix of two strings (not overlapping).
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

/// The delta between two runs of blocks that start at block `first` of their versions (the blocks
/// before and after the runs are the same in both); equal blocks at the ends of the runs are left
/// out of it.
BlockDeltaData diffRuns(int first, const QList<BlockSource> &older, const QList<BlockSource> &newer, int olderCount,
                        int newerCount, const BlockEdges &olderEdges, const BlockEdges &newerEdges)
{
    const qsizetype max = std::min(older.size(), newer.size());
    qsizetype p = 0;
    while (p < max && sameSource(older[p], newer[p]))
        ++p;
    qsizetype s = 0;
    while (s < max - p && sameSource(older[older.size() - 1 - s], newer[newer.size() - 1 - s]))
        ++s;
    BlockDeltaData d;
    d.first = first + int(p);
    d.side[0].count = olderCount;
    d.side[1].count = newerCount;
    d.side[0].edges = olderEdges;
    d.side[1].edges = newerEdges;
    QHash<quint64, qsizetype> inNewer;
    for (qsizetype k = p; k < newer.size() - s; ++k)
        inNewer.insert(newer[k].uid, k);
    for (qsizetype k = p; k < older.size() - s; ++k) {
        const BlockSource &o = older[k];
        d.side[0].run.append(o.uid);
        const auto found = inNewer.constFind(o.uid);
        if (found == inNewer.constEnd()) {
            d.side[0].only.insert(o.uid, o);
            continue;
        }
        const BlockSource &n = newer[*found];
        BlockOwnEdit e;
        if (!sameText(o.own, n.own)) {
            const auto [cp, cs] = commonEnds(o.own, n.own);
            e.from = cp;
            e.text = {o.own.mid(cp, o.own.size() - cp - cs), n.own.mid(cp, n.own.size() - cp - cs)};
        }
        e.size = {o.own.size(), n.own.size()};
        e.eol = {o.eol, n.eol};
        e.trailing = {o.trailing, n.trailing};
        d.both.insert(o.uid, e);
    }
    for (qsizetype k = p; k < newer.size() - s; ++k) {
        d.side[1].run.append(newer[k].uid);
        if (!d.both.contains(newer[k].uid))
            d.side[1].only.insert(newer[k].uid, newer[k]);
    }
    return d;
}

/// The run of version `to` of a delta from the run of version `from` (`run`: its blocks as they
/// are); nullopt when `run` is not exactly that version's run (other uids, order or text).
std::optional<QList<BlockSource>> convertRun(const BlockDeltaData &d, int from, int to, const QList<BlockSource> &run)
{
    const BlockDeltaSide &have = d.side[from];
    if (run.size() != have.run.size())
        return std::nullopt;
    QHash<quint64, qsizetype> kept; // blocks in both versions: their index in `run`
    for (qsizetype k = 0; k < run.size(); ++k) {
        const BlockSource &b = run[k];
        if (b.uid != have.run[k])
            return std::nullopt;
        if (const auto only = have.only.constFind(b.uid); only != have.only.constEnd()) {
            if (!sameSource(*only, b))
                return std::nullopt;
            continue;
        }
        const auto e = d.both.constFind(b.uid);
        if (e == d.both.constEnd() || b.own.size() != e->size[from] || e->from + e->text[from].size() > b.own.size()
            || QStringView(b.own).sliced(e->from, e->text[from].size()) != e->text[from] || b.eol != e->eol[from]
            || b.trailing != e->trailing[from])
            return std::nullopt;
        kept.insert(b.uid, k);
    }
    const BlockDeltaSide &want = d.side[to];
    QList<BlockSource> out;
    out.reserve(want.run.size());
    for (const quint64 uid : want.run) {
        if (const auto only = want.only.constFind(uid); only != want.only.constEnd()) {
            out.append(*only);
            continue;
        }
        const auto e = d.both.constFind(uid);
        const auto at = kept.constFind(uid);
        if (e == d.both.constEnd() || at == kept.constEnd())
            return std::nullopt;
        const BlockSource &b = run[*at];
        BlockSource t{uid, b.own, e->eol[to], e->trailing[to]};
        if (e->text[from] != e->text[to])
            t.own = b.own.first(e->from) + e->text[to] + b.own.sliced(e->from + e->text[from].size());
        out.append(t);
    }
    return out;
}

bool endsWithEol(QStringView s)
{
    return s.endsWith(u'\n') || s.endsWith(u'\r');
}

/// Only spaces, tabs and line breaks (blank lines).
bool isBlankLines(QStringView s)
{
    return std::all_of(s.begin(), s.end(), [](QChar c) { return c == u'\n' || c == u'\r' || isSpaceOrTab(c); });
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

/// A block's text with its position-dependent parse options: everything derive() reads.
struct DeriveKey {
    QStringView own;
    bool frontmatter = false;
    bool properties = false;
};

bool operator==(const DeriveKey &a, const DeriveKey &b)
{
    return a.frontmatter == b.frontmatter && a.properties == b.properties && a.own == b.own;
}

size_t qHash(const DeriveKey &key, size_t seed = 0)
{
    return qHashMulti(seed, key.own, key.frontmatter, key.properties);
}

} // namespace

BlockDocument::Version::Version() = default;

BlockDocument::Delta::Delta() = default;

bool BlockDocument::Delta::isEmpty() const
{
    return !d || (d->side[0].run.isEmpty() && d->side[1].run.isEmpty() && d->side[0].edges == d->side[1].edges);
}

qsizetype BlockDocument::Delta::size() const
{
    if (!d)
        return 0;
    qsizetype n = 0;
    for (const BlockDeltaSide &side : d->side) {
        for (const BlockSource &b : side.only)
            n += b.own.size() + b.eol.size() + b.trailing.size();
    }
    for (const BlockOwnEdit &e : d->both) {
        n += e.text[0].size() + e.text[1].size();
        if (!sameText(e.eol[0], e.eol[1]))
            n += e.eol[0].size() + e.eol[1].size();
        if (!sameText(e.trailing[0], e.trailing[1]))
            n += e.trailing[0].size() + e.trailing[1].size();
    }
    if (!(d->side[0].edges == d->side[1].edges)) {
        for (const BlockDeltaSide &side : d->side)
            n += side.edges.bom.size() + side.edges.leading.size() + side.edges.eol.size();
    }
    return n;
}

BlockDocument::BlockDocument() = default;

BlockDocument::BlockDocument(const QString &content, const BlockDocument *reuse)
{
    setContent(content, reuse);
}

void BlockDocument::setContent(const QString &content, const BlockDocument *reuse)
{
    // The parsed blocks of `reuse` by text and options (`previous` shares its list: `reuse` may
    // be this document; the keys point into its strings).
    const QList<Item> previous = reuse ? reuse->m_items : QList<Item>();
    QHash<DeriveKey, qsizetype> parsed;
    parsed.reserve(previous.size());
    for (qsizetype k = 0; k < previous.size(); ++k) {
        const Item &it = previous[k];
        parsed.insert(DeriveKey{it.own, it.frontmatterAllowed, it.propertiesAllowed}, k);
    }

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
    for (int i = 0; i < count(); ++i) {
        const PositionalOptions options = positionalOptions(i);
        const auto known = parsed.constFind(DeriveKey{m_items[i].own, options.frontmatter, options.properties});
        if (known != parsed.constEnd())
            takeDerived(m_items[i], previous[*known]);
        else
            derive(i);
    }
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
// Blank blocks (empty paragraphs)
// ---------------------------------------------------------------------------------------------

QList<BlankBlock> BlockDocument::blankBlocks() const
{
    QList<BlankBlock> out;
    for (const Item &it : m_items) {
        if (it.entry.kind == BlockKind::Paragraph && isBlankLines(it.own))
            out.append({it.entry.line, countLineBreaks(it.own) + 1});
    }
    return out;
}

int BlockDocument::restoreBlankBlocks(const QList<BlankBlock> &blocks)
{
    QList<BlankBlock> wanted = blocks;
    std::sort(wanted.begin(), wanted.end(), [](const BlankBlock &x, const BlankBlock &y) { return x.line < y.line; });
    int made = 0;
    for (const BlankBlock &b : std::as_const(wanted)) {
        if (b.line < 0 || b.lines < 1)
            continue;
        // The run of blank lines that holds the block: the blank lines before the first block
        // (k = -1) or the ones after block k. A block made before (an overlapping or repeated
        // entry) is block k, its lines are no run.
        int k = -1;
        while (k + 1 < count() && m_items[k + 1].entry.line <= b.line)
            ++k;
        const int start = k < 0 ? 0
                                : m_items[k].entry.line + countLineBreaks(m_items[k].own)
                                          + countLineBreaks(m_items[k].eol);
        if (b.line < start)
            continue; // a line of the block's own text
        QString &run = k < 0 ? m_leading : m_items[k].entry.trailing;
        const QList<text::Line> runLines = text::splitLines(run);
        // splitLines() ends with an empty line after a final terminator: no line of the run.
        const int runCount = int(runLines.size())
                             - (runLines.last().text.isEmpty() && runLines.last().eol.isEmpty() ? 1 : 0);
        const int index = b.line - start;
        const int end = index + b.lines; // one past the block's last line
        Item x = makeItem(QString(), QString(), QString());
        if (end <= runCount) {
            x.own = joinLineRange(runLines, index, end, false);
            x.eol = runLines[end - 1].eol;
            x.entry.trailing = joinLineRange(runLines, end, int(runLines.size()), true);
        } else {
            // Its last line one past the end of a text that ends with a line break: an empty
            // paragraph at the very end of a page without a final newline (fixEnds() writes it
            // that way).
            const bool last = k == count() - 1;
            const bool terminated = run.isEmpty() ? (k < 0 || !m_items[k].eol.isEmpty()) : endsWithEol(run);
            if (!last || end != runCount + 1 || !terminated)
                continue;
            x.own = joinLineRange(runLines, index, runCount, true);
            m_finalNewline = false;
        }
        run = joinLineRange(runLines, 0, index, true);
        x.entry.line = b.line;
        m_items.insert(k + 1, std::move(x));
        derive(k + 1);
        ++made;
    }
    if (made > 0)
        recompute();
    return made;
}

// ---------------------------------------------------------------------------------------------
// Versions and undo
// ---------------------------------------------------------------------------------------------

QList<BlockSource> BlockDocument::sourcesOf(int from, int to) const
{
    QList<BlockSource> out;
    out.reserve(std::max(0, to - from));
    for (int i = from; i < to; ++i) {
        const Item &it = m_items[i];
        out.append({it.entry.uid, it.own, it.eol, it.entry.trailing});
    }
    return out;
}

BlockEdges BlockDocument::edges() const
{
    return {m_bom, m_leading, m_eol, m_finalNewline};
}

void BlockDocument::setEdges(const BlockEdges &edges)
{
    m_bom = edges.bom;
    m_leading = edges.leading;
    m_eol = edges.eol;
    m_finalNewline = edges.finalNewline;
}

BlockDocument::Version BlockDocument::version() const
{
    auto data = std::make_shared<BlockVersionData>();
    data->blocks = sourcesOf(0, count());
    data->edges = edges();
    Version v;
    v.d = std::move(data);
    return v;
}

BlockDocument::Delta BlockDocument::diffFrom(const Version &older) const
{
    if (!older.d)
        return {};
    const QList<BlockSource> &old = older.d->blocks;
    // The blocks at both ends that no edit touched share their strings with the version: the
    // common prefix and suffix cost a pointer comparison per block.
    const auto same = [&](qsizetype o, qsizetype n) {
        const BlockSource &b = old[o];
        const Item &it = m_items[n];
        return b.uid == it.entry.uid && sameText(b.own, it.own) && sameText(b.eol, it.eol)
               && sameText(b.trailing, it.entry.trailing);
    };
    const qsizetype max = std::min(old.size(), m_items.size());
    qsizetype p = 0;
    while (p < max && same(p, p))
        ++p;
    qsizetype s = 0;
    while (s < max - p && same(old.size() - 1 - s, m_items.size() - 1 - s))
        ++s;
    const BlockEdges now = edges();
    if (p + s == old.size() && p + s == m_items.size() && now == older.d->edges)
        return {};
    Delta delta;
    delta.d = std::make_shared<const BlockDeltaData>(diffRuns(int(p), old.mid(p, old.size() - p - s),
                                                              sourcesOf(int(p), count() - int(s)), int(old.size()),
                                                              count(), older.d->edges, now));
    return delta;
}

EditResult BlockDocument::revert(const Delta &delta, bool *ok)
{
    return applyDelta(delta, 1, 0, ok);
}

EditResult BlockDocument::reapply(const Delta &delta, bool *ok)
{
    return applyDelta(delta, 0, 1, ok);
}

EditResult BlockDocument::applyDelta(const Delta &delta, int from, int to, bool *ok)
{
    if (ok)
        *ok = !delta.d; // an empty delta changes nothing
    if (!delta.d)
        return {};
    const BlockDeltaData &d = *delta.d;
    const int n = int(d.side[from].run.size());
    if (count() != d.side[from].count || !(edges() == d.side[from].edges) || d.first < 0 || d.first + n > count())
        return {};
    const std::optional<QList<BlockSource>> target = convertRun(d, from, to, sourcesOf(d.first, d.first + n));
    if (!target)
        return {};
    if (ok)
        *ok = true;

    const Snapshot before = snapshot();
    QHash<quint64, int> current;
    for (int k = d.first; k < d.first + n; ++k)
        current.insert(m_items[k].entry.uid, k);
    // The version's run: a block that is here keeps its parse unless its own text changes (its
    // version goes up when its text does: a Changed row); a block only that version has is new
    // here (an Inserted row) and is parsed.
    QList<Item> run;
    QList<bool> parse;
    run.reserve(target->size());
    parse.reserve(target->size());
    for (const BlockSource &b : *target) {
        const auto at = current.constFind(b.uid);
        Item it;
        bool ownChanged = true;
        if (at != current.constEnd()) {
            it = m_items[*at];
            ownChanged = !sameText(it.own, b.own);
            if (ownChanged || !sameText(it.eol, b.eol) || !sameText(it.entry.trailing, b.trailing))
                ++it.version;
        }
        it.entry.uid = b.uid;
        it.own = b.own;
        it.eol = b.eol;
        it.entry.trailing = b.trailing;
        run.append(std::move(it));
        parse.append(ownChanged);
    }
    m_items.remove(d.first, n);
    m_items.insert(d.first, run.size(), Item());
    for (qsizetype k = 0; k < run.size(); ++k)
        m_items[d.first + k] = std::move(run[k]);
    setEdges(d.side[to].edges);
    // In order: the parse options of a block depend on the kinds of the blocks before it
    // (recompute() then re-derives blocks whose options changed).
    for (qsizetype k = 0; k < parse.size(); ++k) {
        if (parse[k])
            derive(d.first + int(k));
    }
    recompute();
    EditResult r = rowChanges(before, -1, -1);
    // Only the edges changed (a BOM, the blank lines before the first block): the text did, which
    // a caller learns from changed rows.
    if (r.changes.isEmpty() && !(d.side[from].edges == d.side[to].edges) && count() > 0)
        r.changes.append({BlockChange::Type::Changed, 0, 1, -1});
    return r;
}

std::optional<BlockDocument::Delta> BlockDocument::composed(const Delta &first, const Delta &second, bool bounded) const
{
    if (!first.d)
        return second;
    if (!second.d)
        return first;
    const BlockDeltaData &a = *first.d;  // A -> B
    const BlockDeltaData &b = *second.d; // B -> C (this document)
    if (a.side[1].count != b.side[0].count || !(a.side[1].edges == b.side[0].edges) || count() != b.side[1].count
        || !(edges() == b.side[1].edges))
        return std::nullopt;
    // Both runs in B: one window of B over them (with the untouched blocks between them).
    const int end1 = a.first + int(a.side[1].run.size());
    const int end2 = b.first + int(b.side[0].run.size());
    if (std::max(a.first, b.first) - std::min(end1, end2) > MaxComposeGap)
        return std::nullopt;
    const int lo = std::min(a.first, b.first);
    const int hiB = std::max(end1, end2);
    const int hiC = hiB - int(b.side[0].run.size()) + int(b.side[1].run.size());
    if (lo < 0 || hiB > b.side[0].count || hiC > count() || (bounded && std::max(hiB, hiC) - lo > MaxComposeRun))
        return std::nullopt;
    // The window in C, back to B (the second run turned back), back to A (the first one).
    const QList<BlockSource> windowC = sourcesOf(lo, hiC);
    const qsizetype at2 = b.first - lo;
    const qsizetype size2 = b.side[1].run.size();
    const std::optional<QList<BlockSource>> run2 = convertRun(b, 1, 0, windowC.mid(at2, size2));
    if (!run2)
        return std::nullopt;
    const QList<BlockSource> windowB = windowC.first(at2) + *run2 + windowC.sliced(at2 + size2);
    const qsizetype at1 = a.first - lo;
    const qsizetype size1 = a.side[1].run.size();
    if (at1 + size1 > windowB.size())
        return std::nullopt;
    const std::optional<QList<BlockSource>> run1 = convertRun(a, 1, 0, windowB.mid(at1, size1));
    if (!run1)
        return std::nullopt;
    const QList<BlockSource> windowA = windowB.first(at1) + *run1 + windowB.sliced(at1 + size1);
    Delta out;
    out.d = std::make_shared<const BlockDeltaData>(
            diffRuns(lo, windowA, windowC, a.side[0].count, b.side[1].count, a.side[0].edges, b.side[1].edges));
    return out;
}

void BlockDocument::adoptUids(const QList<std::pair<int, quint64>> &uids)
{
    if (uids.isEmpty())
        return;
    for (const auto &[i, uid] : uids) {
        if (i >= 0 && i < count() && uid)
            m_items[i].entry.uid = uid;
    }
#ifndef QT_NO_DEBUG
    QSet<quint64> seen;
    for (const Item &it : std::as_const(m_items)) {
        Q_ASSERT_X(!seen.contains(it.entry.uid), "BlockDocument::adoptUids", "a uid used twice");
        seen.insert(it.entry.uid);
    }
#endif
    recompute(); // the parents' uids
}

// ---------------------------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------------------------

BlockDocument::Item BlockDocument::makeItem(const QString &own, const QString &eol, const QString &trailing)
{
    Item it;
    it.entry.uid = nextUid();
    it.own = own;
    it.eol = eol;
    it.entry.trailing = trailing;
    return it;
}

BlockDocument::PositionalOptions BlockDocument::positionalOptions(int i) const
{
    PositionalOptions options;
    options.frontmatter = i == 0 && m_leading.isEmpty();
    options.properties = true; // only frontmatter blocks before this one (checked backwards: O(1) in practice)
    for (int j = i - 1; j >= 0 && options.properties; --j)
        options.properties = m_items[j].entry.kind == BlockKind::Frontmatter;
    return options;
}

void BlockDocument::derive(int i)
{
    Item &it = m_items[i];
    BlockEntry &e = it.entry;
    // A block is parsed on its own; frontmatter and page properties are only possible at the
    // top of the document. Everything set here depends only on `own` and these two options
    // (takeDerived() copies it).
    const PositionalOptions positional = positionalOptions(i);
    StructureOptions options;
    options.frontmatter = positional.frontmatter;
    options.pageProperties = positional.properties;
    it.frontmatterAllowed = options.frontmatter;
    it.propertiesAllowed = options.pageProperties;
    const ParsedNote note = parseNoteWith(QString(), it.own, VaultConfig(), options);
    const Block *b = note.blocks.isEmpty() ? nullptr : &note.blocks.first();
    e.kind = b ? b->kind : BlockKind::Paragraph;
    e.headingLevel = b ? b->headingLevel : 0;
    e.lang = b ? b->lang : QString();
    e.taskStatus = b && b->task ? std::optional(b->task->status) : std::nullopt;
    e.blockId = b ? b->id : QString();
    it.displayMathOpen = it.own.contains(u"$$") && parseStructure(it.own, options).displayMathOpenAtEnd;

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

/// What derive() sets, from a block with the same text and options (keep the two in step).
void BlockDocument::takeDerived(Item &to, const Item &from)
{
    to.entry.kind = from.entry.kind;
    to.entry.headingLevel = from.entry.headingLevel;
    to.entry.lang = from.entry.lang;
    to.entry.taskStatus = from.entry.taskStatus;
    to.entry.blockId = from.entry.blockId;
    to.entry.prefix = from.entry.prefix;
    to.entry.body = from.entry.body;
    to.markerCol = from.markerCol;
    to.contIndent = from.contIndent;
    to.frontmatterAllowed = from.frontmatterAllowed;
    to.propertiesAllowed = from.propertiesAllowed;
    to.displayMathOpen = from.displayMathOpen;
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

/// A block whose `$$` line opens a display formula it does not close (a closing `$$` deleted, Enter
/// inside the formula, a `$$` typed): read again, the formula would go on over the lines after the
/// block up to a blank line, and a line that is only `$$` there would close it — every block in
/// between would be formula text (list items have no blank lines between them). A blank line after
/// the block keeps the blocks the editor shows (SYNTAX §11: a blank line ends such a formula).
void BlockDocument::separateDisplayMath()
{
    // A line that closes the formula: only `$$`, comments aside (as the parser reads it).
    const auto mayClose = [](const QString &line) {
        if (!line.contains(u"%%"))
            return QStringView(line).trimmed() == u"$$";
        return QStringView(maskRanges(line, docCommentRanges(line, text::splitLines(line)))).trimmed() == u"$$";
    };
    // A search that found nothing stopped at the block with a blank line: no block before it closes
    // a formula, so a block before it needs no search (linear on an outline of `- $$` items).
    int noCloseBefore = -1;
    for (int i = 0; i + 1 < count(); ++i) {
        if (!m_items[i].displayMathOpen || !m_items[i].entry.trailing.isEmpty() || i < noCloseBefore)
            continue;
        bool closes = false;
        int stop = count(); // where the search ended without a closing line
        for (int j = i + 1; j < count() && !closes; ++j) {
            bool blank = false;
            for (const text::Line &l : text::splitLines(m_items[j].own)) {
                blank = isBlank(l.text);
                closes = !blank && mayClose(l.text);
                if (blank || closes)
                    break;
            }
            if (!closes && (blank || !m_items[j].entry.trailing.isEmpty())) {
                stop = j;
                break;
            }
        }
        if (!closes) {
            noCloseBefore = stop;
            continue;
        }
        Item &it = m_items[i];
        if (it.eol.isEmpty())
            it.eol = m_eol;
        it.entry.trailing = m_eol;
        touch(i);
    }
}

EditResult BlockDocument::finish(const Snapshot &before, int focus, int cursor)
{
    protectLeadingRule();
    separateDisplayMath();
    recompute();
    return rowChanges(before, focus, cursor);
}

EditResult BlockDocument::rowChanges(const Snapshot &before, int focus, int cursor) const
{
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
        // A cursor inside or right after the task marker of a task with text counts as the start
        // (the editor hides the marker, so its caret 0 is after it): the text keeps its status.
        bool atStart = c == 0;
        int caret = 0;
        if (const auto m = matchTaskMarker(e.body, 0); m && c <= m->contentStart && m->contentStart < e.body.size()) {
            atStart = true;
            caret = int(m->contentStart);
        }
        if (atStart && !e.body.isEmpty()) {
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
            return finish(before, indexOfUid(e.uid), caret);
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

EditResult BlockDocument::convertToParagraph(int i)
{
    if (i < 0 || i >= count() || !isListItem(i) || at(i).parent >= 0)
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(i).uid;
    // The children move one level out: to the indentation of the item itself.
    const int end = subtreeEnd(i);
    if (end > i + 1)
        shiftIndent(i + 1, end, leadingWhitespace(i + 1), leadingWhitespace(i));
    QString body = at(i).body;
    if (const auto marker = matchTaskMarker(body, 0))
        body = body.sliced(marker->contentStart);
    // The item's own lines without the marker and the continuation indentation (the body holds
    // them that way), with the block's line terminator.
    const Item &it = m_items[i];
    QString eol = firstEol(it.own);
    if (eol.isEmpty())
        eol = it.eol.isEmpty() ? m_eol : it.eol;
    setOwn(i, normalizeNewlines(body).split(u'\n').join(eol));
    touch(i);
    recompute();
    renumberChildren(-1);
    const int ni = indexOfUid(uid);
    ensureSeparated(ni - 1);
    ensureSeparated(ni);
    fixEnds();
    return finish(before, indexOfUid(uid), 0);
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

// ---------------------------------------------------------------------------------------------
// Ranges
// ---------------------------------------------------------------------------------------------

QList<int> BlockDocument::rangeTops(int first, int last) const
{
    QList<int> tops;
    if (first < 0 || last >= count() || first > last)
        return tops;
    for (int i = first; i <= last; ++i) {
        if (at(i).kind != BlockKind::Frontmatter && at(i).parent < first)
            tops.append(i);
    }
    return tops;
}

EditResult BlockDocument::removeRange(int first, int last)
{
    if (first < 0 || last >= count() || first > last)
        return {};
    const Snapshot before = snapshot();
    // The rows to go: [first, end) — the range with the subtrees of its blocks.
    int end = first;
    QList<quint64> parents; // lists that lose items (renumbered afterwards); 0: the top level
    for (int i = first; i <= last; ++i) {
        end = std::max(end, subtreeEnd(i));
        const int p = at(i).parent;
        const quint64 parentUid = p >= 0 ? at(p).uid : 0;
        if (p < first && !parents.contains(parentUid))
            parents.append(parentUid);
    }
    const bool wasLast = end == count();
    const bool noFinalBreak = wasLast && m_items[end - 1].eol.isEmpty();
    removeItems(first, end);
    if (wasLast && first > 0 && noFinalBreak && m_items[first - 1].entry.trailing.isEmpty())
        m_items[first - 1].eol.clear();
    recompute();
    for (const quint64 uid : std::as_const(parents)) {
        const int p = uid ? indexOfUid(uid) : -1;
        if (!uid || p >= 0)
            renumberChildren(p);
    }
    ensureSeparated(first - 1);
    fixEnds();
    const int focus = first > 0 ? first - 1 : (first < count() ? first : -1);
    return finish(before, focus, focus >= 0 && isTextBlock(focus) ? int(at(focus).body.size()) : -1);
}

EditResult BlockDocument::indentRange(int first, int last)
{
    QList<quint64> tops;
    for (const int i : rangeTops(first, last))
        tops.append(at(i).uid);
    if (tops.isEmpty())
        return {};
    const Snapshot before = snapshot();
    const QList<Item> savedItems = m_items;
    const QString savedLeading = m_leading;
    for (const quint64 uid : std::as_const(tops)) {
        const int i = indexOfUid(uid);
        const int depth = at(i).depth;
        indent(i);
        const int ni = indexOfUid(uid);
        if (ni < 0 || !isListItem(ni) || at(ni).depth != depth + 1) {
            // All or nothing: one block that cannot go deeper keeps the whole range as it was.
            m_items = savedItems;
            m_leading = savedLeading;
            return {};
        }
    }
    return finish(before, indexOfUid(tops.first()), -1);
}

EditResult BlockDocument::outdentRange(int first, int last)
{
    QList<quint64> tops;
    for (const int i : rangeTops(first, last))
        tops.append(at(i).uid);
    if (tops.isEmpty())
        return {};
    const Snapshot before = snapshot();
    bool any = false;
    for (qsizetype k = tops.size() - 1; k >= 0; --k)
        any = outdent(indexOfUid(tops[k])).changed() || any;
    if (!any)
        return {};
    return finish(before, indexOfUid(tops.first()), -1);
}

EditResult BlockDocument::moveRangeUp(int first, int last)
{
    const QList<int> tops = rangeTops(first, last);
    if (tops.isEmpty())
        return {};
    for (qsizetype k = 1; k < tops.size(); ++k) {
        if (nextSibling(tops[k - 1]) != tops[k])
            return {};
    }
    const int a = tops.first();
    const int end = subtreeEnd(tops.last());
    const int s = prevSibling(a);
    if (s < 0 || s < pinnedCount())
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(a).uid;
    swapRanges(s, a, end);
    recompute();
    renumberAround(uid);
    ensureSeparated(s - 1);
    ensureSeparated(s + (end - a) - 1);
    ensureSeparated(end - 1);
    fixEnds();
    return finish(before, indexOfUid(uid), -1);
}

EditResult BlockDocument::moveRangeDown(int first, int last)
{
    const QList<int> tops = rangeTops(first, last);
    if (tops.isEmpty())
        return {};
    for (qsizetype k = 1; k < tops.size(); ++k) {
        if (nextSibling(tops[k - 1]) != tops[k])
            return {};
    }
    const int a = tops.first();
    const int t = nextSibling(tops.last());
    if (t < 0 || a < pinnedCount())
        return {};
    const Snapshot before = snapshot();
    const quint64 uid = at(a).uid;
    const int end = subtreeEnd(t);
    swapRanges(a, t, end);
    recompute();
    renumberAround(uid);
    ensureSeparated(a - 1);
    ensureSeparated(a + (end - t) - 1);
    ensureSeparated(end - 1);
    fixEnds();
    return finish(before, indexOfUid(uid), -1);
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
