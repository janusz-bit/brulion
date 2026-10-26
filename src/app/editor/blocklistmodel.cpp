#include "blocklistmodel.h"

#include "blockrender_p.h"
#include "editorhost.h"

#include "calc/calc.h"
#include "markdown/inline.h"
#include "markdown/math.h"
#include "util/paths.h"

#include "picturesize.h"
#include "webpicture/webpicture.h"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

const QList<int> StructuralRoles{
        BlockListModel::DepthRole,      BlockListModel::HasChildrenRole, BlockListModel::CollapsedRole,
        BlockListModel::VisibleRole,    BlockListModel::IsEditingRole,   BlockListModel::IsSelectedRole,
        BlockListModel::IsZoomRootRole, BlockListModel::PrevKindRole,    BlockListModel::SelectionEdgeRole};

/// Where a selected row sits in the band of selected visible rows (selectionEdge role).
enum SelectionEdge : quint8 { NoEdge, SingleEdge, FirstEdge, MiddleEdge, LastEdge };

QString selectionEdgeName(quint8 edge)
{
    switch (edge) {
    case SingleEdge: return u"single"_s;
    case FirstEdge: return u"first"_s;
    case MiddleEdge: return u"middle"_s;
    case LastEdge: return u"last"_s;
    default: return {};
    }
}

QString alignName(calc::TableAlign a)
{
    switch (a) {
    case calc::TableAlign::Left: return u"left"_s;
    case calc::TableAlign::Center: return u"center"_s;
    case calc::TableAlign::Right: return u"right"_s;
    case calc::TableAlign::None: break;
    }
    return {};
}

/// The text may hold an inline calculation (`` `= expr` ``): its rendering depends on the calc
/// variables above the block. Like InlineRenderer, which trims the code span's content (line
/// breaks read as spaces), any whitespace may stand between the backtick and the `=`: a code
/// span whose text starts on the next line of the block (`` `⏎= cena`` ``) is one too. In a
/// quote or callout that next line starts with its `>` prefix, which the renderer strips, so
/// after a line break `>` is skipped as well.
bool hasInlineCalc(const QString &text)
{
    for (qsizetype i = text.indexOf(u'`'); i >= 0; i = text.indexOf(u'`', i + 1)) {
        qsizetype j = i + 1;
        bool lineBreak = false;
        while (j < text.size() && (text[j].isSpace() || (lineBreak && text[j] == u'>'))) {
            lineBreak = lineBreak || text[j] == u'\n';
            ++j;
        }
        if (j < text.size() && text[j] == u'=')
            return true;
    }
    return false;
}

/// The text may hold a block reference (`((id))`, `[[…#^id]]`): its rendering shows the
/// referenced block's text, which may be on this page and change with any edit of it.
bool hasBlockRef(const QString &text)
{
    return text.contains(u"((") || text.contains(u"#^");
}

/// Longest common subsequence of two lists of strings as matched index pairs, with common
/// prefix/suffix trimmed first; a middle part too large for the quadratic table is treated as
/// completely replaced.
QList<std::pair<int, int>> matchBlocks(const QStringList &a, const QStringList &b)
{
    QList<std::pair<int, int>> pairs;
    int start = 0;
    while (start < a.size() && start < b.size() && a[start] == b[start]) {
        pairs.append({start, start});
        ++start;
    }
    int endA = int(a.size());
    int endB = int(b.size());
    QList<std::pair<int, int>> suffix;
    while (endA > start && endB > start && a[endA - 1] == b[endB - 1]) {
        --endA;
        --endB;
        suffix.prepend({endA, endB});
    }
    const int n = endA - start;
    const int m = endB - start;
    if (n > 0 && m > 0 && qint64(n) * m <= 4'000'000) {
        // dp[i][j] = LCS of a[start+i..endA) and b[start+j..endB).
        std::vector<int> dp(size_t(n + 1) * size_t(m + 1), 0);
        const auto at = [&](int i, int j) -> int & { return dp[size_t(i) * size_t(m + 1) + size_t(j)]; };
        for (int i = n - 1; i >= 0; --i) {
            for (int j = m - 1; j >= 0; --j)
                at(i, j) = a[start + i] == b[start + j] ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
        }
        int i = 0;
        int j = 0;
        while (i < n && j < m) {
            if (a[start + i] == b[start + j]) {
                pairs.append({start + i, start + j});
                ++i;
                ++j;
            } else if (at(i + 1, j) >= at(i, j + 1)) {
                ++i;
            } else {
                ++j;
            }
        }
    }
    pairs.append(suffix);
    return pairs;
}

/// Whether text of a block can never change what calculations see, wherever the block sits
/// outside calc fences and the frontmatter: no fence characters (``` / ~~~), no `key:: value`
/// property and no line that could open or close frontmatter (--- / ... after whitespace). A
/// superset of the lines calc::PageCalc reacts to there (fences, property lines, delimiters).
bool calcInert(QStringView s)
{
    if (s.contains(u"```") || s.contains(u"~~~") || s.contains(u"::"))
        return false;
    for (qsizetype start = 0; start <= s.size();) {
        qsizetype end = start;
        while (end < s.size() && s[end] != u'\n' && s[end] != u'\r')
            ++end;
        const QStringView line = s.sliced(start, end - start).trimmed();
        if (line.startsWith(u"---") || line.startsWith(u"..."))
            return false;
        start = end + 1;
    }
    return true;
}

} // namespace

namespace detail {

QString renderBodyHtml(const markdown::BlockEntry &e, const BlockShape &s, const markdown::InlineRenderer &renderer,
                       int *renderOffset)
{
    QString html;
    int offset = 0;
    switch (s.view) {
    case BlockView::Text:
        // renderBlock(): only a list item's leading "[ ]" / "TODO" is a task marker (SYNTAX §5); a
        // paragraph starting with one shows it as text.
        html = onlyEmbeds(e.body, true)         ? QString()
               : e.kind == BlockKind::Paragraph ? renderer.renderBlock(e.body, BlockKind::Paragraph)
                                                : renderer.render(e.body);
        break;
    case BlockView::Heading:
        if (e.kind == BlockKind::Heading) {
            html = renderer.renderBlock(e.body, BlockKind::Heading);
        } else {
            // "# Title" inside a list item: the heading line, then any further lines.
            const QString rest = e.body.mid(s.editOffset);
            const qsizetype nl = rest.indexOf(u'\n');
            offset = s.editOffset;
            html = renderer.renderBlock(nl < 0 ? rest : rest.left(nl), BlockKind::Heading);
            if (nl >= 0) // the lines after the heading are text (a "TODO" there is no task marker)
                html += u"<br/>"_s + renderer.renderBlock(QStringView(rest).sliced(nl + 1), BlockKind::Paragraph);
        }
        break;
    case BlockView::Quote: html = renderer.renderBlock(e.body, BlockKind::Quote); break;
    case BlockView::Callout: html = renderer.renderBlock(e.body, BlockKind::Callout); break;
    case BlockView::Html: html = markdown::InlineRenderer::escape(e.body); break;
    case BlockView::Math:
        // ```math / ```latex / ```tex: the LaTeX source stays in the file, the delegate shows
        // the formula (docs/SYNTAX.md §11, markdown/math.h).
        html = markdown::renderMath(s.editText, true);
        break;
    default: break;
    }
    if (renderOffset)
        *renderOffset = offset;
    return html;
}

QString richTextToPlain(const QString &html)
{
    QString out;
    out.reserve(html.size());
    bool inTag = false;
    for (qsizetype i = 0; i < html.size(); ++i) {
        const QChar c = html[i];
        if (inTag) {
            inTag = c != u'>';
            continue;
        }
        if (c == u'<') {
            inTag = true;
            if (QStringView(html).sliced(i).startsWith(u"<br"))
                out += u' ';
            continue;
        }
        if (c == u'&') {
            const qsizetype semi = html.indexOf(u';', i);
            if (semi > i && semi - i <= 6) {
                const QStringView entity = QStringView(html).sliced(i + 1, semi - i - 1);
                QChar decoded;
                if (entity == u"nbsp")
                    decoded = QChar(0x00a0);
                else if (entity == u"amp")
                    decoded = u'&';
                else if (entity == u"lt")
                    decoded = u'<';
                else if (entity == u"gt")
                    decoded = u'>';
                else if (entity == u"quot")
                    decoded = u'"';
                else if (entity == u"#39")
                    decoded = u'\'';
                if (!decoded.isNull()) {
                    out += decoded;
                    i = semi;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

} // namespace detail

BlockListModel::BlockListModel(QObject *parent) : QAbstractListModel(parent), m_today(QDate::currentDate()) {}

BlockListModel::~BlockListModel() = default;

int BlockListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> BlockListModel::roleNames() const
{
    return {
            {RawRole, "raw"},
            {BodyRole, "body"},
            {EditTextRole, "editText"},
            {EditOffsetRole, "editOffset"},
            {HtmlRole, "html"},
            {DepthRole, "depth"},
            {DocDepthRole, "docDepth"},
            {KindRole, "kind"},
            {ViewRole, "view"},
            {HeadingLevelRole, "headingLevel"},
            {LangRole, "lang"},
            {HasChildrenRole, "hasChildren"},
            {CollapsedRole, "collapsed"},
            {VisibleRole, "visible"},
            {TaskStatusRole, "taskStatus"},
            {BlockIdRole, "blockId"},
            {IsEditingRole, "isEditing"},
            {IsSelectedRole, "isSelected"},
            {IsZoomRootRole, "isZoomRoot"},
            {CalcResultsRole, "calcResults"},
            {TableDataRole, "tableData"},
            {EmbedsRole, "embeds"},
            {OnlyEmbedsRole, "onlyEmbeds"},
            {PropertiesRole, "properties"},
            {QueryRole, "query"},
            {CalloutTypeRole, "calloutType"},
            {CalloutFoldedRole, "calloutFolded"},
            {LineRole, "line"},
            {UidRole, "uid"},
            {PlainTextRole, "plainText"},
            {TransclusionsRole, "transclusions"},
            {ListMarkerRole, "listMarker"},
            {PrevKindRole, "prevKind"},
            {SelectionEdgeRole, "selectionEdge"},
            {NoteModeOnlyRole, "noteModeOnly"},
    };
}

QString BlockListModel::text() const
{
    if (!m_textValid) {
        m_text = m_doc.toString();
        m_textValid = true;
    }
    return m_text;
}

int BlockListModel::docIndexOfRow(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return -1;
    return m_docIndex.value(m_rows[row], -1);
}

BlockListModel::RowState &BlockListModel::stateFor(quint64 uid) const
{
    return m_state[uid];
}

const BlockListModel::RowState *BlockListModel::stateOf(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return nullptr;
    return &stateFor(m_rows[row]);
}

int BlockListModel::rowOfUid(quint64 uid) const
{
    // Outside replays rows and document indices coincide.
    return m_docIndex.value(uid, -1);
}

quint64 BlockListModel::uidAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows[row] : 0;
}

const BlockShape &BlockListModel::cachedShape(int row) const
{
    static const BlockShape empty;
    const int i = docIndexOfRow(row);
    if (i < 0)
        return empty;
    RowState &st = stateFor(m_rows[row]);
    if (!st.shapeValid) {
        const markdown::BlockEntry &e = m_doc.at(i);
        st.shape = shapeOf(e);
        st.noteModeOnly = st.shape.view == BlockView::Frontmatter && onlyNoteModeKey(e.body);
        st.shapeValid = true;
    }
    return st.shape;
}

BlockShape BlockListModel::shapeAt(int row) const
{
    return cachedShape(row);
}

QString BlockListModel::editTextAt(int row) const
{
    return cachedShape(row).editText;
}

int BlockListModel::renderOffsetAt(int row) const
{
    if (docIndexOfRow(row) < 0)
        return 0;
    (void)data(index(row), HtmlRole); // renders when needed
    return stateFor(m_rows[row]).renderOffset;
}

void BlockListModel::rebuildIndex()
{
    m_docIndex.clear();
    m_docIndex.reserve(m_doc.count());
    for (int i = 0; i < m_doc.count(); ++i)
        m_docIndex.insert(m_doc.at(i).uid, i);
}

void BlockListModel::resetContent(const QString &content)
{
    beginResetModel();
    m_doc.setContent(content);
    m_text = content;
    m_textValid = true;
    m_state.clear();
    m_rows.clear();
    for (int i = 0; i < m_doc.count(); ++i)
        m_rows.append(m_doc.at(i).uid);
    rebuildIndex();
    m_zoomUid = 0;
    m_collapsed.clear();
    m_editingUid = 0;
    m_selectedUids.clear();
    calcContentChanged();
    refresh(true);
    endResetModel();
    Q_EMIT countChanged();
}

QHash<quint64, quint64> BlockListModel::replaceContent(const QString &content)
{
    QStringList oldSources;
    QList<quint64> oldUids = m_rows;
    oldSources.reserve(m_doc.count());
    for (int i = 0; i < m_doc.count(); ++i)
        oldSources.append(m_doc.at(i).prefix + m_doc.at(i).body + QString::number(m_doc.at(i).depth));

    markdown::BlockDocument fresh(content);
    QStringList newSources;
    newSources.reserve(fresh.count());
    for (int i = 0; i < fresh.count(); ++i)
        newSources.append(fresh.at(i).prefix + fresh.at(i).body + QString::number(fresh.at(i).depth));
    const QList<std::pair<int, int>> pairs = matchBlocks(oldSources, newSources);

    QHash<quint64, quint64> mapping;
    QSet<int> keptOld;
    QSet<int> keptNew;
    for (const auto &[o, n] : pairs) {
        keptOld.insert(o);
        keptNew.insert(n);
        mapping.insert(oldUids[o], fresh.at(n).uid);
    }

    // Remove rows of old blocks that are gone (from the end, so indices stay valid).
    const int oldCount = int(m_rows.size());
    for (int r = oldCount - 1; r >= 0;) {
        if (keptOld.contains(r)) {
            --r;
            continue;
        }
        int first = r;
        while (first - 1 >= 0 && !keptOld.contains(first - 1))
            --first;
        beginRemoveRows({}, first, r);
        for (int k = first; k <= r; ++k)
            m_state.remove(m_rows[k]);
        m_rows.remove(first, r - first + 1);
        endRemoveRows();
        r = first - 1;
    }

    // Kept rows take the new uids (same blocks, same sources) with their cached state.
    QHash<quint64, RowState> state;
    for (int k = 0; k < m_rows.size(); ++k) {
        const quint64 newUid = mapping.value(m_rows[k]);
        auto it = m_state.find(m_rows[k]);
        if (it != m_state.end())
            state.insert(newUid, *it);
        m_rows[k] = newUid;
    }
    for (RowState &st : state)
        st.calcTextValid = false; // the kept blocks' own text may differ in indentation
    m_state = state;
    m_doc = std::move(fresh);
    m_text = content;
    m_textValid = true;
    calcContentChanged();
    rebuildIndex();

    // View state follows the kept blocks.
    const auto remap = [&](quint64 uid) { return mapping.value(uid, 0); };
    m_zoomUid = remap(m_zoomUid);
    QSet<quint64> collapsed;
    for (const quint64 uid : std::as_const(m_collapsed)) {
        if (const quint64 n = remap(uid))
            collapsed.insert(n);
    }
    m_collapsed = collapsed;
    m_editingUid = remap(m_editingUid);
    QSet<quint64> selected;
    for (const quint64 uid : std::as_const(m_selectedUids)) {
        if (const quint64 n = remap(uid))
            selected.insert(n);
    }
    m_selectedUids = selected;

    // Insert rows for new blocks.
    const int newCount = m_doc.count();
    for (int r = 0; r < newCount;) {
        if (keptNew.contains(r)) {
            ++r;
            continue;
        }
        int last = r;
        while (last + 1 < newCount && !keptNew.contains(last + 1))
            ++last;
        beginInsertRows({}, r, last);
        for (int k = r; k <= last; ++k)
            m_rows.insert(k, m_doc.at(k).uid);
        endInsertRows();
        r = last + 1;
    }
    Q_ASSERT(m_rows.size() == m_doc.count());
    if (oldCount != m_rows.size())
        Q_EMIT countChanged();
    refresh(true);
    return mapping;
}

markdown::EditResult BlockListModel::apply(const std::function<markdown::EditResult(markdown::BlockDocument &)> &op)
{
    const int oldCount = int(m_rows.size());
    // A page with calculations: classify its blocks before the edit (once — calc-neutral edits
    // keep the flags current), so that replay() can tell whether this edit is calc-neutral.
    if (m_hasCalcConsumers && !m_calcFlagsValid)
        analyseCalc();
    markdown::EditResult r = op(m_doc);
    if (!r.changed())
        return r;
    m_textValid = false;
    m_pageCalc.reset();
    rebuildIndex();
    QList<quint64> finalUids;
    finalUids.reserve(m_doc.count());
    for (int i = 0; i < m_doc.count(); ++i)
        finalUids.append(m_doc.at(i).uid);
    bool calcNeutral = replay(r.changes, finalUids);
    if (m_rows != finalUids) {
        // Should never happen (the changes describe the edit exactly); stay consistent anyway.
        qWarning("BlockListModel: row changes did not replay, resetting");
        beginResetModel();
        m_rows = finalUids;
        endResetModel();
        calcNeutral = false;
    }
    if (!calcNeutral)
        calcContentChanged();
    if (oldCount != m_rows.size())
        Q_EMIT countChanged();
    refresh(true);
    return r;
}

bool BlockListModel::replay(const QList<markdown::BlockChange> &changes, const QList<quint64> &finalUids)
{
    using Type = markdown::BlockChange::Type;
    // Calc-neutral: every block removed, inserted or changed defines nothing and lies outside
    // calc fences and the frontmatter, before and after the edit (by the flags of the text
    // before it, kept current here); the first row and moves are never neutral (they can change
    // what the first line is or the order of definitions and calculations). Line numbers may
    // shift: every calculation keeps the definitions above it.
    bool neutral = m_calcFlagsValid;
    const auto insensitive = [this](quint64 uid) {
        const auto f = m_calcFlags.constFind(uid);
        return f != m_calcFlags.constEnd() && !f->sensitive;
    };
    const auto inertNow = [this](quint64 uid) {
        const int i = m_docIndex.value(uid, -1);
        return i >= 0 && calcInert(m_doc.blockSource(i)) && calcInert(m_doc.at(i).trailing);
    };
    for (const markdown::BlockChange &c : changes) {
        if (c.count <= 0)
            continue;
        switch (c.type) {
        case Type::Removed: {
            if (c.first < 0 || c.first + c.count > m_rows.size())
                return false;
            neutral = neutral && c.first > 0;
            beginRemoveRows({}, c.first, c.first + c.count - 1);
            for (int k = 0; k < c.count; ++k) {
                const quint64 uid = m_rows[c.first + k];
                neutral = neutral && insensitive(uid);
                m_calcFlags.remove(uid);
                m_state.remove(uid);
            }
            m_rows.remove(c.first, c.count);
            endRemoveRows();
            break;
        }
        case Type::Inserted: {
            if (c.first < 0 || c.first > m_rows.size() || c.first + c.count > finalUids.size())
                return false;
            if (neutral) {
                // Not inside a calc fence or the frontmatter: none continues after the block before.
                const auto before = c.first > 0 ? m_calcFlags.constFind(m_rows[c.first - 1]) : m_calcFlags.constEnd();
                neutral = before != m_calcFlags.constEnd() && !before->openAfter;
            }
            beginInsertRows({}, c.first, c.first + c.count - 1);
            for (int k = 0; k < c.count; ++k) {
                const quint64 uid = finalUids[c.first + k];
                m_rows.insert(c.first + k, uid);
                m_state.remove(uid);
                neutral = neutral && inertNow(uid);
                if (neutral)
                    m_calcFlags.insert(uid, CalcFlags{false, false});
            }
            endInsertRows();
            break;
        }
        case Type::Moved: {
            neutral = false;
            if (c.first < 0 || c.first + c.count > m_rows.size() || c.destination < 0
                || (c.destination >= c.first && c.destination <= c.first + c.count))
                return false;
            if (!beginMoveRows({}, c.first, c.first + c.count - 1, {}, c.destination))
                return false;
            const QList<quint64> part = m_rows.mid(c.first, c.count);
            m_rows.remove(c.first, c.count);
            const int dest = c.destination > c.first ? c.destination - c.count : c.destination;
            for (int k = 0; k < part.size(); ++k)
                m_rows.insert(dest + k, part[k]);
            endMoveRows();
            break;
        }
        case Type::Changed: {
            if (c.first < 0 || c.first + c.count > m_rows.size())
                return false;
            neutral = neutral && c.first > 0;
            for (int k = 0; k < c.count; ++k) {
                const quint64 uid = m_rows[c.first + k];
                neutral = neutral && insensitive(uid) && inertNow(uid);
                invalidateContent(uid);
            }
            Q_EMIT dataChanged(index(c.first), index(c.first + c.count - 1));
            break;
        }
        }
    }
    return neutral;
}

void BlockListModel::invalidateContent(quint64 uid)
{
    auto it = m_state.find(uid);
    if (it == m_state.end())
        return;
    it->shapeValid = false;
    it->htmlValid = false;
    it->embedsValid = false;
    it->transclusionsValid = false;
    it->resultsValid = false;
    it->calcTextValid = false;
}

void BlockListModel::emitRowsChanged(const QList<int> &rows, const QList<int> &roles)
{
    if (rows.isEmpty())
        return;
    int start = rows.first();
    int prev = start;
    for (qsizetype k = 1; k <= rows.size(); ++k) {
        if (k < rows.size() && rows[k] == prev + 1) {
            prev = rows[k];
            continue;
        }
        Q_EMIT dataChanged(index(start), index(prev), roles);
        if (k < rows.size()) {
            start = rows[k];
            prev = start;
        }
    }
}

void BlockListModel::refresh(bool contentChanged)
{
    const int n = int(m_rows.size());
    const int zoomRow = m_zoomUid ? rowOfUid(m_zoomUid) : -1;
    if (m_zoomUid && zoomRow < 0)
        m_zoomUid = 0;
    markdown::RowRange zoomRange{0, n};
    int zoomDepth = 0;
    if (zoomRow >= 0) {
        zoomRange = m_doc.subtreeRange(zoomRow);
        zoomDepth = m_doc.at(zoomRow).depth;
    }
    // Forget folds of blocks that no longer exist.
    for (auto it = m_collapsed.begin(); it != m_collapsed.end();) {
        if (!m_docIndex.contains(*it))
            it = m_collapsed.erase(it);
        else
            ++it;
    }

    QList<char> dirty(n, 0); // structural state changed
    QList<int> visibleRows;  // in order (selection edges)
    QList<int> calcRows;
    QList<int> tableRows;
    QList<int> htmlRows;
    QList<int> transclusionRows;
    QList<int> lineRows; // blocks moved to other lines of the file by changes above them
    QString prevKind; // of the last visible row
    int visibleCount = 0;
    bool calcConsumers = false;
    for (int row = 0; row < n; ++row) {
        const int i = docIndexOfRow(row);
        if (i < 0)
            continue;
        const markdown::BlockEntry &e = m_doc.at(i);
        // The shape first: it may create the row's state (a reference taken before would dangle).
        bool noteModeOnly = false;
        if (e.kind == BlockKind::Frontmatter) {
            (void)cachedShape(row);
            noteModeOnly = stateFor(e.uid).noteModeOnly;
        }
        RowState &st = stateFor(e.uid);
        const bool hasChildren = m_doc.hasChildren(i);
        const bool collapsed = m_foldingEnabled && hasChildren && m_collapsed.contains(e.uid);
        const bool editing = m_editingUid && e.uid == m_editingUid;
        const bool selected = !m_selectedUids.isEmpty() && m_selectedUids.contains(e.uid);
        bool visible = zoomRow < 0 || (i >= zoomRange.first && i <= zoomRange.last());
        if (visible && m_foldingEnabled && !m_collapsed.isEmpty()) {
            for (int p = e.parent; p >= 0; p = m_doc.at(p).parent) {
                if (p == zoomRow)
                    break;
                if (m_collapsed.contains(m_doc.at(p).uid)) {
                    visible = false;
                    break;
                }
            }
        }
        // Frontmatter with nothing but the note mode: shown only while it is edited or selected.
        if (noteModeOnly && !editing && !selected)
            visible = false;
        const int displayDepth = std::max(0, e.depth - zoomDepth);
        const bool zoomRoot = row == zoomRow;
        if (hasChildren != st.hasChildren || collapsed != st.collapsed || visible != st.visible
            || displayDepth != st.displayDepth || editing != st.editing || selected != st.selected
            || zoomRoot != st.zoomRoot || prevKind != st.prevKind) {
            st.hasChildren = hasChildren;
            st.collapsed = collapsed;
            st.visible = visible;
            st.displayDepth = displayDepth;
            st.editing = editing;
            st.selected = selected;
            st.zoomRoot = zoomRoot;
            st.prevKind = prevKind;
            dirty[row] = 1;
        }
        // A fresh state belongs to a row a view fetched in full (inserted, reset): nothing to tell.
        if (e.line != st.line) {
            if (st.line >= 0)
                lineRows.append(row);
            st.line = e.line;
        }
        if (visible) {
            prevKind = kindName(e.kind);
            ++visibleCount;
            visibleRows.append(row);
        } else if (st.selectionEdge != NoEdge) {
            st.selectionEdge = NoEdge;
            dirty[row] = 1;
        }
        if (!contentChanged)
            continue;
        // Results stay while the block and, when they read calc variables, everything its
        // calculations see are unchanged.
        const BlockShape &shape = cachedShape(row);
        if (shape.view == BlockView::Calc || shape.view == BlockView::Table) {
            if (!st.resultsValid || (st.resultsUseScope && st.resultsEpoch != m_calcEpoch)
                || (shape.view == BlockView::Table && st.resultsUseRefs)) {
                st.resultsValid = true;
                st.resultsEpoch = m_calcEpoch;
                if (shape.view == BlockView::Calc) {
                    QVariantList results = computeCalcResults(i);
                    st.resultsUseScope = true;
                    if (results != st.calcResults) {
                        st.calcResults = results;
                        calcRows.append(row);
                    }
                } else {
                    QVariantMap table = computeTableData(i); // sets resultsUseScope / resultsUseRefs
                    if (table != st.tableData) {
                        st.tableData = table;
                        tableRows.append(row);
                    }
                }
            }
            calcConsumers = calcConsumers || st.resultsUseScope;
        }
        if (st.htmlValid && st.dependsOnCalc)
            calcConsumers = true;
        if (st.htmlValid && (st.dependsOnRefs || (st.dependsOnCalc && st.htmlCalcEpoch != m_calcEpoch))) {
            const QString before = st.html;
            st.htmlValid = false;
            if (renderHtml(i) != before)
                htmlRows.append(row);
        }
        // Embeds of this page show its current text.
        if (st.transclusionsValid && st.transclusionsLocal) {
            const QVariantList before = st.transclusions;
            st.transclusionsValid = false;
            ensureTransclusions(row);
            if (st.transclusions != before)
                transclusionRows.append(row);
        }
    }
    if (contentChanged)
        m_hasCalcConsumers = calcConsumers;
    // The selection band's ends over the visible rows (every row has its state by now: the
    // pointers stay valid, nothing is inserted into m_state below).
    QList<RowState *> shown;
    shown.reserve(visibleRows.size());
    for (const int row : std::as_const(visibleRows))
        shown.append(&stateFor(m_rows[row]));
    for (qsizetype k = 0; k < shown.size(); ++k) {
        RowState &st = *shown[k];
        quint8 edge = NoEdge;
        if (st.selected) {
            const bool before = k > 0 && shown[k - 1]->selected;
            const bool after = k + 1 < shown.size() && shown[k + 1]->selected;
            edge = before && after ? MiddleEdge : before ? LastEdge : after ? FirstEdge : SingleEdge;
        }
        if (edge != st.selectionEdge) {
            st.selectionEdge = edge;
            dirty[visibleRows[k]] = 1;
        }
    }
    QList<int> structural;
    for (int row = 0; row < n; ++row) {
        if (dirty[row])
            structural.append(row);
    }
    emitRowsChanged(structural, StructuralRoles);
    if (visibleCount != m_visibleCount) {
        m_visibleCount = visibleCount;
        Q_EMIT visibleCountChanged();
    }
    emitRowsChanged(calcRows, {CalcResultsRole});
    emitRowsChanged(tableRows, {TableDataRole});
    emitRowsChanged(htmlRows, {HtmlRole});
    emitRowsChanged(transclusionRows, {TransclusionsRole});
    emitRowsChanged(lineRows, {LineRole});
}

QVariantList BlockListModel::computeCalcResults(int i) const
{
    const markdown::BlockEntry &e = m_doc.at(i);
    const BlockShape s = shapeOf(e);
    const calc::CalcLocale locale = calcLocale();
    calc::Scope scope = *scopeFor(i);
    const QList<calc::CalcLineResult> results = calc::evaluateCalc(s.editText, scope, locale);
    QVariantList out;
    out.reserve(results.size());
    for (const calc::CalcLineResult &r : results) {
        out.append(QVariantMap{{u"line"_s, r.line},
                               {u"value"_s, r.value},
                               {u"error"_s, r.error},
                               {u"assigned"_s, r.assigned},
                               {u"label"_s, r.label}});
    }
    return out;
}

QVariantMap BlockListModel::computeTableData(int i) const
{
    const markdown::BlockEntry &e = m_doc.at(i);
    const auto model = calc::parseTable(e.body);
    if (!model) {
        stateFor(e.uid).resultsUseScope = false;
        stateFor(e.uid).resultsUseRefs = false;
        return {};
    }
    // Only formula cells read the variables above the table: no page scope for a table without.
    const bool formulas = std::any_of(model->rows.cbegin(), model->rows.cend(), [](const QStringList &row) {
        return std::any_of(row.cbegin(), row.cend(),
                           [](const QString &cell) { return cell.trimmed().startsWith(u'='); });
    });
    const calc::TableEval eval = formulas ? calc::evaluateTable(*model, calcLocale(), *scopeFor(i))
                                          : calc::evaluateTable(*model, calcLocale());
    // Cells are inline Markdown (bold, links, tags, chips): rendered like paragraphs, cached by
    // their text until the rendering changes (theme, index, language, today). A cell with an
    // inline calculation also depends on the variables above the table: never cached, and the
    // table is recomputed when they may have changed (a new calc epoch). A cell with a block
    // reference shows that block's text: never cached, the table follows every page change.
    RowState &st = stateFor(e.uid);
    st.resultsUseScope = formulas || hasInlineCalc(e.body);
    st.resultsUseRefs = hasBlockRef(e.body);
    std::optional<markdown::InlineRenderer> renderer;
    const auto cellHtml = [&](const QString &cell) -> QString {
        if (cell.trimmed().isEmpty())
            return {};
        const bool live = hasInlineCalc(cell) || hasBlockRef(cell);
        const auto cached = live ? st.cellHtml.constEnd() : st.cellHtml.constFind(cell);
        if (cached != st.cellHtml.constEnd())
            return *cached;
        if (!renderer) {
            markdown::RenderContext ctx = renderContext(i);
            ctx.hideTransclusions = false; // no embed frames in cells: placeholder links
            ctx.dateLinks = false;         // a chip's range would be the cell's, not the block's
            renderer.emplace(ctx);
        }
        const QString html = renderer->renderBlock(cell, BlockKind::Paragraph);
        if (!live)
            st.cellHtml.insert(cell, html);
        return html;
    };
    QVariantList aligns;
    for (const calc::TableAlign a : model->aligns)
        aligns.append(alignName(a));
    QVariantList headerHtml;
    for (const QString &h : model->header)
        headerHtml.append(cellHtml(h));
    QVariantList rows;
    for (qsizetype r = 0; r < model->rows.size(); ++r) {
        QVariantList cells;
        for (qsizetype c = 0; c < model->header.size(); ++c) {
            const QString cell = c < model->rows[r].size() ? model->rows[r][c] : QString();
            const bool formula = cell.trimmed().startsWith(u'=');
            QString display = cell;
            QString error;
            if (formula && r < eval.values.size() && c < eval.values[r].size())
                display = eval.values[r][c];
            if (r < eval.errors.size() && c < eval.errors[r].size())
                error = eval.errors[r][c];
            // Formula results (and errors) are values, not Markdown.
            const QString html = formula || !error.isEmpty() ? markdown::InlineRenderer::escape(display) : cellHtml(cell);
            cells.append(QVariantMap{{u"text"_s, cell},
                                     {u"display"_s, display},
                                     {u"html"_s, html},
                                     {u"plain"_s, formula ? display : detail::richTextToPlain(html)},
                                     {u"error"_s, error},
                                     {u"formula"_s, formula}});
        }
        rows.append(QVariant(cells));
    }
    return {{u"header"_s, QVariant(QStringList(model->header))},
            {u"headerHtml"_s, headerHtml},
            {u"aligns"_s, aligns},
            {u"rows"_s, rows}};
}

const VaultConfig &BlockListModel::vaultConfig() const
{
    if (!m_config)
        m_config = vaultConfigFromMap(m_host ? m_host->vaultConfig() : QVariantMap());
    return *m_config;
}

markdown::RenderContext BlockListModel::renderContext(int docIndex) const
{
    markdown::RenderContext ctx;
    ctx.today = m_today;
    ctx.locale = m_english ? dates::UiLocale::English : dates::UiLocale::Polish;
    ctx.palette = m_palette;
    ctx.linkTitle = markdown::journalLinkTitle(vaultConfig(), ctx.locale, m_today); // [[2026-10-13]]: "wt 13 paź"
    ctx.taskCheckbox = false;
    ctx.hideTransclusions = true; // the delegate shows them in frames (`transclusions`)
    ctx.dateLinks = true;         // date chips have a menu (PageController::dateChip)
    const QPointer<EditorHost> host = m_host;
    const QString path = m_path;
    if (host) {
        ctx.resolvePage = [host, path](const QString &target) -> std::optional<QString> {
            if (!host)
                return target;
            const QString resolved = host->resolveLink(target, path);
            if (resolved.isEmpty())
                return std::nullopt;
            return resolved;
        };
    }
    ctx.blockText = [this, host](const QString &id) -> std::optional<QString> {
        for (int k = 0; k < m_doc.count(); ++k) {
            if (m_doc.at(k).blockId == id)
                return plainTextOf(m_doc.at(k), 120);
        }
        if (host) {
            const QString t = host->blockText(id);
            if (!t.isEmpty())
                return t;
        }
        return std::nullopt;
    };
    // [[Page#^id]]: the block of that page — this one's live text, another's from the index.
    ctx.linkedBlockText = [this, host, path](const QString &target, const QString &id) -> std::optional<QString> {
        const QString resolved = target.isEmpty() || !host ? QString() : host->resolveLink(target, path);
        QString t;
        if (target.isEmpty() || (!path.isEmpty() && resolved == path)) {
            for (int k = 0; k < m_doc.count(); ++k) {
                if (m_doc.at(k).blockId == id)
                    return plainTextOf(m_doc.at(k), 120);
            }
            t = host ? host->blockText(id) : QString(); // moved to another page
        } else if (!resolved.isEmpty()) {
            t = host->linkedBlockText(resolved, id);
        }
        if (t.isEmpty())
            return std::nullopt;
        return t;
    };
    // Inline calculations see the variables above the block (the context is used right away).
    const calc::CalcLocale locale = calcLocale();
    auto scope = std::make_shared<std::shared_ptr<const calc::Scope>>();
    ctx.inlineCalc = [this, scope, docIndex, locale](const QString &expr) -> QString {
        if (!*scope)
            *scope = scopeFor(docIndex);
        const calc::CalcLineResult r = calc::evaluateInline(expr, **scope, locale);
        if (r.hasValue())
            return r.value;
        return r.hasError() ? u"#ERR"_s : QString();
    };
    return ctx;
}

markdown::RenderContext BlockListModel::renderContextFor(const QString &path, const QString &content, int line) const
{
    markdown::RenderContext ctx;
    ctx.today = m_today;
    ctx.locale = m_english ? dates::UiLocale::English : dates::UiLocale::Polish;
    ctx.palette = m_palette;
    ctx.linkTitle = markdown::journalLinkTitle(vaultConfig(), ctx.locale, m_today);
    ctx.taskCheckbox = false;
    ctx.hideTransclusions = true; // nested embeds get their own rows in a transclusion
    const QPointer<EditorHost> host = m_host;
    if (host) {
        ctx.resolvePage = [host, path](const QString &target) -> std::optional<QString> {
            if (!host)
                return target;
            const QString resolved = host->resolveLink(target, path);
            if (resolved.isEmpty())
                return std::nullopt;
            return resolved;
        };
        ctx.blockText = [host](const QString &id) -> std::optional<QString> {
            const QString t = host ? host->blockText(id) : QString();
            if (t.isEmpty())
                return std::nullopt;
            return t.simplified();
        };
        ctx.linkedBlockText = [host, path](const QString &target, const QString &id) -> std::optional<QString> {
            const QString resolved = !host ? QString() : target.isEmpty() ? path : host->resolveLink(target, path);
            const QString t = resolved.isEmpty() ? QString() : host->linkedBlockText(resolved, id);
            if (t.isEmpty())
                return std::nullopt;
            return t.simplified();
        };
    }
    const calc::CalcLocale locale = m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    auto scope = std::make_shared<std::optional<calc::Scope>>();
    ctx.inlineCalc = [scope, content, line, locale](const QString &expr) -> QString {
        if (!scope->has_value())
            *scope = calc::pageScope(content, line, locale);
        const calc::CalcLineResult r = calc::evaluateInline(expr, **scope, locale);
        if (r.hasValue())
            return r.value;
        return r.hasError() ? u"#ERR"_s : QString();
    };
    return ctx;
}

QString BlockListModel::renderHtml(int i) const
{
    const markdown::BlockEntry &e = m_doc.at(i);
    RowState &st = stateFor(e.uid);
    if (st.htmlValid)
        return st.html;
    const BlockShape s = shapeOf(e);
    const markdown::InlineRenderer renderer(renderContext(i));
    int offset = 0;
    const QString html = detail::renderBodyHtml(e, s, renderer, &offset);
    st.html = html;
    st.htmlValid = true;
    st.renderOffset = offset;
    // ((id)) and [[…#^id]] show the text of their block, which may be on this page.
    st.dependsOnRefs = hasBlockRef(e.body);
    st.dependsOnCalc = hasInlineCalc(e.body);
    st.htmlCalcEpoch = m_calcEpoch;
    if (st.dependsOnCalc)
        m_hasCalcConsumers = true;
    return html;
}

// --- Calculations ------------------------------------------------------------------------------

calc::CalcLocale BlockListModel::calcLocale() const
{
    return m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
}

std::shared_ptr<const calc::Scope> BlockListModel::scopeFor(int i) const
{
    // calc::pageScope(text(), line, locale) without comparing the page text per call: one
    // PageCalc per text revision, and the scope kept per block while the epoch lasts.
    const markdown::BlockEntry &e = m_doc.at(i);
    const auto it = m_state.find(e.uid); // never inserts: callers hold references into m_state
    if (it != m_state.end() && it->scope && it->scopeEpoch == m_calcEpoch)
        return it->scope;
    if (!m_pageCalc)
        m_pageCalc = std::make_shared<const calc::PageCalc>(text(), calcLocale());
    auto scope = std::make_shared<const calc::Scope>(m_pageCalc->scopeAt(e.line));
    if (it != m_state.end()) {
        it->scope = scope;
        it->scopeEpoch = m_calcEpoch;
    }
    return scope;
}

void BlockListModel::calcContentChanged()
{
    ++m_calcEpoch;
    m_pageCalc.reset();
    m_calcFlags.clear();
    m_calcFlagsValid = false;
}

void BlockListModel::analyseCalc()
{
    m_calcFlags.clear();
    m_calcFlags.reserve(m_doc.count());
    m_calcFlagsValid = true;
    const QString content = text();
    const QStringView all(content);
    const int count = m_doc.count();
    // What each block's own text can do (kept until the block changes).
    bool anyFences = false;
    for (int i = 0; i < count; ++i) {
        RowState &st = stateFor(m_doc.at(i).uid);
        if (!st.calcTextValid) {
            const QString own = m_doc.blockSource(i);
            st.calcInert = calcInert(own) && calcInert(m_doc.at(i).trailing);
            st.calcFences = own.contains(u"```") || own.contains(u"~~~");
            st.calcTextValid = true;
        }
        anyFences = anyFences || st.calcFences;
    }
    // Lines as text::splitLines() and the BlockDocument count them ("\n", "\r\n", a lone "\r").
    const auto lineEnd = [&](qsizetype from) {
        while (from < all.size() && all[from] != u'\n' && all[from] != u'\r')
            ++from;
        return from;
    };
    const auto nextLine = [&](qsizetype end) {
        return end + (end + 1 < all.size() && all[end] == u'\r' && all[end + 1] == u'\n' ? 2 : 1);
    };
    // Regions whose every line matters to calc::PageCalc, in order, inclusive line ranges: the
    // frontmatter as PageCalc reads it ("---" on the first line up to the first "---" / "..."
    // line) and the calc fences of the body after it; a fence left open runs to the end (End).
    constexpr qsizetype End = std::numeric_limits<qsizetype>::max();
    struct Region {
        qsizetype first;
        qsizetype last;
    };
    QList<Region> regions;
    qsizetype bodyStart = 0;
    if (all.first(lineEnd(0)).trimmed() == u"---") {
        qsizetype n = 1;
        for (qsizetype at = nextLine(lineEnd(0)); at <= all.size(); at = nextLine(lineEnd(at)), ++n) {
            const QStringView l = all.sliced(at, lineEnd(at) - at);
            if ((l.startsWith(u"---") || l.startsWith(u"...")) && l.sliced(3).trimmed().isEmpty()) {
                regions.append({0, n});
                bodyStart = n + 1;
                break;
            }
        }
    }
    // The calc fences (calc::findCalcBlocks, the fences PageCalc reads) depend only on the lines
    // with fence characters: no other line opens or closes one. Those lines of the body, in
    // order, with their line numbers (line 0 from the text itself: it may start with a BOM).
    if (anyFences) {
        QString fenceText;
        QList<qsizetype> fenceLines;
        for (int i = 0; i < count; ++i) {
            if (!stateFor(m_doc.at(i).uid).calcFences)
                continue;
            const QString own = m_doc.blockSource(i);
            qsizetype n = m_doc.at(i).line;
            for (qsizetype at = 0; at <= own.size(); ++n) {
                qsizetype end = at;
                while (end < own.size() && own[end] != u'\n' && own[end] != u'\r')
                    ++end;
                const QStringView l = n == 0 ? all.first(lineEnd(0)) : QStringView(own).sliced(at, end - at);
                if (n >= bodyStart && (l.contains(u"```") || l.contains(u"~~~"))) {
                    if (!fenceLines.isEmpty())
                        fenceText += u'\n';
                    fenceText += l;
                    fenceLines.append(n);
                }
                at = end + (end + 1 < own.size() && own[end] == u'\r' && own[end + 1] == u'\n' ? 2 : 1);
            }
        }
        for (const calc::CalcBlock &b : calc::findCalcBlocks(fenceText))
            regions.append({fenceLines.at(b.startLine), b.closed ? fenceLines.at(b.endLine) : End});
    }
    // Every block (its own lines and the blank lines after it) against the regions.
    qsizetype r = 0;
    for (int i = 0; i < count; ++i) {
        const markdown::BlockEntry &e = m_doc.at(i);
        const qsizetype first = e.line;
        const qsizetype last = i + 1 < count ? qsizetype(m_doc.at(i + 1).line) - 1 : End - 1;
        while (r < regions.size() && regions[r].last < first)
            ++r;
        qsizetype k = r; // the last region starting within the block
        while (k + 1 < regions.size() && regions[k + 1].first <= last)
            ++k;
        CalcFlags f;
        const bool inRegion = r < regions.size() && regions[r].first <= last;
        f.sensitive = inRegion || !stateFor(e.uid).calcInert;
        f.openAfter = k < regions.size() && regions[k].first <= last && regions[k].last > last;
        m_calcFlags.insert(e.uid, f);
    }
}

void BlockListModel::ensureEmbeds(int row) const
{
    const int i = docIndexOfRow(row);
    if (i < 0)
        return;
    RowState &st = stateFor(m_rows[row]);
    if (st.embedsValid)
        return;
    st.embedsValid = true;
    st.embeds.clear();
    st.onlyEmbeds = false;
    const markdown::BlockEntry &e = m_doc.at(i);
    const BlockView view = cachedShape(row).view;
    if (!showsEmbeds(view))
        return;
    st.onlyEmbeds = view == BlockView::Text && (e.body.contains(u"![") || e.body.contains(u"!((")) && onlyEmbeds(e.body, true);
    const QList<Embed> embeds = embedsOf(e.body);
    if (embeds.isEmpty())
        return;
    QString attachments = u"assets"_s;
    if (m_host) {
        const QString folder = m_host->vaultConfig().value(u"attachmentsFolder"_s).toString();
        if (!folder.isEmpty())
            attachments = folder;
    }
    for (const Embed &em : embeds) {
        if (em.web) {
            st.embeds.append(webEmbed(em));
            continue;
        }
        QUrl url;
        // A picture has its size from the start (the file's header, picturesize.h): one that took
        // its size only when it had loaded made its block, and every block below it, move while
        // the page scrolled.
        QSize size;
        // A picture whose format this device has no decoder for (WebP, AVIF without Qt's image
        // plugins) is named as such instead of failing to load (EmbedView).
        bool decodable = em.image;
        if (m_host) {
            QString path = m_host->resolveLink(em.target, m_path);
            if (path.isEmpty()) {
                const QString decoded = QUrl::fromPercentEncoding(em.target.toUtf8());
                path = decoded.contains(u'/') ? paths::normalize(decoded) : paths::join(attachments, decoded);
            }
            url = m_host->assetUrl(path);
            if (em.image) {
                decodable = m_host->canDecodePicture(path);
                if (decodable)
                    size = m_host->assetPixelSize(path);
            }
        }
        if (decodable && !size.isValid() && url.isLocalFile())
            size = localPictureSize(url.toLocalFile());
        if (!size.isValid())
            size = QSize(0, 0);
        st.embeds.append(QVariantMap{{u"target"_s, em.target},
                                     {u"alt"_s, em.alt},
                                     {u"image"_s, em.image},
                                     {u"decodable"_s, decodable},
                                     {u"format"_s, em.image ? pictureFormatName(em.target) : QString()},
                                     {u"url"_s, url},
                                     {u"name"_s, paths::basename(em.target)},
                                     {u"width"_s, size.width()},
                                     {u"height"_s, size.height()}});
    }
}

QVariantMap BlockListModel::webEmbed(const Embed &em) const
{
    // A picture from the internet: what the host has of it (EditorHost::webPicture) — the UI
    // shows a cached file only, never the URL itself.
    const QVariantMap picture = m_host ? m_host->webPicture(em.target) : QVariantMap{{u"state"_s, u"ask"_s}};
    const QString state = picture.value(u"state"_s, u"ask"_s).toString();
    const QUrl url = state == u"ready" ? picture.value(u"url"_s).toUrl() : QUrl();
    const QUrl web = webpicture::webUrl(em.target);
    QSize size(picture.value(u"width"_s).toInt(), picture.value(u"height"_s).toInt());
    // The format of the cached picture: the host's (a data: URL in the browser has no file name;
    // the web URL's ending says nothing — an SVG badge without ".svg"), else the file's suffix.
    QString format = picture.value(u"format"_s).toString();
    if (format.isEmpty() && url.isLocalFile())
        format = QFileInfo(url.toLocalFile()).suffix();
    const QString formatFile = u"picture."_s + format;
    const bool decodable = url.isEmpty() || (!format.isEmpty() && canDecodePictureFormat(formatFile));
    if (!url.isEmpty() && decodable && size.isEmpty() && url.isLocalFile())
        size = localPictureSize(url.toLocalFile());
    if (!size.isValid() || size.isEmpty())
        size = QSize(0, 0);
    return QVariantMap{{u"target"_s, em.target},
                       {u"alt"_s, em.alt},
                       {u"image"_s, true},
                       {u"web"_s, true},
                       {u"state"_s, state},
                       {u"error"_s, picture.value(u"error"_s).toString()},
                       {u"host"_s, web.host()},
                       {u"decodable"_s, decodable},
                       {u"format"_s, url.isEmpty() ? QString() : pictureFormatName(formatFile)},
                       {u"url"_s, url},
                       {u"name"_s, webpicture::displayName(web)},
                       {u"width"_s, size.width()},
                       {u"height"_s, size.height()}};
}

void BlockListModel::webPictureChanged(const QString &url)
{
    QList<int> rows;
    for (int row = 0; row < m_rows.size(); ++row) {
        const int i = docIndexOfRow(row);
        if (i < 0 || !m_doc.at(i).body.contains(u"!["))
            continue;
        RowState &st = stateFor(m_rows[row]);
        if (!st.embedsValid)
            continue; // computed when asked for, with the new state
        // By the pictures the row shows (their targets as parsed: `a\_b.png` names `a_b.png`).
        const bool names = std::any_of(st.embeds.cbegin(), st.embeds.cend(), [&url](const QVariant &v) {
            const QVariantMap em = v.toMap();
            return em.value(u"web"_s).toBool() && (url.isEmpty() || em.value(u"target"_s).toString() == url);
        });
        if (!names)
            continue;
        st.embedsValid = false;
        rows.append(row);
    }
    emitRowsChanged(rows, {EmbedsRole});
}

QVariant BlockListModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid())
        return {};
    const int row = idx.row();
    const int i = docIndexOfRow(row);
    if (i < 0)
        return {};
    const markdown::BlockEntry &e = m_doc.at(i);
    const RowState &st = stateFor(e.uid);
    switch (role) {
    case Qt::DisplayRole:
    case PlainTextRole: return plainTextOf(e);
    case RawRole: return m_doc.blockSource(i);
    case BodyRole: return e.body;
    case EditTextRole: return cachedShape(row).editText;
    case EditOffsetRole: return cachedShape(row).editOffset;
    case HtmlRole: return renderHtml(i);
    case DepthRole: return st.displayDepth;
    case DocDepthRole: return e.depth;
    case KindRole: return kindName(e.kind);
    case ViewRole: return viewName(cachedShape(row).view);
    case HeadingLevelRole: return cachedShape(row).headingLevel;
    case LangRole: return cachedShape(row).lang;
    case HasChildrenRole: return st.hasChildren;
    case CollapsedRole: return st.collapsed;
    case VisibleRole: return st.visible;
    case TaskStatusRole: return taskStatusName(cachedShape(row).task);
    case BlockIdRole: return e.blockId;
    case IsEditingRole: return st.editing;
    case IsSelectedRole: return st.selected;
    case IsZoomRootRole: return st.zoomRoot;
    case CalcResultsRole: return st.calcResults;
    case TableDataRole: return st.tableData;
    case EmbedsRole: ensureEmbeds(row); return stateFor(e.uid).embeds;
    case OnlyEmbedsRole: ensureEmbeds(row); return stateFor(e.uid).onlyEmbeds;
    case PropertiesRole:
        return cachedShape(row).view == BlockView::Frontmatter ? frontmatterProperties(e.body) : QVariantList();
    case QueryRole:
        return cachedShape(row).view == BlockView::Query ? queryInfo(cachedShape(row).editText) : QVariantMap();
    case CalloutTypeRole: return cachedShape(row).calloutType;
    case CalloutFoldedRole: return cachedShape(row).calloutFolded;
    case LineRole: return e.line;
    case UidRole: return QString::number(e.uid);
    case TransclusionsRole: ensureTransclusions(row); return stateFor(e.uid).transclusions;
    case ListMarkerRole: return e.kind == BlockKind::ListItem ? e.prefix.trimmed() : QString();
    case PrevKindRole: return st.prevKind;
    case NoteModeOnlyRole: (void)cachedShape(row); return stateFor(e.uid).noteModeOnly;
    case SelectionEdgeRole: return selectionEdgeName(st.selectionEdge);
    default: return {};
    }
}

QVariantMap BlockListModel::get(int row) const
{
    if (docIndexOfRow(row) < 0)
        return {};
    const QModelIndex idx = index(row);
    return {{u"view"_s, data(idx, ViewRole)},
            {u"kind"_s, data(idx, KindRole)},
            {u"taskStatus"_s, data(idx, TaskStatusRole)},
            {u"depth"_s, data(idx, DepthRole)},
            {u"hasChildren"_s, data(idx, HasChildrenRole)},
            {u"collapsed"_s, data(idx, CollapsedRole)},
            {u"visible"_s, data(idx, VisibleRole)},
            {u"blockId"_s, data(idx, BlockIdRole)},
            {u"headingLevel"_s, data(idx, HeadingLevelRole)},
            {u"plainText"_s, data(idx, PlainTextRole)},
            {u"listMarker"_s, data(idx, ListMarkerRole)}};
}

// --- View state ------------------------------------------------------------------------------

void BlockListModel::setZoomUid(quint64 uid)
{
    if (m_zoomUid == uid)
        return;
    m_zoomUid = uid;
    refresh(false);
}

bool BlockListModel::isCollapsed(int row) const
{
    const RowState *st = stateOf(row);
    return st && st->collapsed;
}

void BlockListModel::setCollapsed(int row, bool collapsed)
{
    const quint64 uid = uidAt(row);
    if (!uid)
        return;
    if (collapsed)
        m_collapsed.insert(uid);
    else
        m_collapsed.remove(uid);
    refresh(false);
}

void BlockListModel::setCollapsedUids(const QSet<quint64> &uids)
{
    m_collapsed = uids;
    refresh(false);
}

void BlockListModel::setFoldingEnabled(bool enabled)
{
    if (m_foldingEnabled == enabled)
        return;
    m_foldingEnabled = enabled;
    refresh(false);
}

void BlockListModel::setEditingUid(quint64 uid)
{
    if (m_editingUid == uid)
        return;
    m_editingUid = uid;
    refresh(false);
}

void BlockListModel::setSelectedUids(const QSet<quint64> &uids)
{
    if (m_selectedUids == uids)
        return;
    m_selectedUids = uids;
    refresh(false);
}

bool BlockListModel::isRowSelected(int row) const
{
    const RowState *st = stateOf(row);
    return st && st->selected;
}

bool BlockListModel::isRowVisible(int row) const
{
    const RowState *st = stateOf(row);
    return st && st->visible;
}

int BlockListModel::nextVisibleRow(int row) const
{
    for (int r = row + 1; r < m_rows.size(); ++r) {
        if (isRowVisible(r))
            return r;
    }
    return -1;
}

int BlockListModel::previousVisibleRow(int row) const
{
    for (int r = std::min(row, int(m_rows.size())) - 1; r >= 0; --r) {
        if (isRowVisible(r))
            return r;
    }
    return -1;
}

int BlockListModel::firstVisibleRow() const
{
    return nextVisibleRow(-1);
}

int BlockListModel::lastVisibleRow() const
{
    return previousVisibleRow(int(m_rows.size()));
}

// --- Rendering context -------------------------------------------------------------------------

void BlockListModel::setHost(EditorHost *host)
{
    m_host = host;
    invalidateRendering();
}

void BlockListModel::setPalette(const markdown::Palette &palette)
{
    m_palette = palette;
    invalidateRendering();
}

void BlockListModel::setEnglish(bool english)
{
    if (m_english == english)
        return;
    m_english = english;
    calcContentChanged(); // numbers and value words are read in the page's language
    for (auto it = m_state.begin(); it != m_state.end(); ++it) {
        it->calcResults.clear();
        it->tableData.clear();
    }
    invalidateRendering();
    refresh(true);
}

void BlockListModel::setToday(QDate today)
{
    if (m_today == today)
        return;
    m_today = today;
    invalidateRendering();
}

void BlockListModel::invalidateRendering()
{
    m_config.reset(); // the journal file format may have changed (links to days)
    for (auto it = m_state.begin(); it != m_state.end(); ++it) {
        it->htmlValid = false;
        it->embedsValid = false;
        it->transclusionsValid = false;
        it->cellHtml.clear();
    }
    // Table cells are rendered too (links resolve differently, other colours).
    QList<int> tableRows;
    for (int row = 0; row < m_rows.size(); ++row) {
        const int i = docIndexOfRow(row);
        if (i < 0 || cachedShape(row).view != BlockView::Table)
            continue;
        RowState &st = stateFor(m_rows[row]);
        QVariantMap table = computeTableData(i);
        st.resultsValid = true;
        st.resultsEpoch = m_calcEpoch;
        if (table != st.tableData) {
            st.tableData = table;
            tableRows.append(row);
        }
    }
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1),
                           {HtmlRole, EmbedsRole, OnlyEmbedsRole, TransclusionsRole});
    emitRowsChanged(tableRows, {TableDataRole});
}

} // namespace brulion::editor
