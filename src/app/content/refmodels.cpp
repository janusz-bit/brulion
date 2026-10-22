#include "refmodels.h"

#include "contentutil.h"
#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "text/text.h"
#include "util/paths.h"
#include "workspace.h"

#include <QScopeGuard>
#include <QSet>

#include <algorithm>

namespace brulion::content {

using namespace Qt::StringLiterals;

namespace {

QString statusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

/// Escaped rich text of `text` with the characters at `positions` on a highlight background.
QString highlightHtml(const QString &text, const QList<int> &positions, const QString &background)
{
    if (positions.isEmpty())
        return markdown::InlineRenderer::escape(text);
    QString out;
    qsizetype i = 0;
    qsizetype p = 0;
    while (i < text.size()) {
        if (p < positions.size() && positions.at(p) == i) {
            qsizetype end = i;
            while (p < positions.size() && positions.at(p) == end) {
                ++end;
                ++p;
            }
            out += u"<span style=\"background-color:"_s + background + u"\">"_s
                   + markdown::InlineRenderer::escape(QStringView(text).mid(i, end - i)) + u"</span>"_s;
            i = end;
            continue;
        }
        const qsizetype next = p < positions.size() ? positions.at(p) : text.size();
        out += markdown::InlineRenderer::escape(QStringView(text).mid(i, next - i));
        i = next;
    }
    return out;
}

/// Journals newest first, then other pages by title (Polish collation), then path.
void sortNotes(QList<QString> &list, const VaultIndex *idx)
{
    const auto journal = [idx](const QString &p) -> QDate {
        const ParsedNote *n = idx->note(p);
        return n && n->journalDate ? *n->journalDate : QDate();
    };
    std::stable_sort(list.begin(), list.end(), [&](const QString &a, const QString &b) {
        const QDate da = journal(a);
        const QDate db = journal(b);
        if (da.isValid() != db.isValid())
            return da.isValid();
        if (da.isValid() && da != db)
            return da > db;
        const int c = compareNames(paths::pageTitle(a), paths::pageTitle(b));
        return c != 0 ? c < 0 : a < b;
    });
}

} // namespace

// ---- GroupedRefsModel -----------------------------------------------------------------------------

GroupedRefsModel::GroupedRefsModel(QObject *parent) : WorkspaceListModel(parent)
{
    setRefreshDelay(200);
}

void GroupedRefsModel::setPalette(const QVariantMap &palette)
{
    if (m_paletteMap == palette)
        return;
    m_paletteMap = palette;
    m_palette = paletteFromMap(palette);
    Q_EMIT paletteChanged();
    scheduleRefresh(0);
}

void GroupedRefsModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    scheduleRefresh(0);
}

void GroupedRefsModel::setToday(const QString &today)
{
    if (m_today == today)
        return;
    m_today = today;
    Q_EMIT todayChanged();
    scheduleRefresh(0);
}

void GroupedRefsModel::setCollapsedPaths(const QStringList &paths)
{
    if (m_collapsed == paths)
        return;
    m_collapsed = paths;
    Q_EMIT collapsedPathsChanged();
    scheduleRefresh(0);
}

void GroupedRefsModel::setLimit(int limit)
{
    limit = std::max(0, limit);
    if (m_limit == limit)
        return;
    m_limit = limit;
    Q_EMIT limitChanged();
    scheduleRefresh(0);
}

void GroupedRefsModel::toggleCollapsed(const QString &path)
{
    QStringList paths = m_collapsed;
    if (paths.contains(path))
        paths.removeAll(path);
    else
        paths.append(path);
    setCollapsedPaths(paths);
    refreshNow();
}

QDate GroupedRefsModel::effectiveToday() const
{
    const QDate fixed = parseIsoDate(m_today);
    if (fixed.isValid())
        return fixed;
    return workspace() ? workspace()->now().date() : QDate::currentDate();
}

dates::UiLocale GroupedRefsModel::uiLocaleValue() const
{
    return uiLocale(m_locale);
}

GroupedRefsModel::Group GroupedRefsModel::groupFor(const QString &path) const
{
    Group g;
    g.path = path;
    const VaultIndex *idx = vaultIndex();
    const ParsedNote *note = idx ? idx->note(path) : nullptr;
    g.title = note ? note->title : paths::pageTitle(path);
    g.folder = paths::dirname(path);
    if (note && note->journalDate)
        g.journalDate = isoDate(*note->journalDate);
    return g;
}

GroupedRefsModel::Ref GroupedRefsModel::refForBlock(const QString &path, int blockIndex) const
{
    Ref r;
    const VaultIndex *idx = vaultIndex();
    const ParsedNote *note = idx ? idx->note(path) : nullptr;
    if (!note || blockIndex < 0 || blockIndex >= note->blocks.size())
        return r;
    r.line = note->blocks.at(blockIndex).line;
    r.pendingBlock = blockIndex;
    return r;
}

void GroupedRefsModel::fillRef(Ref &r, const QString &path) const
{
    if (r.pendingBlock < 0)
        return;
    const int blockIndex = r.pendingBlock;
    r.pendingBlock = -1;
    const VaultIndex *idx = vaultIndex();
    const ParsedNote *note = idx ? idx->note(path) : nullptr;
    if (!note || blockIndex >= note->blocks.size())
        return;
    const Block &block = note->blocks.at(blockIndex);
    r.line = block.line;
    r.text = oneLine(plainText(block.text, idx), 240);
    r.blockId = block.id;
    if (block.task)
        r.taskStatus = statusName(block.task->status);
    QStringList crumbs;
    for (int p = block.parent; p >= 0 && p < note->blocks.size(); p = note->blocks.at(p).parent)
        crumbs.prepend(oneLine(plainText(note->blocks.at(p).text, idx), 48));
    crumbs.removeAll(QString());
    r.breadcrumb = crumbs.join(u" › "_s);

    std::shared_ptr<markdown::BlockDocument> &doc = m_docs[path];
    if (!doc)
        doc = std::make_shared<markdown::BlockDocument>(idx->content(path));
    const int i = doc->indexAtLine(block.line);
    if (i >= 0) {
        const markdown::BlockEntry &entry = doc->at(i);
        markdown::RenderContext ctx = renderContext(idx, path, effectiveToday(), uiLocaleValue(), m_palette);
        // The row draws the editor's checkbox for tasks (ContentRefRow), not a text glyph.
        ctx.taskCheckbox = false;
        r.html = markdown::InlineRenderer(ctx).renderBlock(entry.body, entry.kind);
    }
    if (r.html.isEmpty())
        r.html = markdown::InlineRenderer::escape(r.text);
}

GroupedRefsModel::Ref GroupedRefsModel::refForLine(const QString &path, int line) const
{
    Ref r;
    r.line = line;
    const VaultIndex *idx = vaultIndex();
    const QList<text::Line> lines = text::splitLines(idx ? idx->content(path) : QString());
    if (line >= 0 && line < lines.size())
        r.text = oneLine(plainText(lines.at(line).text, idx), 240);
    r.html = markdown::InlineRenderer::escape(r.text);
    return r;
}

void GroupedRefsModel::refresh()
{
    m_docs.clear();
    QList<Group> groups;
    if (workspace() && vaultIndex() && workspace()->isOpen())
        groups = collect();
    const auto dropDocs = qScopeGuard([this] { m_docs.clear(); });

    QList<Row> next;
    QSet<QString> keys;
    int refs = 0;
    int shown = 0;
    int hidden = 0;
    for (const Group &g : std::as_const(groups)) {
        const bool collapsed = m_collapsed.contains(g.path);
        // Past the limit: count what is left out, list no more groups.
        if (m_limit > 0 && shown >= m_limit) {
            refs += int(g.refs.size()) + (m_pageLevelCounts && g.pageLevel ? 1 : 0);
            if (!collapsed)
                hidden += int(g.refs.size());
            continue;
        }
        Row h;
        h.header = true;
        h.path = g.path;
        h.title = g.title;
        h.folder = g.folder;
        h.journalDate = g.journalDate;
        h.count = int(g.refs.size());
        h.pageLevel = g.pageLevel;
        h.collapsed = collapsed;
        h.key = u"p\n"_s + g.path;
        if (keys.contains(h.key))
            continue;
        keys.insert(h.key);
        next.append(h);
        refs += h.count + (m_pageLevelCounts && h.pageLevel ? 1 : 0);
        if (h.collapsed)
            continue;
        for (const Ref &r : g.refs) {
            if (m_limit > 0 && shown >= m_limit) {
                ++hidden;
                continue;
            }
            ++shown;
            Row row;
            row.path = g.path;
            row.title = g.title;
            row.folder = g.folder;
            row.journalDate = g.journalDate;
            row.ref = r;
            fillRef(row.ref, g.path); // only the listed rows are rendered
            row.key = u"r\n"_s + g.path + u'\n' + QString::number(r.line);
            for (int n = 2; keys.contains(row.key); ++n)
                row.key = u"r\n"_s + g.path + u'\n' + QString::number(r.line) + u'#' + QString::number(n);
            keys.insert(row.key);
            next.append(row);
        }
    }
    applyRows(m_rows, next, [](const Row &r) { return r.key; }, [](const Row &a, const Row &b) { return a == b; });
    const int pages = int(groups.size());
    if (refs != m_refCount || pages != m_pageCount || hidden != m_hidden) {
        m_refCount = refs;
        m_pageCount = pages;
        m_hidden = hidden;
        Q_EMIT totalsChanged();
    }
}

QVariant GroupedRefsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case RowTypeRole: return r.header ? u"page"_s : u"ref"_s;
    case PathRole: return r.path;
    case TitleRole: return r.title;
    case FolderRole: return r.folder;
    case JournalDateRole: return r.journalDate;
    case CountRole: return r.count;
    case CollapsedRole: return r.collapsed;
    case PageLevelRole: return r.pageLevel;
    case LineRole: return r.header ? -1 : r.ref.line;
    case HtmlRole: return r.ref.html;
    case TextRole: return r.header ? r.title : r.ref.text;
    case BlockIdRole: return r.ref.blockId;
    case BreadcrumbRole: return r.ref.breadcrumb;
    case TaskStatusRole: return r.ref.taskStatus;
    case DepthRole: return 0;
    }
    return {};
}

QHash<int, QByteArray> GroupedRefsModel::roleNames() const
{
    return { { RowTypeRole, "rowType" },     { PathRole, "path" },         { TitleRole, "title" },
             { FolderRole, "folder" },       { JournalDateRole, "journalDate" }, { CountRole, "count" },
             { CollapsedRole, "collapsed" }, { PageLevelRole, "pageLevel" }, { LineRole, "line" },
             { HtmlRole, "html" },           { TextRole, "text" },         { BlockIdRole, "blockId" },
             { BreadcrumbRole, "breadcrumb" }, { TaskStatusRole, "taskStatus" }, { DepthRole, "depth" } };
}

QVariantMap GroupedRefsModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return out;
}

bool GroupedRefsModel::toggleTask(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).header || !workspace())
        return false;
    const Row &r = m_rows.at(row);
    if (r.ref.taskStatus.isEmpty())
        return false;
    const bool closed = r.ref.taskStatus == u"done" || r.ref.taskStatus == u"cancelled";
    return workspace()->setTaskStatus(r.path, r.ref.line, closed ? u"todo"_s : u"done"_s);
}

bool GroupedRefsModel::setTaskStatus(int row, const QString &status)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).header || !workspace())
        return false;
    const Row &r = m_rows.at(row);
    if (r.ref.taskStatus.isEmpty() || r.ref.taskStatus == status)
        return false;
    return workspace()->setTaskStatus(r.path, r.ref.line, status);
}

// ---- BacklinksModel ---------------------------------------------------------------------------------

BacklinksModel::BacklinksModel(QObject *parent) : GroupedRefsModel(parent)
{
    setPageLevelCounts(true); // a page tagged as a whole is one reference
}

void BacklinksModel::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    Q_EMIT pathChanged();
    scheduleRefresh(0);
}

void BacklinksModel::setMode(const QString &mode)
{
    const QString m = mode == u"unlinked" ? u"unlinked"_s : u"linked"_s;
    if (m_mode == m)
        return;
    m_mode = m;
    Q_EMIT modeChanged();
    scheduleRefresh(0);
}

QStringList BacklinksModel::names() const
{
    const VaultIndex *idx = vaultIndex();
    const ParsedNote *note = idx ? idx->note(paths::normalize(m_path)) : nullptr;
    QStringList out{ note ? note->title : paths::pageTitle(m_path) };
    if (note)
        out.append(note->aliases);
    out.removeAll(QString());
    return out;
}

QList<GroupedRefsModel::Group> BacklinksModel::collect()
{
    if (m_path.isEmpty())
        return {};
    return m_mode == u"unlinked" ? collectUnlinked() : collectLinked();
}

QList<GroupedRefsModel::Group> BacklinksModel::collectLinked()
{
    const VaultIndex *idx = vaultIndex();
    const QString path = paths::normalize(m_path);
    QList<Group> groups;
    QHash<QString, qsizetype> groupOf;
    const auto group = [&](const QString &source) -> Group & {
        qsizetype gi = groupOf.value(source, -1);
        if (gi < 0) {
            groups.append(groupFor(source));
            gi = groups.size() - 1;
            groupOf.insert(source, gi);
        }
        return groups[gi];
    };
    // One row per referencing block (a block may link several times, and also carry a tag).
    QSet<QString> seen;
    for (const Backlink &b : idx->backlinks(path)) {
        Group &g = group(b.path);
        const QString key = b.path + u'\n'
                            + (b.blockIndex >= 0 ? QString::number(b.blockIndex) : u"L"_s + QString::number(b.link.line));
        if (seen.contains(key))
            continue;
        seen.insert(key);
        g.refs.append(b.blockIndex >= 0 ? refForBlock(b.path, b.blockIndex) : refForLine(b.path, b.link.line));
    }
    // A tag names a page (docs/SYNTAX.md §3): the tagged blocks join their page's group; a page
    // tagged as a whole marks its group ("cała strona").
    const QList<TagReference> tagged = idx->tagReferences(path);
    for (const TagReference &r : tagged) {
        Group &g = group(r.path);
        if (r.blockIndex < 0) {
            g.pageLevel = true;
            continue;
        }
        const QString key = r.path + u'\n' + QString::number(r.blockIndex);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        g.refs.append(refForBlock(r.path, r.blockIndex));
    }
    if (!tagged.isEmpty()) {
        // Journals first (newest first), then the other pages by path — the order of both lists —
        // and the references of a page in document order.
        const auto journal = [idx](const QString &p) {
            const ParsedNote *n = idx->note(p);
            return n && n->journalDate ? *n->journalDate : QDate();
        };
        std::stable_sort(groups.begin(), groups.end(), [&](const Group &a, const Group &b) {
            const QDate da = journal(a.path);
            const QDate db = journal(b.path);
            if (da.isValid() != db.isValid())
                return da.isValid();
            if (da.isValid() && da != db)
                return da > db;
            return a.path < b.path;
        });
        for (Group &g : groups)
            std::stable_sort(g.refs.begin(), g.refs.end(), [](const Ref &a, const Ref &b) { return a.line < b.line; });
    }
    if (m_taggedCount != tagged.size()) {
        m_taggedCount = int(tagged.size());
        Q_EMIT taggedCountChanged();
    }
    return groups;
}

QList<GroupedRefsModel::Group> BacklinksModel::collectUnlinked()
{
    const VaultIndex *idx = vaultIndex();
    const QStringList pageNames = names();
    QList<Group> groups;
    QHash<QString, qsizetype> groupOf;
    for (const UnlinkedRef &u : idx->unlinkedReferences(paths::normalize(m_path))) {
        qsizetype gi = groupOf.value(u.path, -1);
        if (gi < 0) {
            groups.append(groupFor(u.path));
            gi = groups.size() - 1;
            groupOf.insert(u.path, gi);
        }
        Ref r;
        r.line = u.line;
        r.text = u.excerpt;
        r.html = highlightHtml(u.excerpt, mentionPositions(u.excerpt, pageNames), rendererPalette().highlightBackground);
        groups[gi].refs.append(r);
    }
    return groups;
}

QString BacklinksModel::linkedContent(const QString &source, const QString &content, int line) const
{
    const VaultIndex *idx = vaultIndex();
    const QString target = paths::normalize(m_path);
    const MentionEdit edit = content::linkMention(content, line, names(), [&](const QString &mention) {
        return mentionLink(idx, mention, target, source);
    });
    return edit.ok() ? edit.content : QString();
}

bool BacklinksModel::linkMention(int row)
{
    const QVariantMap r = get(row);
    if (m_mode != u"unlinked" || r.isEmpty() || r.value(u"rowType"_s) != u"ref"_s || !workspace())
        return false;
    const QString source = r.value(u"path"_s).toString();
    const QString content = workspace()->readFile(source);
    const QString linked = linkedContent(source, content, r.value(u"line"_s).toInt());
    if (linked.isEmpty() || linked == content) {
        scheduleRefresh(0); // the mention is gone: the list is out of date
        return false;
    }
    const bool ok = workspace()->writeFile(source, linked);
    scheduleRefresh(0);
    return ok;
}

int BacklinksModel::linkAll()
{
    if (m_mode != u"unlinked" || !workspace())
        return 0;
    // Every listed mention, also of collapsed groups: from the index, grouped by file.
    const VaultIndex *idx = vaultIndex();
    if (!idx)
        return 0;
    QHash<QString, QList<int>> lines;
    QStringList order;
    for (const UnlinkedRef &u : idx->unlinkedReferences(paths::normalize(m_path))) {
        if (!lines.contains(u.path))
            order.append(u.path);
        lines[u.path].append(u.line);
    }
    int linked = 0;
    for (const QString &source : std::as_const(order)) {
        const QString original = workspace()->readFile(source);
        QString content = original;
        int here = 0;
        for (const int line : lines.value(source)) {
            const QString next = linkedContent(source, content, line);
            if (!next.isEmpty() && next != content) {
                content = next;
                ++here;
            }
        }
        if (here > 0 && content != original && workspace()->writeFile(source, content))
            linked += here;
    }
    scheduleRefresh(0);
    return linked;
}

// ---- TagBlocksModel -----------------------------------------------------------------------------------

TagBlocksModel::TagBlocksModel(QObject *parent) : GroupedRefsModel(parent) { }

void TagBlocksModel::setTag(const QString &tag)
{
    const QString key = normalizeTag(tag);
    if (m_tag == key)
        return;
    m_tag = key;
    m_display = key;
    Q_EMIT tagChanged();
    scheduleRefresh(0);
}

QString TagBlocksModel::parentTag() const
{
    const qsizetype slash = m_tag.lastIndexOf(u'/');
    return slash > 0 ? m_tag.left(slash) : QString();
}

QList<GroupedRefsModel::Group> TagBlocksModel::collect()
{
    QList<Group> groups;
    const QVariantList oldSubTags = m_subTags;
    const QString oldDisplay = m_display;
    m_subTags.clear();
    // The display spelling and the sub-tags are published with the totals.
    const auto publish = qScopeGuard([&] {
        if (m_subTags != oldSubTags || m_display != oldDisplay)
            Q_EMIT totalsChanged();
    });
    if (m_tag.isEmpty())
        return groups;
    const VaultIndex *idx = vaultIndex();
    const QString prefix = m_tag + u'/';
    const auto matches = [&](const QString &t) { return t == m_tag || t.startsWith(prefix); };
    QSet<QString> notes;
    for (const TagCount &tc : idx->tags()) {
        if (tc.tag == m_tag) {
            m_display = tc.display.isEmpty() ? tc.tag : tc.display;
            for (const QString &p : tc.paths)
                notes.insert(p);
        } else if (tc.tag.startsWith(prefix)) {
            m_subTags.append(QVariantMap{ { u"tag"_s, tc.tag }, { u"display"_s, tc.display }, { u"count"_s, tc.count } });
            for (const QString &p : tc.paths)
                notes.insert(p);
        }
    }
    QList<QString> ordered(notes.cbegin(), notes.cend());
    sortNotes(ordered, idx);
    for (const QString &path : std::as_const(ordered)) {
        const ParsedNote *note = idx->note(path);
        if (!note)
            continue;
        Group g = groupFor(path);
        for (int i = 0; i < note->blocks.size(); ++i) {
            const QStringList &tags = note->blocks.at(i).tags;
            if (std::any_of(tags.cbegin(), tags.cend(), matches))
                g.refs.append(refForBlock(path, i));
        }
        const QStringList own = pageTags(*note);
        g.pageLevel = std::any_of(own.cbegin(), own.cend(), matches) || g.refs.isEmpty();
        groups.append(g);
    }
    return groups;
}

} // namespace brulion::content
