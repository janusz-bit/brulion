#include "streamselection.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include "dates/names.h"
#include "markdown/blockclipboard.h"
#include "markdown/linkformat.h"

#include <QDate>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using markdown::BlockDocument;

namespace {

QVariantMap failure(const QString &reason)
{
    return {{u"ok"_s, false}, {u"reason"_s, reason}, {u"count"_s, 0}, {u"pages"_s, 0}};
}

/// The first row a range may start at (after the frontmatter / page properties), the last row;
/// (-1, -1) for a page with no blocks to select.
std::pair<int, int> documentEdges(const BlockDocument &doc)
{
    int first = 0;
    while (first < doc.count() && doc.at(first).kind == BlockKind::Frontmatter)
        ++first;
    if (first >= doc.count())
        return {-1, -1};
    return {first, doc.count() - 1};
}

std::pair<int, int> visibleEdges(const BlockListModel *model)
{
    const BlockDocument &doc = model->document();
    int first = model->firstVisibleRow();
    while (first >= 0 && doc.at(first).kind == BlockKind::Frontmatter)
        first = model->nextVisibleRow(first);
    const int last = model->lastVisibleRow();
    if (first < 0 || last < first)
        return {-1, -1};
    return {first, last};
}

QString stemOf(const QString &path)
{
    QString stem = path.mid(path.lastIndexOf(u'/') + 1);
    if (stem.endsWith(u".md"_s, Qt::CaseInsensitive))
        stem.chop(3);
    return stem;
}

} // namespace

StreamSelection::StreamSelection(QObject *parent)
    : QObject(parent)
{
}

StreamSelection::~StreamSelection()
{
    for (const QPointer<PageController> &page : std::as_const(m_pages)) {
        if (page)
            disconnect(page, nullptr, this, nullptr);
    }
}

void StreamSelection::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    m_host = host;
    Q_EMIT hostChanged();
}

void StreamSelection::setModel(QAbstractItemModel *model)
{
    if (m_model == model)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_modelConnections))
        disconnect(c);
    m_modelConnections.clear();
    m_model = model;
    if (model) {
        const auto read = [this] { readPaths(); };
        m_modelConnections = {
                connect(model, &QAbstractItemModel::modelReset, this, read),
                connect(model, &QAbstractItemModel::rowsInserted, this, read),
                connect(model, &QAbstractItemModel::rowsRemoved, this, read),
                connect(model, &QAbstractItemModel::rowsMoved, this, read),
                connect(model, &QAbstractItemModel::layoutChanged, this, read),
                connect(model, &QAbstractItemModel::dataChanged, this, read),
                connect(model, &QObject::destroyed, this, read),
        };
    }
    readPaths();
    Q_EMIT modelChanged();
}

void StreamSelection::setLocale(const QString &locale)
{
    const bool english = locale.startsWith(u"en"_s);
    if (english == m_english)
        return;
    m_english = english;
    Q_EMIT localeChanged();
}

void StreamSelection::readPaths()
{
    QStringList paths;
    QStringList dates;
    if (const QAbstractItemModel *model = m_model.data()) {
        const QHash<int, QByteArray> roles = model->roleNames();
        const int pathRole = roles.key("path", -1);
        const int dateRole = roles.key("date", -1);
        for (int r = 0; pathRole >= 0 && r < model->rowCount(); ++r) {
            const QModelIndex index = model->index(r, 0);
            paths.append(model->data(index, pathRole).toString());
            dates.append(dateRole >= 0 ? model->data(index, dateRole).toString() : QString());
        }
    }
    if (paths == m_paths && dates == m_dates)
        return;
    m_paths = paths;
    m_dates = dates;
    m_fileCounts.clear();
    // An end of the span left the stream: the span ends (what is left stays where it is).
    if (m_active && (indexOf(m_anchorPath) < 0 || indexOf(m_focusPath) < 0)) {
        clear();
        return;
    }
    if (m_active)
        apply();
    update();
}

PageController *StreamSelection::attached(const QString &path) const
{
    for (const QPointer<PageController> &page : m_pages) {
        if (page && page->path() == path)
            return page;
    }
    return nullptr;
}

int StreamSelection::pageCount() const
{
    const auto [lo, hi] = bounds();
    return lo < 0 ? 0 : hi - lo + 1;
}

std::pair<int, int> StreamSelection::bounds() const
{
    if (!m_active)
        return {-1, -1};
    const int a = indexOf(m_anchorPath);
    const int f = indexOf(m_focusPath);
    if (a < 0 || f < 0)
        return {-1, -1};
    return {std::min(a, f), std::max(a, f)};
}

std::pair<int, int> StreamSelection::rowsIn(const QString &path, int first, int last) const
{
    const int index = indexOf(path);
    const auto [lo, hi] = bounds();
    if (lo < 0 || index < lo || index > hi || first < 0 || last < first)
        return {-1, -1};
    const bool down = indexOf(m_anchorPath) < indexOf(m_focusPath);
    const auto clamp = [&](int row) { return std::clamp(row, first, last); };
    if (path == m_anchorPath)
        return {clamp(m_anchorRow), down ? last : first};
    if (path == m_focusPath)
        return {down ? first : last, clamp(m_focusRow)};
    return {first, last};
}

std::pair<int, int> StreamSelection::rowsIn(PageController *page) const
{
    if (!page || !page->loaded())
        return {-1, -1};
    const auto [first, last] = visibleEdges(page->blocks());
    return rowsIn(page->path(), first, last);
}

void StreamSelection::attach(PageController *page)
{
    if (!page || m_pages.contains(page))
        return;
    m_pages.append(page);
    connect(page, &PageController::selectionChanged, this, [this, page] { pageChanged(page); });
    connect(page, &PageController::editingRowChanged, this, [this, page] { pageChanged(page); });
    // Loaded later (a day created while scrolling): its part of the span then.
    connect(page, &PageController::loadingChanged, this, [this, page] {
        if (!m_active || !page->loaded() || page->loading())
            return;
        ++m_applying;
        applyTo(page);
        --m_applying;
        update();
    });
    connect(page, &PageController::pathChanged, this, [this] {
        if (m_active) {
            apply();
            update();
        }
    });
    connect(page, &QObject::destroyed, this, [this, page] {
        m_pages.removeAll(nullptr);
        if (m_tools == page)
            m_tools = nullptr;
        update();
    });
    if (m_active && page->loaded() && !page->loading()) {
        ++m_applying;
        applyTo(page);
        --m_applying;
    }
    update();
}

void StreamSelection::detach(PageController *page)
{
    if (!page || !m_pages.contains(page))
        return;
    disconnect(page, nullptr, this, nullptr);
    m_pages.removeAll(page);
    m_pages.removeAll(nullptr);
    update();
}

void StreamSelection::pageChanged(PageController *page)
{
    if (m_applying > 0 || !m_active)
        return;
    // The user selected, edited or clicked in a day: it alone keeps its selection.
    end(page);
}

void StreamSelection::applyTo(PageController *page)
{
    if (!page || !page->loaded())
        return;
    const auto [anchor, focus] = rowsIn(page);
    if (anchor < 0)
        page->clearSelection();
    else
        page->selectRange(anchor, focus);
}

void StreamSelection::apply()
{
    ++m_applying;
    for (const QPointer<PageController> &page : std::as_const(m_pages))
        applyTo(page);
    --m_applying;
}

void StreamSelection::end(PageController *except)
{
    m_active = false;
    ++m_applying;
    for (const QPointer<PageController> &page : std::as_const(m_pages)) {
        if (page && page != except)
            page->clearSelection();
    }
    --m_applying;
    m_fileCounts.clear();
    update();
}

void StreamSelection::clear()
{
    end(nullptr);
}

void StreamSelection::collapseTo(int row)
{
    // Back in the anchor page: that page alone, from the anchor to `row`.
    PageController *anchorPage = attached(m_anchorPath);
    m_active = false;
    ++m_applying;
    for (const QPointer<PageController> &page : std::as_const(m_pages)) {
        if (page && page != anchorPage)
            page->clearSelection();
    }
    if (anchorPage)
        anchorPage->selectRange(m_anchorRow, row);
    --m_applying;
    m_fileCounts.clear();
    update();
    if (anchorPage)
        Q_EMIT focusMoved(anchorPage, anchorPage->selectedRow(), true);
}

void StreamSelection::setFocus(const QString &path, int row, bool reveal)
{
    if (m_active && path == m_focusPath && row == m_focusRow)
        return;
    m_focusPath = path;
    m_focusRow = row;
    m_active = true;
    apply();
    update();
    if (PageController *page = attached(path); page && page->loaded())
        Q_EMIT focusMoved(page, row, reveal);
    else if (reveal)
        Q_EMIT revealRequested(path);
}

bool StreamSelection::extendTo(PageController *page, int row)
{
    if (!page || !page->loaded() || row < 0 || row >= page->blocks()->count() || indexOf(page->path()) < 0)
        return false;
    if (!m_active) {
        PageController *from = nullptr;
        int fromRow = -1;
        for (const QPointer<PageController> &other : std::as_const(m_pages)) {
            if (!other || other == page || indexOf(other->path()) < 0)
                continue;
            if (other->selectionCount() > 0 && other->selectionAnchorRow() >= 0) {
                from = other;
                fromRow = other->selectionAnchorRow();
                break;
            }
            if (other->editingRow() >= 0) {
                from = other;
                fromRow = other->editingRow();
                break;
            }
        }
        if (!from)
            return false;
        m_anchorPath = from->path();
        m_anchorRow = fromRow;
    } else if (page->path() == m_anchorPath) {
        collapseTo(row);
        return true;
    }
    setFocus(page->path(), row, false);
    return true;
}

bool StreamSelection::extendFrom(PageController *from, int fromRow, PageController *page, int row)
{
    if (!from || !page || from == page || fromRow < 0 || !page->loaded() || row < 0
        || row >= page->blocks()->count() || indexOf(from->path()) < 0 || indexOf(page->path()) < 0)
        return false;
    if (!m_active || m_anchorPath != from->path() || m_anchorRow != fromRow) {
        m_active = false; // a new anchor: setFocus() applies it
        m_anchorPath = from->path();
        m_anchorRow = fromRow;
    }
    setFocus(page->path(), row, false);
    return true;
}

bool StreamSelection::extendBy(PageController *page, int step)
{
    if (step == 0)
        return false;
    const int dir = step > 0 ? 1 : -1;
    // The page edge row a step into page `index` reaches: its first block going down, its last one
    // going up; pages with nothing to select are passed.
    const auto edgeOf = [&](int index, QString *path) {
        for (; index >= 0 && index < m_paths.size(); index += dir) {
            std::pair<int, int> edges{-1, -1};
            if (PageController *p = attached(m_paths.at(index)); p && p->loaded()) {
                edges = visibleEdges(p->blocks());
            } else if (m_host) {
                bool ok = false;
                const QString text = m_host->read(m_paths.at(index), &ok);
                if (ok)
                    edges = documentEdges(BlockDocument(text));
            }
            if (edges.first >= 0) {
                *path = m_paths.at(index);
                return dir > 0 ? edges.first : edges.second;
            }
        }
        return -1;
    };
    QString path;
    if (!m_active) {
        if (!page || indexOf(page->path()) < 0)
            return false;
        const int anchor = page->selectionAnchorRow() >= 0 ? page->selectionAnchorRow() : page->editingRow();
        if (anchor < 0)
            return false;
        const int row = edgeOf(indexOf(page->path()) + dir, &path);
        if (row < 0)
            return false;
        m_anchorPath = page->path();
        m_anchorRow = anchor;
        setFocus(path, row, true);
        return true;
    }
    PageController *focusPage = attached(m_focusPath);
    if (!focusPage || !focusPage->loaded()) {
        Q_EMIT revealRequested(m_focusPath);
        return true;
    }
    ++m_applying;
    const bool moved = focusPage->extendSelection(dir);
    --m_applying;
    if (moved) {
        m_focusRow = focusPage->selectedRow();
        update();
        Q_EMIT focusMoved(focusPage, m_focusRow, true);
        return true;
    }
    const int row = edgeOf(indexOf(m_focusPath) + dir, &path);
    if (row < 0)
        return false;
    if (path == m_anchorPath)
        collapseTo(row);
    else
        setFocus(path, row, true);
    return true;
}

bool StreamSelection::othersSelected(PageController *page) const
{
    for (const QPointer<PageController> &other : m_pages) {
        if (other && other != page && other->selectionCount() > 0)
            return true;
    }
    return false;
}

StreamSelection::Part StreamSelection::partOf(const QString &path, bool withText) const
{
    Part part;
    part.path = path;
    if (PageController *page = attached(path); page && page->loaded()) {
        if (withText) {
            part.markdown = page->selectionMarkdown();
            part.plainText = page->selectionPlainText();
        }
        part.count = page->selectionCount();
        return part;
    }
    if (!m_host)
        return part;
    bool ok = false;
    const QString text = m_host->read(path, &ok);
    if (!ok)
        return part;
    const BlockDocument doc(text);
    const auto [first, last] = documentEdges(doc);
    const auto [anchor, focus] = rowsIn(path, first, last);
    if (anchor < 0)
        return part;
    QList<int> rows;
    for (int r = std::min(anchor, focus); r <= std::max(anchor, focus); ++r)
        rows.append(r);
    const QList<int> tops = markdown::topRows(doc, rows);
    for (const int top : tops)
        part.count += doc.subtreeRange(top).count;
    if (withText) {
        part.markdown = markdown::blocksMarkdown(doc, tops);
        markdown::PlainTextOptions options;
        const QPointer<EditorHost> host = m_host;
        options.blockText = [host](const QString &id) { return host ? host->blockText(id) : QString(); };
        part.plainText = markdown::blocksPlainText(doc, tops, options);
    }
    return part;
}

QString StreamSelection::label(const QString &path, bool plainText) const
{
    if (plainText) {
        const int index = indexOf(path);
        const QDate d = QDate::fromString(index >= 0 ? m_dates.at(index) : QString(), u"yyyy-MM-dd"_s);
        if (!d.isValid())
            return stemOf(path);
        // As the day's heading in the stream (DatesHelper.formatLong).
        const dates::UiLocale loc = m_english ? dates::UiLocale::English : dates::UiLocale::Polish;
        const QString weekday = dates::weekdayName(d.dayOfWeek(), loc, dates::NameForm::Nominative);
        if (m_english) {
            return u"%1, %2 %3, %4"_s.arg(weekday, dates::monthName(d.month(), loc, dates::NameForm::Nominative),
                                          QString::number(d.day()), QString::number(d.year()));
        }
        return u"%1, %2 %3 %4"_s.arg(weekday, QString::number(d.day()),
                                     dates::monthName(d.month(), loc, dates::NameForm::Genitive),
                                     QString::number(d.year()));
    }
    QString target = m_host ? m_host->linkTarget(path, QString()) : QString();
    if (target.isEmpty())
        target = stemOf(path);
    return markdown::formatReference(markdown::ReferenceKind::PageLink, target);
}

QString StreamSelection::joined(bool plainText) const
{
    const auto [lo, hi] = bounds();
    if (lo < 0)
        return {};
    QStringList parts;
    for (int i = lo; i <= hi; ++i) {
        const Part part = partOf(m_paths.at(i), true);
        const QString &text = plainText ? part.plainText : part.markdown;
        if (text.isEmpty())
            continue;
        parts.append(label(part.path, plainText) + (plainText ? u"\n"_s : u"\n\n"_s) + text);
    }
    return parts.join(u"\n\n"_s);
}

QString StreamSelection::markdown() const
{
    return joined(false);
}

QString StreamSelection::plainText() const
{
    return joined(true);
}

QVariantMap StreamSelection::copy(bool plainText)
{
    if (!m_active)
        return failure(u"empty"_s);
    const QString text = joined(plainText);
    if (text.isEmpty())
        return failure(u"empty"_s);
    LazyHtmlMimeData::Render html;
    if (!plainText && m_host) {
        // All journal days share a folder: links resolve the same from the first one.
        const QPointer<EditorHost> host = m_host;
        html = [host, text, path = m_paths.at(bounds().first)] { return host ? host->renderHtml(text, path) : QString(); };
    }
    QVariantMap result = PageController::writeClipboard(text, html);
    if (result.value(u"ok"_s).toBool()) {
        result.insert(u"count"_s, std::max(1, m_count));
        result.insert(u"pages"_s, pageCount());
    } else {
        result.insert(u"pages"_s, 0);
    }
    return result;
}

void StreamSelection::update()
{
    int count = 0;
    PageController *tools = nullptr;
    const auto [lo, hi] = bounds();
    for (int i = lo; lo >= 0 && i <= hi; ++i) {
        const QString &path = m_paths.at(i);
        if (PageController *page = attached(path); page && page->loaded()) {
            count += page->selectionCount();
            continue;
        }
        const auto [first, last] = std::pair<int, int>{i == indexOf(m_anchorPath) ? m_anchorRow : -1,
                                                       i == indexOf(m_focusPath) ? m_focusRow : -1};
        const QString key = path + u'\n' + QString::number(first) + u',' + QString::number(last);
        auto it = m_fileCounts.constFind(key);
        if (it == m_fileCounts.cend())
            it = m_fileCounts.insert(key, partOf(path, false).count);
        count += it.value();
    }
    if (m_active) {
        tools = attached(m_focusPath);
        if (!tools)
            tools = attached(m_anchorPath);
        for (int i = lo; !tools && lo >= 0 && i <= hi; ++i)
            tools = attached(m_paths.at(i));
    }
    m_count = count;
    m_tools = tools;
    Q_EMIT changed();
}

} // namespace brulion::editor
