#include "codefileindex.h"

#include "codefilesbackground_p.h"

#include "codefiles/viewer.h"
#include "gitcli/client.h"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

namespace {

QString cleanFolder(const QString &folder)
{
    return folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath());
}

bool &alwaysSync()
{
    static bool on = false;
    return on;
}

/// `text` escaped for StyledText, the characters at `positions` (offsets in `text`) bold in `mark`.
QString markup(QStringView text, const QList<int> &positions, int offset, const QString &mark)
{
    QString out;
    out.reserve(text.size() + positions.size() * 32);
    qsizetype p = 0;
    bool open = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        while (p < positions.size() && positions.at(p) - offset < i)
            ++p;
        const bool marked = p < positions.size() && positions.at(p) - offset == i;
        if (marked && !open) {
            out += u"<b><font color=\""_s + mark + u"\">"_s;
            open = true;
        } else if (!marked && open) {
            out += u"</font></b>"_s;
            open = false;
        }
        const QChar c = text.at(i);
        if (c == u'<')
            out += u"&lt;"_s;
        else if (c == u'>')
            out += u"&gt;"_s;
        else if (c == u'&')
            out += u"&amp;"_s;
        else if (c == u'"')
            out += u"&quot;"_s;
        else
            out += c;
    }
    if (open)
        out += u"</font></b>"_s;
    return out;
}

/// Pool threads stop walking once the cache is gone.
std::shared_ptr<std::atomic<bool>> &stopFlag()
{
    static auto flag = std::make_shared<std::atomic<bool>>(false);
    return flag;
}

} // namespace

QString fileIcon(QStringView path, bool folder)
{
    if (folder)
        return u"folder"_s;
    if (isPictureName(path))
        return u"image"_s;
    const QString language = languageForPath(path);
    if (language.isEmpty() || language == u"text" || language == u"markdown" || language == u"csv")
        return u"file-text"_s;
    return u"code"_s;
}

QVariantList fileRows(const FileIndex &index, const QList<FileIndex::Hit> &hits, const QColor &mark)
{
    QVariantList rows;
    rows.reserve(hits.size());
    const QString color = mark.name(QColor::HexRgb);
    for (const FileIndex::Hit &h : hits) {
        const QStringView path = index.path(h.entry);
        const int nameStart = index.nameStart(h.entry);
        const bool folder = index.isFolder(h.entry);
        const QStringView name = path.sliced(nameStart);
        const QStringView parent = nameStart > 0 ? path.first(nameStart - 1) : QStringView();
        rows.append(QVariantMap{{u"path"_s, path.toString()},
                                {u"name"_s, name.toString()},
                                {u"folder"_s, parent.toString()},
                                {u"isFolder"_s, folder},
                                {u"title"_s, folder ? path.toString() + u'/' : path.toString()},
                                {u"nameMarkup"_s, markup(name, h.positions, nameStart, color)},
                                {u"folderMarkup"_s, markup(parent, h.positions, 0, color)},
                                {u"iconName"_s, fileIcon(path, folder)}});
    }
    return rows;
}

// ---- FileIndexCache ---------------------------------------------------------------------------------

FileIndexCache *FileIndexCache::instance()
{
    static QPointer<FileIndexCache> cache;
    if (!cache)
        cache = new FileIndexCache(QCoreApplication::instance());
    return cache;
}

FileIndexCache::FileIndexCache(QObject *parent) : QObject(parent), m_git(new gitcli::Client(this))
{
    stopFlag()->store(false);
}

FileIndexCache::~FileIndexCache()
{
    stopFlag()->store(true);
    stopFlag() = std::make_shared<std::atomic<bool>>(false);
}

void FileIndexCache::setGitProgram(const QString &program)
{
    m_git->setGitProgram(program);
}

std::shared_ptr<const FileIndex> FileIndexCache::index(const QString &folder)
{
    const QString key = cleanFolder(folder);
    if (key.isEmpty())
        return nullptr;
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        it = m_slots.insert(key, Slot{});
        it->used = ++m_clock;
        build(key);
        trim();
        it = m_slots.find(key);
        return it == m_slots.end() ? nullptr : it->index;
    }
    it->used = ++m_clock;
    return it->index;
}

bool FileIndexCache::building(const QString &folder) const
{
    const auto it = m_slots.constFind(cleanFolder(folder));
    return it != m_slots.cend() && it->building;
}

QString FileIndexCache::source(const QString &folder) const
{
    const auto it = m_slots.constFind(cleanFolder(folder));
    return it == m_slots.cend() ? QString() : it->source;
}

void FileIndexCache::refresh(const QString &folder)
{
    const QString key = cleanFolder(folder);
    if (key.isEmpty())
        return;
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        index(key);
        return;
    }
    if (it->building) {
        it->again = true;
        return;
    }
    build(key);
}

void FileIndexCache::build(const QString &folder)
{
    auto it = m_slots.find(folder);
    if (it == m_slots.end())
        return;
    it->building = true;
    it->again = false;
    const int generation = ++it->generation;
    Q_EMIT updated(folder);
    const std::shared_ptr<std::atomic<bool>> stop = stopFlag();
    const auto walk = [this, folder, generation, stop] {
        detail::onPool<std::shared_ptr<const FileIndex>>(
                this,
                [folder, stop] {
                    return std::make_shared<const FileIndex>(
                            FileIndex::walk(folder, FileIndex::DefaultMaxEntries, stop.get()));
                },
                [this, folder, generation](std::shared_ptr<const FileIndex> index) {
                    finish(folder, generation, std::move(index), u"folder"_s);
                });
    };
    if (m_git->gitProgram().isEmpty() || !gitcli::Client::processesAvailable()) {
        walk();
        return;
    }
    gitcli::RunOptions options;
    options.readOnly = true;
    options.maxOutput = MaxGitOutput;
    m_git->git(folder, {u"ls-files"_s, u"-co"_s, u"--exclude-standard"_s, u"-z"_s}, options,
               [this, folder, generation, walk](const gitcli::RunResult &r) {
                   // (Cut at the limit: what came is more than an index keeps anyway.)
                   if (!r.ok() && !r.outputCut) {
                       walk();
                       return;
                   }
                   QByteArray out = r.out;
                   if (r.outputCut)
                       out.truncate(out.lastIndexOf('\0') + 1);
                   detail::onPool<std::shared_ptr<const FileIndex>>(
                           this,
                           [out] { return std::make_shared<const FileIndex>(FileIndex::fromNulSeparated(out)); },
                           [this, folder, generation](std::shared_ptr<const FileIndex> index) {
                               finish(folder, generation, std::move(index), u"git"_s);
                           });
               });
}

void FileIndexCache::finish(const QString &folder, int generation, std::shared_ptr<const FileIndex> index,
                            const QString &source)
{
    auto it = m_slots.find(folder);
    if (it == m_slots.end() || it->generation != generation)
        return;
    it->index = std::move(index);
    it->source = source;
    it->building = false;
    if (it->again)
        build(folder);
    Q_EMIT updated(folder);
}

void FileIndexCache::trim()
{
    while (m_slots.size() > MaxFolders) {
        auto oldest = m_slots.end();
        for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
            if (oldest == m_slots.end() || it->used < oldest->used)
                oldest = it;
        }
        if (oldest == m_slots.end())
            return;
        m_slots.erase(oldest);
    }
}

// ---- CodeFileIndex ----------------------------------------------------------------------------------

void CodeFileIndex::setAlwaysSync(bool on)
{
    alwaysSync() = on;
}

CodeFileIndex::CodeFileIndex(QObject *parent) : QObject(parent)
{
    m_liveTimer.setInterval(15000);
    connect(&m_liveTimer, &QTimer::timeout, this, &CodeFileIndex::refresh);
    connect(FileIndexCache::instance(), &FileIndexCache::updated, this, [this](const QString &folder) {
        if (folder != m_folder)
            return;
        const std::shared_ptr<const FileIndex> next = FileIndexCache::instance()->index(m_folder);
        const bool changed = next != m_index;
        m_index = next;
        Q_EMIT stateChanged();
        if (changed)
            update();
    });
}

CodeFileIndex::~CodeFileIndex() = default;

void CodeFileIndex::setFolder(const QString &folder)
{
    const QString clean = cleanFolder(folder);
    if (clean == m_folder)
        return;
    m_folder = clean;
    Q_EMIT folderChanged();
    attach();
}

void CodeFileIndex::attach()
{
    m_index = m_folder.isEmpty() || !m_active ? nullptr : FileIndexCache::instance()->index(m_folder);
    Q_EMIT stateChanged();
    update();
}

void CodeFileIndex::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    Q_EMIT queryChanged();
    update();
}

void CodeFileIndex::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    // (Inactive: the index is not even asked for; active again: it is.)
    if (m_active && !m_index && !m_folder.isEmpty())
        attach();
    else
        update();
}

void CodeFileIndex::setKinds(const QString &kinds)
{
    if (kinds == m_kinds)
        return;
    m_kinds = kinds;
    Q_EMIT kindsChanged();
    update();
}

void CodeFileIndex::setLimit(int limit)
{
    limit = std::clamp(limit, 0, 1000);
    if (limit == m_limit)
        return;
    m_limit = limit;
    Q_EMIT limitChanged();
    update();
}

void CodeFileIndex::setLive(bool live)
{
    if (live == m_live)
        return;
    m_live = live;
    if (m_live) {
        m_liveTimer.start();
        refresh();
    } else {
        m_liveTimer.stop();
    }
    Q_EMIT liveChanged();
}

void CodeFileIndex::setMarkColor(const QColor &color)
{
    if (color == m_markColor)
        return;
    m_markColor = color;
    Q_EMIT markColorChanged();
    update();
}

void CodeFileIndex::setRecent(const QStringList &recent)
{
    if (recent == m_recent)
        return;
    m_recent = recent;
    Q_EMIT recentChanged();
    if (m_query.trimmed().isEmpty())
        update();
}

bool CodeFileIndex::building() const
{
    return !m_folder.isEmpty() && FileIndexCache::instance()->building(m_folder);
}

QString CodeFileIndex::source() const
{
    return m_folder.isEmpty() ? QString() : FileIndexCache::instance()->source(m_folder);
}

void CodeFileIndex::refresh()
{
    if (!m_folder.isEmpty())
        FileIndexCache::instance()->refresh(m_folder);
}

bool CodeFileIndex::contains(const QString &path) const
{
    return m_index && m_index->contains(cleanRelativePath(path));
}

bool CodeFileIndex::isFolder(const QString &path) const
{
    return m_index && m_index->isFolder(m_index->find(cleanRelativePath(path)));
}

QString CodeFileIndex::pathAt(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows.at(row).toMap().value(u"path"_s).toString() : QString();
}

void CodeFileIndex::setRows(const QVariantList &rows)
{
    if (rows == m_rows)
        return;
    m_rows = rows;
    Q_EMIT rowsChanged();
}

void CodeFileIndex::update()
{
    const int generation = ++m_generation;
    if (!m_active || !m_index || m_limit <= 0) {
        setRows({});
        return;
    }
    const FileIndex::Kinds kinds = m_kinds == u"files"     ? FileIndex::Kinds::Files
                                   : m_kinds == u"folders" ? FileIndex::Kinds::Folders
                                                           : FileIndex::Kinds::All;
    const std::shared_ptr<const FileIndex> index = m_index;
    const QString query = m_query;
    const int limit = m_limit;
    const QColor mark = m_markColor;
    const QStringList recent = m_recent;
    const auto compute = [index, query, limit, kinds, mark, recent] {
        if (!query.trimmed().isEmpty() || recent.isEmpty())
            return fileRows(*index, index->search(query, limit, kinds), mark);
        // An empty query: the recent files the index has, then the shallowest entries.
        QList<FileIndex::Hit> hits;
        QList<int> seen;
        for (const QString &p : recent) {
            const int e = index->find(cleanRelativePath(p));
            if (e < 0 || seen.contains(e) || (kinds == FileIndex::Kinds::Folders) != index->isFolder(e))
                continue;
            seen.append(e);
            hits.append({e, 1, {}});
            if (hits.size() >= limit)
                break;
        }
        for (const FileIndex::Hit &h : index->search(QString(), limit + int(seen.size()), kinds)) {
            if (hits.size() >= limit)
                break;
            if (!seen.contains(h.entry))
                hits.append(h);
        }
        return fileRows(*index, hits, mark);
    };
    if (alwaysSync() || index->size() <= SyncQueryLimit) {
        setRows(compute());
        return;
    }
    detail::onPool<QVariantList>(this, compute, [this, generation](QVariantList rows) {
        if (generation == m_generation)
            setRows(rows);
    });
}

} // namespace brulion::codefiles
