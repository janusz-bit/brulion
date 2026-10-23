#include "fakeeditorhost.h"

#include "blocktext.h"

#include "dates/local.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/edit.h"
#include "markdown/frontmatter.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "markdown/properties.h"
#include "text/text.h"
#include "util/paths.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QSet>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// A calm sketch-like picture for the sample image embed (no external files needed).
QByteArray sampleImagePng()
{
    QImage img(720, 360, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, 360);
    sky.setColorAt(0, QColor(u"#9fc3e6"_s));
    sky.setColorAt(1, QColor(u"#fbf7ee"_s));
    p.fillRect(img.rect(), sky);
    QRadialGradient sun(560, 110, 70);
    sun.setColorAt(0, QColor(u"#f2b33d"_s));
    sun.setColorAt(1, QColor(242, 179, 61, 0));
    p.setBrush(sun);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(560, 110), 70, 70);
    // Floor plan of a bathroom: tiles (hexagons) and fixtures.
    p.setBrush(QColor(u"#f3eee2"_s));
    p.setPen(QPen(QColor(u"#1f2430"_s), 3));
    p.drawRoundedRect(QRectF(60, 60, 380, 250), 10, 10);
    p.setPen(QPen(QColor(u"#cdc2ab"_s), 1.2));
    for (int row = 0; row < 9; ++row) {
        for (int col = 0; col < 14; ++col) {
            const double r = 14;
            const double x = 80 + col * r * 1.75 + (row % 2) * r * 0.875;
            const double y = 80 + row * r * 1.5;
            if (x > 425 || y > 295)
                continue;
            QPainterPath hex;
            for (int k = 0; k < 6; ++k) {
                const double a = (60.0 * k + 30.0) * 3.14159265 / 180.0;
                const QPointF pt(x + r * std::cos(a), y + r * std::sin(a));
                if (k == 0)
                    hex.moveTo(pt);
                else
                    hex.lineTo(pt);
            }
            hex.closeSubpath();
            p.drawPath(hex);
        }
    }
    p.setPen(QPen(QColor(u"#1f2430"_s), 2.5));
    p.setBrush(QColor(u"#fffdf7"_s));
    p.drawRoundedRect(QRectF(80, 80, 120, 70), 30, 30);   // bathtub
    p.drawEllipse(QRectF(360, 230, 56, 56));               // shower
    p.drawRoundedRect(QRectF(260, 80, 70, 50), 14, 14);     // sink
    p.setPen(QPen(QColor(u"#e2574c"_s), 3));
    p.drawLine(QPointF(60, 330), QPointF(440, 330));
    p.drawLine(QPointF(60, 322), QPointF(60, 338));
    p.drawLine(QPointF(440, 322), QPointF(440, 338));
    p.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return bytes;
}

} // namespace

namespace {

/// The shell's QueryHost item shape (src/app/shell/workspacequery.cpp, editor/queryhost.h).
QString statusName(TaskStatus status)
{
    return taskStatusName(status);
}

QString priorityName(Priority priority)
{
    switch (priority) {
    case Priority::None: return {};
    case Priority::Lowest: return u"lowest"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::High: return u"high"_s;
    case Priority::Highest: return u"highest"_s;
    }
    return {};
}

QVariantMap taskMap(const Task &task)
{
    QVariantMap m{{u"status"_s, statusName(task.status)},
                  {u"marker"_s, task.marker},
                  {u"text"_s, task.text},
                  {u"priority"_s, priorityName(task.priority)}};
    if (task.due)
        m.insert(u"due"_s, task.due->toString());
    if (task.scheduled)
        m.insert(u"scheduled"_s, task.scheduled->toString());
    if (task.start)
        m.insert(u"start"_s, task.start->toString());
    if (task.doneDate)
        m.insert(u"done"_s, task.doneDate->toString(Qt::ISODate));
    if (!task.recurrence.isEmpty())
        m.insert(u"recurrence"_s, task.recurrence);
    return m;
}

QVariantMap queryItemMap(const QueryItem &item, const VaultIndex &index)
{
    const ParsedNote *note = index.note(item.path);
    QVariantMap m{{u"path"_s, item.path},
                  {u"title"_s, note ? note->title : paths::pageTitle(item.path)},
                  {u"folder"_s, paths::dirname(item.path)},
                  {u"line"_s, 0},
                  {u"depth"_s, 0}};
    if (note && item.blockIndex >= 0 && item.blockIndex < note->blocks.size()) {
        const Block &block = note->blocks.at(item.blockIndex);
        m.insert(u"line"_s, block.line);
        m.insert(u"text"_s, block.text);
        m.insert(u"raw"_s, block.raw);
        m.insert(u"depth"_s, block.depth);
        if (!block.id.isEmpty())
            m.insert(u"blockId"_s, block.id);
    } else {
        m.insert(u"text"_s, note ? note->title : paths::pageTitle(item.path));
        m.insert(u"raw"_s, QString());
    }
    if (item.task) {
        m.insert(u"task"_s, taskMap(*item.task));
        if (item.blockIndex < 0)
            m.insert(u"line"_s, item.task->line);
    }
    if (item.reminder) {
        QVariantMap r{{u"at"_s, item.reminder->at.toString()}, {u"alarm"_s, item.reminder->alarm}};
        if (!item.reminder->recurrence.isEmpty())
            r.insert(u"recurrence"_s, item.reminder->recurrence);
        m.insert(u"reminder"_s, r);
        m.insert(u"line"_s, item.reminder->line);
    }
    return m;
}

QString queryTypeName(QueryType type)
{
    switch (type) {
    case QueryType::Tasks: return u"tasks"_s;
    case QueryType::Blocks: return u"blocks"_s;
    case QueryType::Pages: return u"pages"_s;
    case QueryType::Reminders: return u"reminders"_s;
    }
    return {};
}

} // namespace

FakeEditorHost::FakeEditorHost(QObject *parent) : EditorHost(parent) {}

FakeEditorHost::~FakeEditorHost() = default;

void FakeEditorHost::filesChanged(bool announce)
{
    m_indexDirty = true;
    if (!announce || m_indexSignalPending)
        return;
    m_indexSignalPending = true;
    QTimer::singleShot(0, this, [this] {
        m_indexSignalPending = false;
        Q_EMIT indexChanged();
    });
}

void FakeEditorHost::setNow(const QString &now)
{
    if (m_now == now)
        return;
    m_now = now;
    Q_EMIT nowChanged();
}

QVariantMap FakeEditorHost::runQuery(const QString &source, const QString &currentPath)
{
    ++m_queryRuns;
    QVariantMap out{{u"ok"_s, false},        {u"error"_s, QString()},      {u"type"_s, u"blocks"_s},
                    {u"total"_s, 0},         {u"items"_s, QVariantList()}, {u"groups"_s, QVariantList()},
                    {u"complete"_s, true}};
    if (m_queriesFail) {
        out.insert(u"error"_s, u"No notebook is open."_s);
        return out;
    }
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *error = std::get_if<QString>(&parsed)) {
        out.insert(u"error"_s, *error);
        return out;
    }
    if (!m_index || m_indexDirty) {
        m_index = std::make_unique<VaultIndex>(vaultConfigFromMap(vaultConfig()));
        VaultIndex::Batch batch(*m_index);
        for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it)
            m_index->update(it.key(), it.value());
        for (auto it = m_assets.constBegin(); it != m_assets.constEnd(); ++it)
            m_index->update(it.key(), QString());
        m_indexDirty = false;
    }
    QDateTime current = QDateTime::currentDateTime();
    if (const auto spec = dates::parseDateSpec(m_now))
        current = spec->toLocalDateTime();
    QueryOptions options;
    options.currentPath = paths::normalize(currentPath);
    options.now = current.time();
    const QueryResult result = brulion::runQuery(std::get<Query>(parsed), *m_index, current.date(), Qt::Monday, options);
    QVariantList items;
    for (const QueryItem &item : result.items)
        items.append(queryItemMap(item, *m_index));
    QVariantList groups;
    for (const QueryGroup &group : result.groups) {
        QVariantList groupItems;
        for (const QueryItem &item : group.items)
            groupItems.append(queryItemMap(item, *m_index));
        groups.append(QVariantMap{{u"key"_s, group.key}, {u"label"_s, group.label}, {u"items"_s, groupItems}});
    }
    out.insert(u"ok"_s, true);
    out.insert(u"type"_s, queryTypeName(result.type));
    out.insert(u"total"_s, result.total);
    out.insert(u"items"_s, items);
    out.insert(u"groups"_s, groups);
    return out;
}

bool FakeEditorHost::setTaskStatus(const QString &path, int line, const QString &status)
{
    const std::optional<TaskStatus> wanted = taskStatusFromName(status);
    const auto it = m_files.find(path);
    if ((!wanted && !status.isEmpty()) || line < 0 || it == m_files.end())
        return false;
    QDate today = QDate::currentDate();
    if (const auto spec = dates::parseDateSpec(m_now))
        today = spec->date;
    const QString edited =
            wanted ? markdown::setTaskStatus(*it, line, *wanted, today) : markdown::removeTaskMarker(*it, line);
    if (edited == *it)
        return false;
    *it = edited;
    ++m_taskEdits;
    Q_EMIT taskEdited(path, line);
    Q_EMIT fileChanged(path);
    filesChanged();
    return true;
}

QString FakeEditorHost::read(const QString &path, bool *ok)
{
    const auto it = m_files.constFind(path);
    if (ok)
        *ok = it != m_files.constEnd();
    return it != m_files.constEnd() ? *it : QString();
}

bool FakeEditorHost::write(const QString &path, const QString &content)
{
    if (m_failWrites)
        return false;
    m_files.insert(path, content);
    ++m_writeCount;
    Q_EMIT written(path);
    filesChanged();
    return true;
}

EditorHost::WriteResult FakeEditorHost::writeIfUnchanged(const QString &path, const QString &content,
                                                         const QString &expected)
{
    const auto it = m_files.constFind(path);
    if (it != m_files.constEnd() && *it != expected) {
        ++m_casRefusals;
        return WriteResult::Changed;
    }
    return write(path, content) ? WriteResult::Written : WriteResult::Failed;
}

QVariantList FakeEditorHost::suggestPages(const QString &prefix, int limit) const
{
    const QString key = text::foldKey(prefix.trimmed());
    struct Hit {
        int rank;
        QVariantMap item;
    };
    QList<Hit> hits;
    QStringList paths = m_files.keys();
    std::sort(paths.begin(), paths.end());
    for (const QString &path : std::as_const(paths)) {
        if (!paths::isMarkdown(path) || path.startsWith(u"templates/"_s))
            continue;
        const QString title = paths::pageTitle(path);
        const QString folded = text::foldKey(title);
        const int rank = key.isEmpty() ? 1 : folded.startsWith(key) ? 0 : folded.contains(key) ? 1 : -1;
        if (rank >= 0)
            hits.append({rank, {{u"path"_s, path}, {u"title"_s, title}, {u"folder"_s, paths::dirname(path)}}});
        const markdown::FrontmatterResult fm = markdown::parseFrontmatter(m_files.value(path));
        for (const QString &alias : markdown::namesFrom(markdown::frontmatterGet(fm.data, {u"aliases"_s, u"alias"_s}), false)) {
            const QString fa = text::foldKey(alias);
            if (!key.isEmpty() && fa.contains(key))
                hits.append({fa.startsWith(key) ? 0 : 1,
                             {{u"path"_s, path}, {u"title"_s, title}, {u"alias"_s, alias}, {u"folder"_s, paths::dirname(path)}}});
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.rank < b.rank; });
    QVariantList out;
    for (const Hit &h : std::as_const(hits)) {
        if (out.size() >= limit)
            break;
        out.append(h.item);
    }
    return out;
}

QVariantList FakeEditorHost::suggestBlocks(const QString &query, int limit) const
{
    const QString key = text::foldKey(query.trimmed());
    QVariantList out;
    QStringList paths = m_files.keys();
    std::sort(paths.begin(), paths.end());
    for (const QString &path : std::as_const(paths)) {
        if (!paths::isMarkdown(path))
            continue;
        const ParsedNote note = markdown::parseNote(path, m_files.value(path));
        for (const Block &b : note.blocks) {
            if (b.kind == BlockKind::Frontmatter || b.text.trimmed().isEmpty() || b.kind == BlockKind::Code)
                continue;
            if (!key.isEmpty() && !text::foldKey(b.text).contains(key))
                continue;
            out.append(QVariantMap{{u"path"_s, path}, {u"line"_s, b.line}, {u"text"_s, b.text.simplified()}, {u"blockId"_s, b.id}});
            if (out.size() >= limit)
                return out;
        }
    }
    return out;
}

QVariantList FakeEditorHost::suggestTags(const QString &prefix, int limit) const
{
    QHash<QString, int> counts;
    QHash<QString, QString> spelling;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (!paths::isMarkdown(it.key()))
            continue;
        const ParsedNote note = markdown::parseNote(it.key(), it.value());
        for (const Block &b : note.blocks) {
            for (const QString &t : b.tags) {
                counts[t.toLower()] += 1;
                spelling.insert(t.toLower(), t);
            }
        }
        for (const QString &t : note.tags) {
            if (!counts.contains(t.toLower())) {
                counts[t.toLower()] = 1;
                spelling.insert(t.toLower(), t);
            }
        }
    }
    const QString key = text::foldKey(prefix.trimmed());
    QList<std::pair<QString, int>> tags;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (key.isEmpty() || text::foldKey(it.key()).contains(key))
            tags.append({spelling.value(it.key()), it.value()});
    }
    std::sort(tags.begin(), tags.end(), [&key](const auto &a, const auto &b) {
        const bool pa = text::foldKey(a.first).startsWith(key);
        const bool pb = text::foldKey(b.first).startsWith(key);
        if (pa != pb)
            return pa;
        if (a.second != b.second)
            return a.second > b.second;
        return a.first < b.first;
    });
    // Tags (with the page they name), then pages no tag names yet (docs/SYNTAX.md §3), one entry
    // per tag key (case-insensitive, not diacritic-insensitive: #zolw is not Żółw).
    const auto tagKey = [](const QString &name) { return name.normalized(QString::NormalizationForm_C).toLower(); };
    QVariantList out;
    QSet<QString> seen;
    for (const auto &[tag, count] : std::as_const(tags)) {
        if (out.size() >= limit)
            break;
        QVariantMap m{{u"tag"_s, tag}, {u"count"_s, count}, {u"kind"_s, u"tag"_s}};
        const QString page = resolveLink(tag, QString());
        if (paths::isMarkdown(page)) {
            m.insert(u"path"_s, page);
            m.insert(u"title"_s, paths::pageTitle(page));
            m.insert(u"folder"_s, paths::dirname(page));
        }
        seen.insert(tagKey(tag));
        out.append(m);
    }
    for (const QVariant &v : suggestPages(prefix, limit)) {
        if (out.size() >= limit)
            break;
        const QVariantMap p = v.toMap();
        const QString name = p.value(u"alias"_s).toString().isEmpty() ? p.value(u"title"_s).toString()
                                                                      : p.value(u"alias"_s).toString();
        if (seen.contains(tagKey(name)) || markdown::tagSyntax(name).isEmpty())
            continue;
        seen.insert(tagKey(name));
        out.append(QVariantMap{{u"tag"_s, name},
                               {u"count"_s, 0},
                               {u"kind"_s, u"page"_s},
                               {u"path"_s, p.value(u"path"_s)},
                               {u"title"_s, p.value(u"title"_s)},
                               {u"folder"_s, p.value(u"folder"_s)}});
    }
    return out;
}

QString FakeEditorHost::resolveLink(const QString &target, const QString &fromPath) const
{
    Q_UNUSED(fromPath);
    const QString t = paths::normalize(target);
    if (t.isEmpty())
        return {};
    // Exact path (with or without .md), case-insensitive; assets too.
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it.key().compare(t, Qt::CaseInsensitive) == 0 || it.key().compare(t + u".md"_s, Qt::CaseInsensitive) == 0)
            return it.key();
    }
    for (auto it = m_assets.constBegin(); it != m_assets.constEnd(); ++it) {
        if (it.key().compare(t, Qt::CaseInsensitive) == 0 || paths::basename(it.key()).compare(t, Qt::CaseInsensitive) == 0)
            return it.key();
    }
    // Unique base name; then aliases.
    QStringList keys = m_files.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    for (const QString &path : std::as_const(keys)) {
        if (paths::isMarkdown(path) && paths::pageTitle(path).compare(t, Qt::CaseInsensitive) == 0)
            return path;
    }
    for (const QString &path : std::as_const(keys)) {
        if (!paths::isMarkdown(path))
            continue;
        const markdown::FrontmatterResult fm = markdown::parseFrontmatter(m_files.value(path));
        for (const QString &alias : markdown::namesFrom(markdown::frontmatterGet(fm.data, {u"aliases"_s, u"alias"_s}), false)) {
            if (alias.compare(t, Qt::CaseInsensitive) == 0)
                return path;
        }
    }
    return {};
}

QString FakeEditorHost::blockText(const QString &blockId) const
{
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (!paths::isMarkdown(it.key()) || !it.value().contains(blockId))
            continue;
        const ParsedNote note = markdown::parseNote(it.key(), it.value());
        const auto found = note.blockIds.constFind(blockId);
        if (found != note.blockIds.constEnd())
            return note.blocks.at(*found).text.simplified();
    }
    return {};
}

QString FakeEditorHost::linkedBlockText(const QString &path, const QString &blockId) const
{
    const auto it = m_files.constFind(path);
    if (it == m_files.constEnd() || !paths::isMarkdown(path))
        return {};
    const ParsedNote note = markdown::parseNote(path, *it);
    if (const auto found = note.blockIds.constFind(blockId); found != note.blockIds.constEnd())
        return note.blocks.at(*found).text.simplified();
    return blockText(blockId); // moved to another page
}

bool FakeEditorHost::blockIdTaken(const QString &blockId) const
{
    if (blockId.isEmpty())
        return false;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (paths::isMarkdown(it.key()) && it.value().contains(blockId)
            && markdown::parseNote(it.key(), it.value()).blockIds.contains(blockId))
            return true;
    }
    return false;
}

QString FakeEditorHost::ensureBlockId(const QString &path, int line)
{
    const auto it = m_files.constFind(path);
    if (it == m_files.constEnd())
        return {};
    const markdown::EnsuredBlockId res =
            markdown::ensureBlockId(*it, line, [this](const QString &id) { return blockIdTaken(id); });
    if (!res.id.isEmpty() && res.content != *it) {
        m_files.insert(path, res.content);
        m_indexDirty = true;
        Q_EMIT fileChanged(path);
        Q_EMIT indexChanged();
    }
    return res.id;
}

QVariantList FakeEditorHost::templates() const
{
    QVariantList out;
    QStringList keys = m_files.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &path : std::as_const(keys)) {
        if (path.startsWith(u"templates/"_s) && paths::isMarkdown(path))
            out.append(QVariantMap{{u"path"_s, path}, {u"title"_s, paths::pageTitle(path)}});
    }
    return out;
}

QUrl FakeEditorHost::assetUrl(const QString &path) const
{
    const auto cached = m_assetUrls.constFind(path);
    if (cached != m_assetUrls.constEnd())
        return *cached;
    const auto it = m_assets.constFind(path);
    if (it == m_assets.constEnd())
        return {};
    if (!m_assetDir)
        m_assetDir = std::make_unique<QTemporaryDir>();
    if (!m_assetDir->isValid())
        return {};
    const QString file = m_assetDir->filePath(QString::number(m_assetUrls.size()) + u'-' + paths::basename(path));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(*it) != it->size())
        return {};
    f.close();
    const QUrl url = QUrl::fromLocalFile(file);
    m_assetUrls.insert(path, url);
    return url;
}

bool FakeEditorHost::canDecodePicture(const QString &path) const
{
    return !m_undecodable.contains(pictureFormatName(path), Qt::CaseInsensitive) && EditorHost::canDecodePicture(path);
}

QString FakeEditorHost::saveAttachment(const QString &fileName, const QByteArray &data)
{
    const QString path = paths::uniquePath(u"assets/"_s + paths::sanitizeFileName(fileName),
                                           [this](const QString &p) { return m_assets.contains(p); });
    m_assets.insert(path, data);
    m_assetUrls.remove(path);
    filesChanged(false);
    return path;
}

QVariantMap FakeEditorHost::vaultConfig() const
{
    return {{u"journalsFolder"_s, u"journals"_s},   {u"journalFileFormat"_s, u"yyyy-MM-dd"_s},
            {u"attachmentsFolder"_s, u"assets"_s},  {u"templatesFolder"_s, u"templates"_s},
            {u"defaultReminderTime"_s, u"09:00"_s}, {u"weekStartsOn"_s, 1},
            {u"journalMode"_s, m_journalMode},      {u"pageMode"_s, m_pageMode}};
}

void FakeEditorHost::setPageMode(const QString &mode)
{
    if (m_pageMode == mode)
        return;
    m_pageMode = mode;
    Q_EMIT configChanged();
}

void FakeEditorHost::setJournalMode(const QString &mode)
{
    if (m_journalMode == mode)
        return;
    m_journalMode = mode;
    Q_EMIT configChanged();
}

void FakeEditorHost::navigate(const QVariantMap &route, bool newPane)
{
    m_lastRoute = route;
    Q_EMIT navigated(route, newPane);
}

void FakeEditorHost::openUrl(const QUrl &url)
{
    Q_EMIT urlOpened(url);
}

QString FakeEditorHost::renderHtml(const QString &markdown, const QString &fromPath) const
{
    Q_UNUSED(fromPath);
    return u"<pre>"_s + markdown.toHtmlEscaped() + u"</pre>"_s;
}

QString FakeEditorHost::htmlToMarkdown(const QString &html) const
{
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toMarkdown(QTextDocument::MarkdownDialectGitHub).trimmed();
}

void FakeEditorHost::setFile(const QString &path, const QString &content)
{
    m_files.insert(path, content);
    filesChanged(false);
}

void FakeEditorHost::setTextAsset(const QString &path, const QString &content)
{
    m_assets.insert(path, content.toUtf8());
    m_assetUrls.remove(path);
    filesChanged(false);
}

void FakeEditorHost::overwriteTextAsset(const QString &path, const QString &content)
{
    const QByteArray data = content.toUtf8();
    m_assets.insert(path, data);
    const auto url = m_assetUrls.constFind(path);
    if (url != m_assetUrls.constEnd()) {
        QFile f(url->toLocalFile());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size())
            m_assetUrls.remove(path);
    }
    filesChanged(); // the editors render their embeds again (indexChanged)
}

void FakeEditorHost::externalWrite(const QString &path, const QString &content)
{
    m_files.insert(path, content);
    Q_EMIT fileChanged(path);
    filesChanged();
}

void FakeEditorHost::removeFile(const QString &path)
{
    if (m_files.remove(path)) {
        Q_EMIT fileChanged(path);
        filesChanged();
    }
}

QString FakeEditorHost::file(const QString &path) const
{
    return m_files.value(path);
}

QStringList FakeEditorHost::files() const
{
    QStringList keys = m_files.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

QString FakeEditorHost::samplePath() const
{
    return u"Projekty/Remont łazienki.md"_s;
}

QString FakeEditorHost::sampleNote()
{
    return uR"(---
tags: [projekt, dom]
status: w toku
budżet: 12000
aliases: [Łazienka]
---

Plan remontu na październik. Szczegóły w [[Budżet domowy]] i [[Kontakty|kontaktach]], inspiracje na [[Pinterest]]. #projekt

- Przygotowanie ^przyg
	- [x] Zmierzyć ściany i podłogę ✅ 2026-10-12
	- [ ] Kupić płytki 📅 2026-10-16 ⏫ #zakupy
		- Wzór: heksagon, kolor **piaskowy**, matowe
		- Sklep: [[Castorama]] albo https://example.com/plytki
	- [/] Zadzwonić do hydraulika ⏰ 2026-10-15 18:00 #telefon
	- [ ] Wywieźć gruz ⏳ 2026-10-18
- Prace
	- [ ] Skuć stare płytki 📅 2026-10-13
	- [ ] Położyć hydroizolację
	- [ ] Podlać kwiaty u sąsiadów ⏰! 2026-10-16 07:30 🔁 every week on Friday
		- Klucz w skrzynce, kod `4711`
	- Kolejność: *najpierw* hydraulik, ~~potem~~ elektryk
- Notatki z rozmowy z ekipą — zobacz ((przyg))
	- Ekipa może zacząć 20 października, cena ==do negocjacji==

## Koszty

```calc
powierzchnia = 6,5
cena_m2 = 89 zł

płytki = powierzchnia * cena_m2
klej = 4 * 45 zł
robocizna = 2800 zł
suma
```

Zostaje z budżetu: `= budżet - płytki - klej - robocizna`

| Pozycja | Ilość | Cena | Razem |
| --- | ---: | ---: | ---: |
| **Płytki** z [[Castorama]] | 6,5 | 89 | =[Ilość]*[Cena] |
| Klej *elastyczny* | 4 | 45 | =B2*C2 |
| Fuga #zakupy | 2 | 38 | =[Ilość]*[Cena] |
| Suma |  |  | =sum(above) |

> [!tip] Wskazówka
> Do płytek dolicz **10%** zapasu na docinki i stłuczki.

```query
type:tasks status:open tag:zakupy sort:due
```

## Powiązane

- Budżet na październik
	- ![[Budżet domowy#Październik]]
- Hydraulik z kontaktów: !((hydr))

![[łazienka-szkic.png]]

---

> Dobrze zaplanowany remont to połowa remontu.
)"_s;
}

QString FakeEditorHost::documentPath() const
{
    return u"Podróże/Wyjazd w Tatry.md"_s;
}

QString FakeEditorHost::documentNote()
{
    return uR"(---
tags: [podróże]
brulion: document
---
# Wyjazd w Tatry

Trzy dni w Zakopanem pod koniec października: pociąg w piątek rano, powrót w niedzielę wieczorem. Budżet liczymy w [[Budżet domowy]]. #podróże

## Plan

Pierwszego dnia spokojnie — Krupówki, Gubałówka i kolacja w góralskiej karczmie. Drugiego dnia **Morskie Oko**: wyjście o świcie, zanim na szlaku zrobi się tłoczno.

- Piątek: Gubałówka i Krupówki
- Sobota: Morskie Oko
	- Wejście od Palenicy Białczańskiej, ok. 2,5 h
	- Z powrotem bryczką, jeśli zabraknie sił
- Niedziela: termy w Bukowinie

## Do spakowania

- [x] Buty trekkingowe ✅ 2026-10-12
- [ ] Kurtka przeciwdeszczowa
- [ ] Termos i czołówka 📅 2026-10-22

## Koszty

1. Pociąg: 2 × 89 zł
2. Nocleg: dwie noce po 240 zł
3. Bilety do parku: 2 × 9 zł

Razem wychodzi `= 2 * 89 + 2 * 240 + 2 * 9` zł na dwie osoby.

> [!tip] Pogoda
> W październiku w Tatrach bywa już śnieg — sprawdź prognozę dzień przed wyjazdem.
)"_s;
}

QString FakeEditorHost::recipePath() const
{
    return u"Przepisy/Pierogi ruskie.md"_s;
}

QString FakeEditorHost::recipeNote()
{
    return uR"(---
tags: [przepis, obiad]
porcje: 4
czas: 90 min
---
# Pierogi ruskie

Babcine pierogi z ziemniakami i twarogiem — najlepsze z **podsmażoną cebulką**. #przepis

## Składniki

- 1 kg ziemniaków
- 500 g twarogu półtłustego
- 2 duże cebule
- [ ] Kupić twaróg i mąkę #zakupy

## Przygotowanie

1. Ugotować ziemniaki i przecisnąć przez praskę.
2. Zeszklić cebulę, wymieszać z twarogiem; doprawić pieprzem.
3. Lepić, gotować 3 minuty od wypłynięcia.
)"_s;
}

void FakeEditorHost::populateSample()
{
    m_files.clear();
    m_assets.clear();
    m_assetUrls.clear();
    setFile(samplePath(), sampleNote());
    setFile(u"Budżet domowy.md"_s,
            u"# Budżet domowy\n\n## Październik\n\n- Remont: **12 000 zł** #finanse ^bud01\n- Jedzenie: 1 800 zł\n"
            u"- [ ] Opłacić czynsz 📅 2026-10-20 #finanse\n\n## Listopad\n\n- Prezenty: 400 zł\n"_s);
    setFile(u"Kontakty.md"_s,
            u"---\naliases: [Telefony]\n---\n- Hydraulik: pan Marek, 600 100 200 #telefon ^hydr\n- Elektryk: pani Ewa #telefon\n"_s);
    setFile(u"Projekty/Ogród.md"_s, u"- [ ] Przyciąć żywopłot #ogród\n- Pomysły na wiosnę #pomysły\n"_s);
    setFile(u"Projekty/Remont kuchni.md"_s, u"- Zrobione w 2025 #dom\n"_s);
    setFile(u"Zakupy.md"_s, u"- [ ] Mleko #zakupy\n- [ ] Śrubki do półki #zakupy\n- [ ] Żarówki LED #zakupy\n"_s);
    setFile(u"Praca/Spotkania.md"_s, u"- Spotkanie zespołu w poniedziałek #praca\n"_s);
    setFile(u"journals/2026-10-15.md"_s,
            u"- Rano: przegląd zadań\n- Zadzwonić do mamy ⏰ 2026-10-15 19:00 #rodzina\n"_s);
    setFile(u"journals/2026-10-14.md"_s, u"- Odebrać paczkę #zakupy\n"_s);
    setFile(u"templates/Spotkanie.md"_s,
            u"- Spotkanie {{date}} — {{title}}\n\t- Uczestnicy:: \n\t- Ustalenia\n\t\t- {{cursor}}\n\t- [ ] Wysłać notatkę\n"_s);
    setFile(u"templates/Zakupy tygodniowe.md"_s, u"- [ ] Pieczywo\n- [ ] Warzywa\n- [ ] Owoce\n"_s);
    setFile(recipePath(), recipeNote());
    setFile(documentPath(), documentNote());
    m_assets.insert(u"assets/łazienka-szkic.png"_s, sampleImagePng());
    m_indexDirty = true;
    Q_EMIT indexChanged();
}

} // namespace brulion::editor
