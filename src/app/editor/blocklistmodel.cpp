#include "blocklistmodel.h"

#include "blockrender_p.h"
#include "editorhost.h"

#include "calc/calc.h"
#include "markdown/inline.h"
#include "util/paths.h"

#include <QUrl>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

const QList<int> StructuralRoles{BlockListModel::DepthRole,      BlockListModel::HasChildrenRole,
                                 BlockListModel::CollapsedRole,  BlockListModel::VisibleRole,
                                 BlockListModel::IsEditingRole,  BlockListModel::IsSelectedRole,
                                 BlockListModel::IsZoomRootRole, BlockListModel::PrevKindRole};

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
    m_selectedUid = 0;
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
    m_state = state;
    m_doc = std::move(fresh);
    m_text = content;
    m_textValid = true;
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
    m_selectedUid = remap(m_selectedUid);

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
    markdown::EditResult r = op(m_doc);
    if (!r.changed())
        return r;
    m_textValid = false;
    rebuildIndex();
    QList<quint64> finalUids;
    finalUids.reserve(m_doc.count());
    for (int i = 0; i < m_doc.count(); ++i)
        finalUids.append(m_doc.at(i).uid);
    replay(r.changes, finalUids);
    if (m_rows != finalUids) {
        // Should never happen (the changes describe the edit exactly); stay consistent anyway.
        qWarning("BlockListModel: row changes did not replay, resetting");
        beginResetModel();
        m_rows = finalUids;
        endResetModel();
    }
    if (oldCount != m_rows.size())
        Q_EMIT countChanged();
    refresh(true);
    return r;
}

void BlockListModel::replay(const QList<markdown::BlockChange> &changes, const QList<quint64> &finalUids)
{
    using Type = markdown::BlockChange::Type;
    for (const markdown::BlockChange &c : changes) {
        if (c.count <= 0)
            continue;
        switch (c.type) {
        case Type::Removed: {
            if (c.first < 0 || c.first + c.count > m_rows.size())
                return;
            beginRemoveRows({}, c.first, c.first + c.count - 1);
            for (int k = 0; k < c.count; ++k)
                m_state.remove(m_rows[c.first + k]);
            m_rows.remove(c.first, c.count);
            endRemoveRows();
            break;
        }
        case Type::Inserted: {
            if (c.first < 0 || c.first > m_rows.size() || c.first + c.count > finalUids.size())
                return;
            beginInsertRows({}, c.first, c.first + c.count - 1);
            for (int k = 0; k < c.count; ++k) {
                m_rows.insert(c.first + k, finalUids[c.first + k]);
                m_state.remove(finalUids[c.first + k]);
            }
            endInsertRows();
            break;
        }
        case Type::Moved: {
            if (c.first < 0 || c.first + c.count > m_rows.size() || c.destination < 0
                || (c.destination >= c.first && c.destination <= c.first + c.count))
                return;
            if (!beginMoveRows({}, c.first, c.first + c.count - 1, {}, c.destination))
                return;
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
                return;
            for (int k = 0; k < c.count; ++k)
                invalidateContent(m_rows[c.first + k]);
            Q_EMIT dataChanged(index(c.first), index(c.first + c.count - 1));
            break;
        }
        }
    }
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

    QList<int> structural;
    QList<int> calcRows;
    QList<int> tableRows;
    QList<int> htmlRows;
    QList<int> transclusionRows;
    QString prevKind; // of the last visible row
    int visibleCount = 0;
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
        RowState next = st;
        next.hasChildren = m_doc.hasChildren(i);
        next.collapsed = m_foldingEnabled && next.hasChildren && m_collapsed.contains(e.uid);
        next.editing = m_editingUid && e.uid == m_editingUid;
        next.selected = m_selectedUid && e.uid == m_selectedUid;
        bool visible = zoomRow < 0 || (i >= zoomRange.first && i <= zoomRange.last());
        if (visible && m_foldingEnabled) {
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
        if (noteModeOnly && !next.editing && !next.selected)
            visible = false;
        next.visible = visible;
        next.displayDepth = std::max(0, e.depth - zoomDepth);
        next.zoomRoot = row == zoomRow;
        next.prevKind = prevKind;
        if (visible) {
            prevKind = kindName(e.kind);
            ++visibleCount;
        }
        if (next.hasChildren != st.hasChildren || next.collapsed != st.collapsed || next.visible != st.visible
            || next.displayDepth != st.displayDepth || next.editing != st.editing || next.selected != st.selected
            || next.zoomRoot != st.zoomRoot || next.prevKind != st.prevKind) {
            st.hasChildren = next.hasChildren;
            st.collapsed = next.collapsed;
            st.visible = next.visible;
            st.displayDepth = next.displayDepth;
            st.editing = next.editing;
            st.selected = next.selected;
            st.zoomRoot = next.zoomRoot;
            st.prevKind = next.prevKind;
            structural.append(row);
        }
        if (!contentChanged)
            continue;
        const BlockShape &shape = cachedShape(row);
        if (shape.view == BlockView::Calc) {
            QVariantList results = computeCalcResults(i);
            if (results != st.calcResults) {
                st.calcResults = results;
                calcRows.append(row);
            }
        } else if (shape.view == BlockView::Table) {
            QVariantMap table = computeTableData(i);
            if (table != st.tableData) {
                st.tableData = table;
                tableRows.append(row);
            }
        }
        if (st.dependsOnPage && st.htmlValid) {
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
    emitRowsChanged(structural, StructuralRoles);
    if (visibleCount != m_visibleCount) {
        m_visibleCount = visibleCount;
        Q_EMIT visibleCountChanged();
    }
    emitRowsChanged(calcRows, {CalcResultsRole});
    emitRowsChanged(tableRows, {TableDataRole});
    emitRowsChanged(htmlRows, {HtmlRole});
    emitRowsChanged(transclusionRows, {TransclusionsRole});
}

QVariantList BlockListModel::computeCalcResults(int i) const
{
    const markdown::BlockEntry &e = m_doc.at(i);
    const BlockShape s = shapeOf(e);
    const calc::CalcLocale locale = m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    calc::Scope scope = calc::pageScope(text(), e.line, locale);
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
    if (!model)
        return {};
    const calc::CalcLocale locale = m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    const calc::TableEval eval = calc::evaluateTable(*model, locale, calc::pageScope(text(), e.line, locale));
    // Cells are inline Markdown (bold, links, tags, chips): rendered like paragraphs, cached by
    // their text until the rendering changes (theme, index, language, today).
    RowState &st = stateFor(e.uid);
    std::optional<markdown::InlineRenderer> renderer;
    const auto cellHtml = [&](const QString &cell) -> QString {
        if (cell.trimmed().isEmpty())
            return {};
        const auto cached = st.cellHtml.constFind(cell);
        if (cached != st.cellHtml.constEnd())
            return *cached;
        if (!renderer) {
            markdown::RenderContext ctx = renderContext(i);
            ctx.hideTransclusions = false; // no embed frames in cells: placeholder links
            renderer.emplace(ctx);
        }
        const QString html = renderer->renderBlock(cell, BlockKind::Paragraph);
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
    const int line = m_doc.at(docIndex).line;
    const calc::CalcLocale locale = m_english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    auto scope = std::make_shared<std::optional<calc::Scope>>();
    const QString content = text();
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
    st.dependsOnPage = e.body.contains(u"`=") || e.body.contains(u"` =") || e.body.contains(u"((");
    return html;
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
    if (view != BlockView::Text && view != BlockView::Heading && view != BlockView::Quote && view != BlockView::Callout)
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
        QUrl url;
        if (m_host) {
            QString path = m_host->resolveLink(em.target, m_path);
            if (path.isEmpty()) {
                const QString decoded = QUrl::fromPercentEncoding(em.target.toUtf8());
                path = decoded.contains(u'/') ? paths::normalize(decoded) : paths::join(attachments, decoded);
            }
            url = m_host->assetUrl(path);
        }
        st.embeds.append(QVariantMap{{u"target"_s, em.target},
                                     {u"alt"_s, em.alt},
                                     {u"image"_s, em.image},
                                     {u"url"_s, url},
                                     {u"name"_s, paths::basename(em.target)}});
    }
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

void BlockListModel::setSelectedUid(quint64 uid)
{
    if (m_selectedUid == uid)
        return;
    m_selectedUid = uid;
    refresh(false);
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
