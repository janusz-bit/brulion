#include "workspace.h"

#include "indexworkers.h"
#include "picturesize.h"
#include "workspacereminders.h"
#include "workspacesync.h"

#include "dates/dates.h"
#include "drawing/excalidrawmd.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "log/slowscope.h"
#include "markdown/markdown.h"
#include "notebook/capture.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "notebook/pages.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#ifdef Q_OS_WASM
#include "wasm/wasmshell.h"
#include "wasm/wasmstorage.h"
#endif

#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLocale>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

Q_LOGGING_CATEGORY(lcWorkspace, "brulion.workspace")

namespace {

/// The bytes are UTF-8 text (a BOM allowed): a note Brulion may rewrite without losing bytes.
bool isUtf8Text(const QByteArray &bytes)
{
    const QByteArrayView body = bytes.startsWith("\xEF\xBB\xBF") ? QByteArrayView(bytes).sliced(3) : QByteArrayView(bytes);
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = utf8.decode(body); // decoding sets hasError()
    Q_UNUSED(decoded);
    return !utf8.hasError();
}

constexpr auto kFavorites = "ui/favorites";
constexpr auto kRecentPages = "ui/recentPages";

/// LocalStore key of a page's folded blocks.
QString foldStateKey(const QString &path)
{
    return u"ui/folds/"_s + path;
}

/// Files indexed per event-loop turn while building the index in the background.
constexpr qint64 kIndexBudgetMs = 12;

/// Interval of the repeating timer that runs the index chunks. In the browser it must not be 0:
/// Qt 6.11's single-threaded WebAssembly event dispatcher re-arms its browser timeout only after
/// it fired a timer with a non-zero interval, so a repeating 0 ms timer fires once and then holds
/// back every Qt timer until another timer starts or stops or input arrives — the index stood
/// still until the user moved the mouse (docs/WEB.md). Browsers make back-to-back timeouts wait
/// 4 ms anyway.
#ifdef Q_OS_WASM
constexpr int kIndexIntervalMs = 1;
#else
constexpr int kIndexIntervalMs = 0;
#endif

QVariantMap pageEntry(const QString &path)
{
    return { { u"path"_s, path }, { u"title"_s, paths::pageTitle(path) } };
}

QString lineOf(const QString &text)
{
    QString out = text.simplified();
    if (out.size() > 300)
        out = out.left(299) + u'…';
    return out;
}

} // namespace

Workspace::Workspace(QObject *parent) : EditorHost(parent)
{
#ifdef Q_OS_WASM
    m_dataUrlAssets = true;
    m_webPageScheme = platform::pageScheme();
#endif
    m_indexTimer.setInterval(kIndexIntervalMs);
    connect(&m_indexTimer, &QTimer::timeout, this, &Workspace::indexChunk);
    m_impactTimer.setInterval(0);
    connect(&m_impactTimer, &QTimer::timeout, this, &Workspace::moveImpactStep);

    m_reminders = new WorkspaceReminders([this] { return now(); }, this);
    connect(m_reminders, &WorkspaceReminders::navigationRequested, this,
            [this](const QVariantMap &route) { navigate(route); });
    connect(m_reminders, &WorkspaceReminders::errorOccurred, this, &Workspace::errorOccurred);
    m_sync = new WorkspaceSync(this);
    m_sync->setClock([this] { return now(); });
    connect(m_sync, &WorkspaceSync::errorOccurred, this, &Workspace::errorOccurred);
    connect(m_sync, &WorkspaceSync::foldersChanged, this,
            [this](const QStringList &paths) { notifyFolderChange(paths, false); });
    // A folder the server renamed, renamed here by the sync: its pages followed already (Vault
    // changes); the tree and an open "All pages" of the folder follow it. Nothing is rewritten:
    // the device that renamed it updated the links, and those edits arrive as downloads.
    connect(m_sync, &WorkspaceSync::folderMovedHere, this, [this](const QString &from, const QString &to) {
        notifyFolderChange({from, to}, false);
        Q_EMIT folderRenamed(from, to);
    });
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        connect(app, &QGuiApplication::applicationStateChanged, this, &Workspace::applicationStateChanged);
}

Workspace::~Workspace()
{
    // Nobody may react to a workspace that is going away (the app's controller is half
    // destroyed by now): close quietly.
    m_sync->blockSignals(true);
    m_reminders->blockSignals(true);
    blockSignals(true);
    close();
}

bool Workspace::open(const QString &rootPath, const QString &deviceStatePath, QString *error)
{
    close();
    auto vault = std::make_unique<Vault>(rootPath, deviceStatePath);
    QString err;
    if (!vault->open(&err)) {
        if (error)
            *error = err;
        return false;
    }
    m_vault = std::move(vault);
    if (m_clock)
        m_vault->setClock(m_clock);
    m_index = std::make_unique<VaultIndex>(m_vault->config());
    connect(m_vault.get(), &Vault::changed, this, &Workspace::onVaultChanged);
    connect(m_vault.get(), &Vault::configChanged, this, [this] {
        m_index->setConfig(m_vault->config());
        if (m_loader)
            m_loader->configChanged();
        m_impacts = ImpactCache(); // the settings a move rebases are the new ones
        Q_EMIT configChanged();
    });
    connect(m_index.get(), &VaultIndex::changed, this, &EditorHost::indexChanged);
#ifndef Q_OS_WASM
    // Other programs (Syncthing, editors) change files: watch the folders (and the files open in
    // editors, see editorOpened()), rescan when the app comes back to the foreground
    // (applicationStateChanged()) and slowly in between, because watchers miss events now and
    // then (Android, network folders) and in-place rewrites of other files show in no folder.
    // In the browser nothing else writes to the vault (the HTML shell allows one tab), so none
    // of this is needed.
    for (auto it = m_editorFiles.cbegin(); it != m_editorFiles.cend(); ++it)
        m_vault->watchFile(it.key()); // editors that were open before this vault
    m_vault->startWatching();
    m_vault->setRescanInterval(120 * 1000);
#endif
    m_wasInactive = false;

    notebook::WorkerPool pool;
#ifdef BRULION_INDEX_THREADS
    if (!m_indexPool && indexWorkerCount() > 0)
        m_indexPool = new QThreadPool(this);
    pool = indexWorkerPool(static_cast<QThreadPool *>(m_indexPool));
#endif
    notebook::IndexLoader::Options loaderOptions;
    loaderOptions.firstFolder = m_vault->config().journalsFolder;
    // The index cache next to History and LocalStore. In the browser it goes to a cache mount of
    // its own (WasmStorage::indexCacheRoot(), its own IndexedDB database): a cache the quota
    // cannot take must not abort the write-back of History, sync state and settings with it.
    m_indexCachePath = deviceStatePath.isEmpty() || qEnvironmentVariableIsSet("BRULION_NO_INDEX_CACHE")
                               ? QString()
                               : QDir(deviceStatePath).filePath(u"index-cache.bin"_s);
#ifdef Q_OS_WASM
    if (!m_indexCachePath.isEmpty())
        m_indexCachePath = QDir(platform::WasmStorage::indexCacheRoot())
                                   .filePath(QFileInfo(deviceStatePath).fileName() + u"/index-cache.bin"_s);
#endif
    loaderOptions.cachePath = m_indexCachePath;
    m_loader = std::make_unique<notebook::IndexLoader>(*m_vault, *m_index, std::move(pool), loaderOptions);
    connect(m_loader.get(), &notebook::IndexLoader::resultsReady, this, &Workspace::onIndexResultsReady);
    m_indexClock.start();
    m_loader->start(m_vault->files());
    m_trashCount = -1;
    m_reminders->attach(m_vault.get(), m_index.get());
    m_sync->attach(m_vault.get());
    if (!m_loader->isDone())
        m_indexTimer.start();

    ++m_revision;
    Q_EMIT openChanged();
    Q_EMIT configChanged();
    Q_EMIT revisionChanged();
    Q_EMIT favoritesChanged();
    Q_EMIT recentPagesChanged();
    Q_EMIT indexingChanged();
    Q_EMIT indexProgressChanged();
    Q_EMIT trashChanged();
    if (!indexing())
        indexingFinished();
    return true;
}

void Workspace::close()
{
    if (!m_vault)
        return;
    const bool wasReady = ready();
    // Reminders and sync let go of the vault and the index first (a running sync is abandoned:
    // what it finished stays synced, the rest is synced next time).
    m_reminders->detach();
    m_sync->detach();
    m_indexTimer.stop();
    cancelMoveImpact(); // its planner reads the index
    m_impacts = ImpactCache();
    const bool wasIndexing = indexing();
    // A notebook closed before its first cache was written (the app quit right after indexing):
    // write it now, once, so the next start does not parse everything again.
    if (m_cacheKeeper && m_loader && !m_cacheKeeper->hasSaved() && !m_cacheKeeper->isSaving()
        && m_loader->stats().cache != u"used" && m_cacheKeeper->isDirty())
        m_cacheKeeper->saveNow();
    m_cacheKeeper.reset(); // before the loader, the index and the vault
    m_loader.reset();      // before the index and the vault (its workers stop after their file)
    m_trashCount = -1;
    m_dataUrls.clear();
    m_dataUrlSizes.clear();
    m_vault->stopWatching();
    disconnect(m_vault.get(), nullptr, this, nullptr);
    disconnect(m_index.get(), nullptr, this, nullptr);
    m_listCaches.reset();
    m_index.reset();
    m_vault.reset();
    ++m_revision;
    Q_EMIT openChanged();
    Q_EMIT configChanged();
    Q_EMIT revisionChanged();
    Q_EMIT favoritesChanged();
    Q_EMIT recentPagesChanged();
    Q_EMIT trashChanged();
    if (wasIndexing) {
        Q_EMIT indexingChanged();
        Q_EMIT indexProgressChanged();
    }
    setReady(wasReady);
}

bool Workspace::isOpen() const
{
    return m_vault != nullptr;
}

QString Workspace::rootPath() const
{
    return m_vault ? m_vault->rootPath() : QString();
}

QString Workspace::name() const
{
    return m_vault ? QFileInfo(m_vault->rootPath()).fileName() : QString();
}

bool Workspace::isSyncthingFolder() const
{
    return m_vault && QFileInfo::exists(m_vault->rootPath() + u"/.stfolder"_s);
}

void Workspace::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    // Trash stamps and version history follow the same clock (tests, screenshots).
    if (m_vault)
        m_vault->setClock(m_clock);
}

QDateTime Workspace::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void Workspace::setUiLanguage(const QString &language)
{
    m_language = language == u"en" ? u"en"_s : u"pl"_s;
    m_reminders->setLanguage(m_language);
    m_sync->setLanguage(m_language);
}

void Workspace::setDataUrlAssets(bool on)
{
    m_dataUrlAssets = on;
    m_dataUrls.clear();
    m_dataUrlSizes.clear();
}

// ---- Index ------------------------------------------------------------------------------------

void Workspace::indexFile(const QString &path)
{
    if (!m_vault || !m_index || paths::isIgnored(path))
        return;
    if (paths::isMarkdown(path)) {
        bool ok = false;
        const QString content = m_vault->readText(path, &ok);
        if (ok)
            m_index->update(path, content);
    } else {
        m_index->update(path, QString()); // attachments are remembered by path
    }
}

void Workspace::indexChunk()
{
    if (!m_index || !indexing()) {
        m_indexTimer.stop();
        return;
    }
    const notebook::IndexLoader::Step step = m_loader->step(int(kIndexBudgetMs));
    Q_EMIT indexProgressChanged();
    switch (step) {
    case notebook::IndexLoader::Step::More: break;
    case notebook::IndexLoader::Step::Waiting: m_indexTimer.stop(); break; // onIndexResultsReady()
    case notebook::IndexLoader::Step::Done:
        m_indexTimer.stop();
        Q_EMIT indexingChanged();
        indexingFinished();
        break;
    }
}

void Workspace::onIndexResultsReady()
{
    if (indexing() && !m_indexTimer.isActive())
        m_indexTimer.start();
}

void Workspace::finishIndexing()
{
    if (!m_index || !indexing())
        return;
    m_loader->finish();
    m_indexTimer.stop();
    Q_EMIT indexingChanged();
    Q_EMIT indexProgressChanged();
    indexingFinished();
}

notebook::IndexLoader::Stats Workspace::indexStats() const
{
    return m_loader ? m_loader->stats() : notebook::IndexLoader::Stats();
}

void Workspace::indexingFinished()
{
    if (m_indexClock.isValid()) {
        const notebook::IndexLoader::Stats stats = indexStats();
        const QString how = stats.workers > 0 ? u"parsed on %1 threads"_s.arg(stats.workers) : u"parsed in slices"_s;
        qCInfo(logging::lcIndex).noquote()
                << u"Index ready: %1 notes and %2 attachments in %3 (%4 from the cache, %5 %6)"_s
                           .arg(stats.total - stats.attachments)
                           .arg(stats.attachments)
                           .arg(logging::formatMs(m_indexClock.elapsed()))
                           .arg(stats.fromCache)
                           .arg(stats.parsed)
                           .arg(how);
        m_indexClock.invalidate();
    }
    // The index cache follows the index from now on (written soon when anything was parsed).
    if (!m_indexCachePath.isEmpty() && !m_cacheKeeper && m_loader) {
        notebook::WorkerPool pool;
#ifdef BRULION_INDEX_THREADS
        pool = indexWorkerPool(static_cast<QThreadPool *>(m_indexPool));
#endif
        notebook::IndexCacheKeeper::Policy policy = m_cachePolicy;
#ifdef Q_OS_WASM
        // The site's quota is shared with the notes: the cache takes at most half of what is free.
        const platform::WasmStorage::Estimate storage = platform::WasmStorage::estimate();
        policy.maxBytes = notebook::indexCacheBudget(storage.usage, storage.quota, QFileInfo(m_indexCachePath).size(),
                                                     policy.maxBytes);
#endif
        m_cacheKeeper = std::make_unique<notebook::IndexCacheKeeper>(*m_vault, *m_index, std::move(pool),
                                                                     m_indexCachePath, policy);
#ifdef Q_OS_WASM
        connect(m_cacheKeeper.get(), &notebook::IndexCacheKeeper::saved, this,
                [] { platform::reportIndexCacheSaved(); });
#endif
        m_cacheKeeper->loaded(m_loader->stats());
    }
    // Every note is known now: reminders may fire and missed ones be recorded (a half-built
    // index would make reminders look deleted). A vault with paused reminders never starts its
    // scheduler: an empty schedule() would cancel what the previous notebook armed.
    if (!m_remindersPaused)
        m_reminders->start();
    setReady(false);
#ifdef Q_OS_WASM
    {
        const notebook::IndexLoader::Stats stats = indexStats();
        platform::reportIndexStats(int(stats.total - stats.attachments), int(stats.fromCache));
    }
    platform::reportIndexReady(); // for scripts/wasm-smoke.mjs and the browser benchmarks
#endif
}

void Workspace::stopIndexCache(const QString &why)
{
    if (m_indexCachePath.isEmpty())
        return;
    qCWarning(logging::lcIndex).noquote()
            << u"Index cache not saved (%1): none is written while this notebook is open"_s.arg(why);
    m_cacheKeeper.reset();
    m_indexCachePath.clear(); // indexingFinished() creates no new keeper
}

void Workspace::setReady(bool wasReady)
{
    if (wasReady != ready())
        Q_EMIT readyChanged();
}

qreal Workspace::indexProgress() const
{
    if (!m_vault || !indexing())
        return 1.0;
    return m_loader->progress();
}

void Workspace::onVaultChanged(const QList<VaultChange> &changes)
{
    QStringList changed;
    {
        VaultIndex::Batch batch(*m_index);
        for (const VaultChange &c : changes) {
            m_dataUrls.remove(c.path);
            m_dataUrlSizes.remove(c.path);
            if (!c.oldPath.isEmpty()) {
                m_dataUrls.remove(c.oldPath);
                m_dataUrlSizes.remove(c.oldPath);
            }
            switch (c.type) {
            case ChangeType::Created:
            case ChangeType::Modified:
                if (m_loader)
                    m_loader->forget(c.path); // a worker's older read of it is dropped
                indexFile(c.path);
                break;
            case ChangeType::Deleted:
                if (m_loader)
                    m_loader->forget(c.path);
                m_index->remove(c.path);
                m_vault->store()->remove(foldStateKey(c.path));
                break;
            case ChangeType::Renamed: {
                QString content;
                if (paths::isMarkdown(c.path))
                    content = m_vault->readText(c.path);
                if (m_loader) {
                    m_loader->forget(c.oldPath);
                    m_loader->forget(c.path);
                }
                if (paths::isIgnored(c.path))
                    m_index->remove(c.oldPath);
                else
                    m_index->rename(c.oldPath, c.path, content);
                renamePathInLists(c.oldPath, c.path);
                break;
            }
            }
            if (c.type == ChangeType::Renamed && !paths::isIgnored(c.oldPath))
                changed.append(c.oldPath);
            if (!paths::isIgnored(c.path))
                changed.append(c.path);
        }
    }
    ++m_revision;
    Q_EMIT revisionChanged();
    // Deletions and restores move files through the trash (.trash is not tracked itself).
    if (std::any_of(changes.cbegin(), changes.cend(), [](const VaultChange &c) {
            return c.type == ChangeType::Deleted || c.type == ChangeType::Created || c.type == ChangeType::Renamed;
        })) {
        m_trashCount = -1;
        Q_EMIT trashChanged();
    }
    for (const VaultChange &c : changes) {
        if (c.type == ChangeType::Renamed)
            Q_EMIT pathRenamed(c.oldPath, c.path);
        else if (c.type == ChangeType::Deleted)
            Q_EMIT pathRemoved(c.path);
    }
    for (const QString &path : std::as_const(changed))
        Q_EMIT fileChanged(path);
    if (!changed.isEmpty())
        Q_EMIT filesChanged(changed);
}

// ---- EditorHost: files ----------------------------------------------------------------------

QString Workspace::read(const QString &path, bool *ok)
{
    if (!m_vault) {
        if (ok)
            *ok = false;
        return {};
    }
    return m_vault->readText(path, ok);
}

bool Workspace::write(const QString &path, const QString &content)
{
    // fileChanged(path) follows, also for the editor that saved: the same page may be open in a
    // second pane (side panel) that has to reload. The saving editor recognises its own text
    // (PageController compares with what it wrote).
    if (!m_vault)
        return false;
    const bool ok = m_vault->writeText(path, content, ChangeSource::User);
    if (!ok)
        Q_EMIT errorOccurred(tr("Could not save %1: %2").arg(path, m_vault->errorString()));
    return ok;
}

EditorHost::WriteResult Workspace::writeIfUnchanged(const QString &path, const QString &content,
                                                    const QString &expected)
{
    if (!m_vault)
        return WriteResult::Failed;
    // A change found on disk is reported by the vault right away (onVaultChanged: fileChanged),
    // so the editor merges it before it saves again.
    switch (m_vault->writeTextIfUnchanged(path, content, expected, ChangeSource::User)) {
    case WriteOutcome::Written: return WriteResult::Written;
    case WriteOutcome::Changed: return WriteResult::Changed;
    case WriteOutcome::Failed: break;
    }
    Q_EMIT errorOccurred(tr("Could not save %1: %2").arg(path, m_vault->errorString()));
    return WriteResult::Failed;
}

void Workspace::editorOpened(const QString &rawPath)
{
    // The pages editors show are watched themselves, so another program that rewrites one in
    // place is noticed at once (the vault's folder watches do not see that). Only those: reads
    // and saves of other pages (embeds, Tasks, Calendar, export) must not push them out of the
    // vault's bounded list of watched files.
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty() || m_editorFiles[path]++ > 0)
        return;
    if (m_vault)
        m_vault->watchFile(path);
}

void Workspace::editorClosed(const QString &rawPath)
{
    const QString path = paths::normalize(rawPath);
    const auto it = m_editorFiles.find(path);
    if (it == m_editorFiles.end() || --it.value() > 0)
        return;
    m_editorFiles.erase(it);
    if (m_vault)
        m_vault->unwatchFile(path);
}

QString Workspace::readFile(const QString &path)
{
    return read(path);
}

namespace {

/// The length of `bytes` without a UTF-8 sequence its end cuts short (a lead byte with fewer
/// continuation bytes after it than it announces); the whole length otherwise.
qsizetype utf8CompletePrefix(QByteArrayView bytes)
{
    const qsizetype n = bytes.size();
    // The last sequence starts at most 3 bytes before the end (4-byte sequences are the longest).
    for (qsizetype i = n - 1; i >= 0 && i >= n - 4; --i) {
        const auto b = static_cast<unsigned char>(bytes.at(i));
        if ((b & 0xC0) == 0x80)
            continue; // a continuation byte: its lead comes before it
        const qsizetype length = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        return i + length > n && b >= 0xC0 && b < 0xF8 ? i : n;
    }
    return n; // continuation bytes only: no character to complete
}

} // namespace

QVariantMap Workspace::readTextStart(const QString &path, int maxBytes) const
{
    QVariantMap out{{u"text"_s, QString()}, {u"truncated"_s, false}, {u"bytes"_s, 0}, {u"size"_s, 0}, {u"ok"_s, false}};
    if (!m_vault || path.isEmpty())
        return out;
    // Straight from the file: Vault::readText() reads it whole (a 20 MB photo: 20 MB of bytes and
    // 40 MB of text) for the 200 KB shown.
    QFile file(m_vault->absolutePath(path));
    if (!file.open(QIODevice::ReadOnly))
        return out;
    const qint64 size = file.size();
    QByteArray bytes = file.read(qMax(0, maxBytes));
    const bool truncated = bytes.size() < size;
    if (truncated)
        bytes.truncate(utf8CompletePrefix(bytes));
    QByteArrayView text(bytes);
    if (text.startsWith("\xEF\xBB\xBF"))
        text = text.sliced(3);
    const QLocale locale;
    out.insert(u"text"_s, QString::fromUtf8(text));
    out.insert(u"truncated"_s, truncated);
    out.insert(u"bytes"_s, double(bytes.size()));
    out.insert(u"size"_s, double(size));
    out.insert(u"bytesText"_s, locale.formattedDataSize(bytes.size(), 1, QLocale::DataSizeTraditionalFormat));
    out.insert(u"sizeText"_s, locale.formattedDataSize(size, 1, QLocale::DataSizeTraditionalFormat));
    out.insert(u"ok"_s, true);
    return out;
}

bool Workspace::writeFile(const QString &path, const QString &content)
{
    return write(path, content);
}

bool Workspace::exists(const QString &path) const
{
    return m_vault && m_vault->exists(path);
}

QString Workspace::pageTitle(const QString &path) const
{
    return paths::pageTitle(path);
}

QString Workspace::folderOf(const QString &path) const
{
    return paths::dirname(path);
}

bool Workspace::isPicture(const QString &path) const
{
    return paths::isPicturePath(path);
}

bool Workspace::canDecodePicture(const QString &path) const
{
    return paths::isPicturePath(path) && editor::canDecodePictureFormat(path);
}

bool Workspace::isDrawing(const QString &rawPath) const
{
    if (!m_vault || rawPath.isEmpty())
        return false;
    // The vault's own file (also in the browser: IDBFS is a folder of files there too).
    const QString path = paths::normalize(rawPath);
    const bool markdown = path.endsWith(u".md"_s, Qt::CaseInsensitive);
    if (!markdown && !path.endsWith(u".svg"_s, Qt::CaseInsensitive)
        && !path.endsWith(u".excalidraw"_s, Qt::CaseInsensitive))
        return false;
    if (markdown && m_index) {
        // An Obsidian Excalidraw plugin file: the note's frontmatter has the plugin's key — the
        // index knows without reading the file (bindings ask this for every page opened).
        if (const ParsedNote *note = m_index->note(path))
            return note->frontmatter.contains(drawing::ExcalidrawPluginKey.toString());
    }
    return m_vault->exists(path) && editor::localFileIsDrawing(m_vault->absolutePath(path));
}

QSize Workspace::drawingSize(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    return editor::localDrawingSize(m_vault->absolutePath(paths::normalize(path)));
}

QByteArray Workspace::readAttachment(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    bool ok = false;
    const QByteArray bytes = m_vault->readBinary(paths::normalize(path), &ok);
    return ok ? bytes : QByteArray();
}

bool Workspace::writeAttachment(const QString &rawPath, const QByteArray &data)
{
    if (!m_vault || rawPath.isEmpty() || data.isEmpty())
        return false;
    const QString path = paths::normalize(rawPath);
    // Only over the file as it is on disk: one deleted there (another device through Syncthing, a
    // file manager) whose deletion the vault has not seen yet is never written again. The read
    // that fails makes the vault check the path, so pathRemoved follows.
    if (!QFileInfo(m_vault->absolutePath(path)).isFile()) {
        bool ok = false;
        m_vault->readBinary(path, &ok);
        return false;
    }
    if (m_vault->writeBinary(path, data, ChangeSource::User))
        return true;
    Q_EMIT errorOccurred(tr("Could not save the drawing %1: %2").arg(paths::basename(path), m_vault->errorString()));
    return false;
}

QString Workspace::createAttachment(const QString &rawPath, const QByteArray &data)
{
    if (!m_vault || rawPath.isEmpty() || data.isEmpty())
        return {};
    const QString path = paths::normalize(rawPath);
    const VaultPath created = m_vault->createBinary(path, data, ChangeSource::User);
    if (created.isEmpty())
        Q_EMIT errorOccurred(tr("Could not save the drawing %1: %2").arg(paths::basename(path), m_vault->errorString()));
    return created;
}

QString Workspace::journalDate(const QString &path) const
{
    if (!m_vault)
        return {};
    const auto date = markdown::journalDateFromPath(path, m_vault->config());
    return date ? date->toString(Qt::ISODate) : QString();
}

// ---- EditorHost: index-backed helpers --------------------------------------------------------

QVariantList Workspace::suggestPages(const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const PageSuggestion &s : m_index->suggestPages(prefix, limit)) {
        QVariantMap m{ { u"path"_s, s.path }, { u"title"_s, s.title }, { u"folder"_s, paths::dirname(s.path) } };
        if (!s.alias.isEmpty())
            m.insert(u"alias"_s, s.alias);
        out.append(m);
    }
    return out;
}

QVariantList Workspace::suggestBlocks(const QString &text, int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const BlockRef &ref : m_index->suggestBlocks(text, limit)) {
        const ParsedNote *note = m_index->note(ref.path);
        if (!note || ref.blockIndex < 0 || ref.blockIndex >= note->blocks.size())
            continue;
        const Block &block = note->blocks.at(ref.blockIndex);
        QVariantMap m{ { u"path"_s, ref.path },
                       { u"line"_s, block.line },
                       { u"text"_s, lineOf(block.text) },
                       { u"title"_s, note->title } };
        if (!block.id.isEmpty())
            m.insert(u"blockId"_s, block.id);
        out.append(m);
    }
    return out;
}

QVariantList Workspace::suggestTags(const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_index || limit <= 0)
        return out;
    // VaultIndex::suggestTags(): tags in use, then pages no tag names yet, then tags containing
    // the text (a tag names a page, docs/SYNTAX.md §3).
    for (const TagSuggestion &s : m_index->suggestTags(prefix, limit)) {
        QVariantMap m{{u"tag"_s, s.tag}, {u"count"_s, s.count}, {u"kind"_s, s.page ? u"page"_s : u"tag"_s}};
        if (!s.path.isEmpty()) {
            m.insert(u"path"_s, s.path);
            m.insert(u"title"_s, paths::pageTitle(s.path));
            m.insert(u"folder"_s, paths::dirname(s.path));
        }
        out.append(m);
    }
    return out;
}

QVariantList Workspace::propertyKeys() const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const PropertyKeyInfo &k : m_index->propertyKeys())
        out.append(QVariantMap{{u"key"_s, k.key}, {u"count"_s, k.count}, {u"type"_s, k.type}});
    return out;
}

QVariantList Workspace::propertyValues(const QString &key, const QString &prefix, int limit) const
{
    QVariantList out;
    if (!m_index)
        return out;
    for (const PropertyValueCount &v : m_index->propertyValues(key, prefix, limit))
        out.append(QVariantMap{{u"value"_s, v.value}, {u"count"_s, v.count}});
    return out;
}

QString Workspace::resolveLink(const QString &target, const QString &fromPath) const
{
    if (!m_index)
        return {};
    return m_index->resolveLink(target, fromPath).value_or(QString());
}

QString Workspace::blockText(const QString &blockId) const
{
    if (!m_index)
        return {};
    const auto ref = m_index->blockById(blockId);
    if (!ref)
        return {};
    const ParsedNote *note = m_index->note(ref->path);
    if (!note || ref->blockIndex < 0 || ref->blockIndex >= note->blocks.size())
        return {};
    return note->blocks.at(ref->blockIndex).text;
}

bool Workspace::blockIdTaken(const QString &blockId) const
{
    return m_index && !blockId.isEmpty() && m_index->blockById(blockId).has_value();
}

QString Workspace::linkedBlockText(const QString &path, const QString &blockId) const
{
    const ParsedNote *note = m_index ? m_index->note(path) : nullptr;
    if (!note)
        return {};
    if (const int i = note->blockIds.value(blockId, -1); i >= 0 && i < note->blocks.size())
        return note->blocks.at(i).text;
    return blockText(blockId); // moved to another page (resolveRoute follows it there)
}

QString Workspace::ensureBlockId(const QString &path, int line)
{
    if (!m_vault)
        return {};
    bool ok = false;
    const QString content = m_vault->readText(path, &ok);
    if (!ok)
        return {};
    finishIndexing(); // a new id avoids every id of the vault, also those not indexed yet
    const auto taken = [this](const QString &id) { return blockIdTaken(id); };
    const markdown::EnsuredBlockId result = markdown::ensureBlockId(content, line, taken);
    if (result.id.isEmpty())
        return {};
    // Written as an ordinary change: an editor showing that page reloads it (fileChanged).
    if (result.content != content && !m_vault->writeText(path, result.content, ChangeSource::User))
        return {};
    return result.id;
}

QVariantList Workspace::templates() const
{
    QVariantList out;
    if (!m_vault)
        return out;
    const QString folder = paths::normalize(m_vault->config().templatesFolder);
    if (folder.isEmpty())
        return out;
    const QString prefix = folder + u'/';
    QStringList found;
    for (const FileStat &f : m_vault->markdownFiles()) {
        if (f.path.startsWith(prefix, Qt::CaseInsensitive))
            found.append(f.path);
    }
    // By title as people read them, not by code units ("journal" before "Spotkanie").
    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) {
        return QString::compare(paths::pageTitle(a), paths::pageTitle(b), Qt::CaseInsensitive) < 0;
    });
    for (const QString &path : std::as_const(found))
        out.append(pageEntry(path));
    return out;
}

// ---- EditorHost: assets ----------------------------------------------------------------------

QUrl Workspace::assetUrl(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    if (!m_dataUrlAssets) {
        const QString key = paths::normalize(path);
        QUrl url = QUrl::fromLocalFile(m_vault->absolutePath(key));
        // A drawing changes under the same name (the drawing editor's saves): its version in the
        // URL, as Qt's pixmap cache keys by the URL — the embeds and the picture view load the
        // new one, the file is still found (a local file's query is not part of its path).
        if (key.endsWith(u".svg"_s, Qt::CaseInsensitive) && isDrawing(key)) {
            if (const std::optional<FileStat> st = m_vault->stat(key))
                url.setQuery(u"v="_s + QString::number(st->mtime) + u'-' + QString::number(st->size));
        }
        return url;
    }
    const QString key = paths::normalize(path);
    const auto cached = m_dataUrls.constFind(key);
    if (cached != m_dataUrls.constEnd())
        return cached.value();
    bool ok = false;
    const QByteArray bytes = m_vault->readBinary(key, &ok);
    if (!ok)
        return {};
    const QString mime = QMimeDatabase().mimeTypeForFile(key, QMimeDatabase::MatchExtension).name();
    QUrl url(u"data:"_s + mime + u";base64,"_s + QString::fromLatin1(bytes.toBase64()));
    m_dataUrls.insert(key, url);
    if (mime.startsWith(u"image/"_s)) {
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        m_dataUrlSizes.insert(key, editor::pictureSize(&buffer, key));
    }
    return url;
}

QUrl Workspace::drawingUrl(const QString &path, bool dark) const
{
    if (!m_vault || path.isEmpty())
        return {};
    // The vault's own file (in the browser too: IDBFS is a folder of files there), read by the
    // image provider "drawing" (src/app/drawing/drawingimageprovider.h, the URL's format there);
    // the version keeps Qt's pixmap cache honest, as in assetUrl().
    const QString key = paths::normalize(path);
    QString version;
    if (const std::optional<FileStat> st = m_vault->stat(key))
        version = QString::number(st->mtime) + u'-' + QString::number(st->size);
    return QUrl(u"image://drawing/"_s
                        + QString::fromLatin1(
                                QUrl::toPercentEncoding(QDir::fromNativeSeparators(m_vault->absolutePath(key))))
                        + u"?v="_s + QString::fromLatin1(QUrl::toPercentEncoding(version)) + u"&dark="_s
                        + (dark ? u'1' : u'0'),
                QUrl::StrictMode);
}

QSize Workspace::assetPixelSize(const QString &path) const
{
    if (!m_dataUrlAssets || !m_vault || path.isEmpty())
        return {};
    const QString key = paths::normalize(path);
    if (!m_dataUrls.contains(key))
        assetUrl(path);
    return m_dataUrlSizes.value(key);
}

QSize Workspace::pictureSize(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    // A `.excalidraw` scene or an Obsidian Excalidraw plugin file: its preview's size (the drawing
    // image provider draws it so; an SVG drawing has its root's size, as any picture).
    if ((path.endsWith(u".md"_s, Qt::CaseInsensitive) || path.endsWith(u".excalidraw"_s, Qt::CaseInsensitive))
        && isDrawing(path))
        return drawingSize(path);
    const QSize pixels = assetPixelSize(path); // WebAssembly: the data URL's bytes (cached above)
    if (pixels.isValid() && !pixels.isEmpty())
        return pixels;
    const QUrl url = assetUrl(path); // desktop / Android: the file on disk
    return url.isLocalFile() ? editor::localPictureSize(url.toLocalFile()) : QSize();
}

QString Workspace::saveAttachment(const QString &fileName, const QByteArray &data)
{
    if (!m_vault)
        return {};
    QString name = paths::basename(QString(fileName).replace(u'\\', u'/')).trimmed();
    if (name.isEmpty())
        name = u"attachment-"_s + now().toString(u"yyyyMMdd-HHmmss"_s);
    // sanitizeFileName keeps the extension (dots are allowed) and drops separators/reserved chars.
    name = paths::sanitizeFileName(name);
    if (paths::extension(name).isEmpty()) {
        // Pasted images often come without a name: take the type from the bytes, so the embed
        // shows the image and other programs recognise the file. Plain text stays as it is.
        const QMimeType type = QMimeDatabase().mimeTypeForData(data);
        const QString suffix = type.preferredSuffix();
        if (!suffix.isEmpty() && !type.inherits(u"text/plain"_s) && !type.isDefault())
            name += u'.' + suffix;
    }
    const QString folder = paths::normalize(m_vault->config().attachmentsFolder);
    const VaultPath path = m_vault->createBinary(folder.isEmpty() ? name : paths::join(folder, name), data,
                                                 ChangeSource::User);
    if (path.isEmpty())
        Q_EMIT errorOccurred(tr("Could not save the attachment %1: %2").arg(name, m_vault->errorString()));
    return path;
}

QString Workspace::importAttachment(const QString &fileName, const QByteArray &data)
{
    if (!m_vault || data.isEmpty())
        return {};
    // The same file picked again (another slide of the lecture's PDF): the one already stored —
    // under its name or one saveAttachment() gave it ("Slajdy 2.pdf").
    const QString name = paths::sanitizeFileName(paths::basename(QString(fileName).replace(u'\\', u'/')).trimmed());
    const QString folder = paths::normalize(m_vault->config().attachmentsFolder);
    if (!name.isEmpty()) {
        const VaultPath path = folder.isEmpty() ? name : paths::join(folder, name);
        const QString ext = paths::extension(path);
        const QString stem = path.left(path.size() - ext.size());
        for (int i = 1; i <= 50; ++i) {
            const VaultPath candidate = m_vault->existingCase(i == 1 ? path : stem + u' ' + QString::number(i) + ext);
            const std::optional<FileStat> stat = m_vault->stat(candidate);
            if (!stat)
                break;
            bool ok = false;
            if (!stat->isDir && stat->size == data.size() && m_vault->readBinary(candidate, &ok) == data && ok)
                return candidate;
        }
    }
    return saveAttachment(fileName, data);
}

QString Workspace::importAttachment(const QUrl &file, qint64 maxBytes)
{
    if (!m_vault)
        return {};
    if (file.isLocalFile()) {
        // A file of the notebook itself (the lecture's PDF synced into it): used where it is.
        const QString absolute = QFileInfo(file.toLocalFile()).canonicalFilePath();
        const QString root = QFileInfo(m_vault->rootPath()).canonicalFilePath();
        if (!absolute.isEmpty() && !root.isEmpty()) {
            const QString relative = QDir(root).relativeFilePath(absolute);
            if (!relative.startsWith(u"../"_s) && relative != u".."_s && !QDir::isAbsolutePath(relative)
                && QFileInfo(absolute).isFile())
                return m_vault->existingCase(paths::normalize(relative));
        }
    }
    return EditorHost::importAttachment(file, maxBytes);
}

QString Workspace::absoluteFilePath(const QString &path) const
{
    if (!m_vault || path.isEmpty())
        return {};
    return m_vault->absolutePath(paths::normalize(path));
}

// ---- EditorHost: context and navigation ------------------------------------------------------

QVariantMap Workspace::vaultConfig() const
{
    // The command line's JSON of the settings (notebook::vaultConfigJson): one serializer for both.
    return notebook::vaultConfigJson(m_vault ? m_vault->config() : VaultConfig()).toVariantMap();
}

bool Workspace::setConfig(const QVariantMap &map)
{
    if (!m_vault)
        return false;
    VaultConfig c = m_vault->config();
    const auto folder = [&](const char *key, QString &field) {
        const QString k = QString::fromLatin1(key);
        if (map.contains(k))
            field = paths::normalize(map.value(k).toString());
    };
    folder("journalsFolder", c.journalsFolder);
    folder("newPagesFolder", c.newPagesFolder);
    folder("attachmentsFolder", c.attachmentsFolder);
    folder("templatesFolder", c.templatesFolder);
    folder("journalTemplate", c.journalTemplate);
    if (map.contains(u"journalFileFormat"_s)) {
        const QString format = map.value(u"journalFileFormat"_s).toString().trimmed();
        if (!isUsableJournalFormat(format))
            return false;
        c.journalFileFormat = format;
    }
    if (map.contains(u"defaultReminderTime"_s)) {
        const QTime t = QTime::fromString(map.value(u"defaultReminderTime"_s).toString().trimmed(), u"H:mm"_s);
        if (!t.isValid())
            return false;
        c.defaultReminderTime = t;
    }
    if (map.contains(u"weekStartsOn"_s)) {
        const int day = map.value(u"weekStartsOn"_s).toInt();
        if (day < 1 || day > 7)
            return false;
        c.weekStartsOn = Qt::DayOfWeek(day);
    }
    // Note modes: "outline" | "document" (anything else is refused).
    for (const auto &[key, field] :
         {std::pair{u"journalMode"_s, &c.journalMode}, std::pair{u"pageMode"_s, &c.pageMode}}) {
        if (!map.contains(key))
            continue;
        const auto mode = markdown::parseNoteMode(map.value(key).toString());
        if (!mode)
            return false;
        *field = *mode;
    }
    if (sameVaultConfig(c, m_vault->config()))
        return true;
    if (!m_vault->saveConfig(c)) {
        Q_EMIT errorOccurred(tr("Could not save the notebook settings: %1").arg(m_vault->errorString()));
        return false;
    }
    return true;
}

QStringList Workspace::configWarnings() const
{
    return m_vault ? m_vault->configWarnings() : QStringList();
}

QStringList Workspace::configFromObsidian() const
{
    return m_vault ? m_vault->configAdoptedFromObsidian() : QStringList();
}

bool Workspace::saveAdoptedConfig()
{
    if (!m_vault)
        return false;
    if (!m_vault->saveConfig(m_vault->config())) {
        Q_EMIT errorOccurred(tr("Could not save the notebook settings: %1").arg(m_vault->errorString()));
        return false;
    }
    return true;
}

void Workspace::navigate(const QVariantMap &route, bool newPane)
{
    Q_EMIT navigationRequested(resolveRoute(route), newPane);
}

QVariantMap Workspace::resolveRoute(const QVariantMap &route)
{
    // A tag names a page (docs/SYNTAX.md §3): a click on #tag opens that page, created by its
    // first content when it does not exist yet. {view: "tag", overview: true} (and routes the
    // shell navigates to itself) keep the tag overview. `fromTag`: a page not written yet waits for
    // a tap on touch screens instead of bringing the keyboard over the blocks with the tag.
    if (route.value(u"view"_s).toString() == u"tag" && !route.value(u"overview"_s).toBool() && m_vault) {
        const QString path = tagPage(route.value(u"tag"_s).toString());
        if (!path.isEmpty())
            return {{u"view"_s, u"page"_s}, {u"path"_s, path}, {u"fromTag"_s, route.value(u"tag"_s).toString()}};
        return route;
    }
    if (route.value(u"view"_s).toString() != u"page" || !m_vault)
        return route;
    // [[Page#^id]] whose block was moved to another page (cut and pasted, a block moved by hand):
    // the index knows where the id is now. Only when the linked page is known and lacks the id.
    if (const QString path = route.value(u"path"_s).toString(); !path.isEmpty()) {
        const QString blockId = route.value(u"blockId"_s).toString();
        if (blockId.isEmpty() || !m_index)
            return route;
        if (indexing())
            finishIndexing();
        const ParsedNote *note = m_index->note(path);
        if (!note || note->blockIds.contains(blockId))
            return route;
        if (const auto ref = m_index->blockById(blockId); ref && ref->path != path) {
            QVariantMap moved = route;
            moved.insert(u"path"_s, ref->path);
            moved.remove(u"target"_s);
            return moved;
        }
        return route;
    }
    // Page routes the editor could not resolve itself: {view: "page", blockId} for a ((block)) of
    // another page, {view: "page", target} for a link to a page that does not exist.
    QVariantMap out = route;
    // Right after opening the index is still being built: what looks unresolved may exist.
    if (indexing())
        finishIndexing();
    const QString blockId = route.value(u"blockId"_s).toString();
    if (!blockId.isEmpty() && m_index) {
        if (const auto ref = m_index->blockById(blockId)) {
            out.insert(u"path"_s, ref->path);
            return out;
        }
    }
    const QString target = route.value(u"target"_s).toString().trimmed();
    if (target.isEmpty())
        return out;
    QString path = resolveLink(target, QString());
    if (path.isEmpty())
        path = newPagePath(target);
    if (!path.isEmpty())
        out.insert(u"path"_s, path);
    return out;
}

QString Workspace::tagPage(const QString &rawTag)
{
    if (!m_vault)
        return {};
    QString tag = rawTag.trimmed();
    if (tag.startsWith(u'#'))
        tag.remove(0, 1);
    if (tag.startsWith(u"[["_s) && tag.endsWith(u"]]"_s) && tag.size() >= 4)
        tag = tag.mid(2, tag.size() - 4).trimmed();
    while (tag.endsWith(u'/'))
        tag.chop(1);
    if (tag.trimmed().isEmpty())
        return {};
    // Right after opening the index is still being built: what looks unresolved may exist.
    if (indexing())
        finishIndexing();
    if (m_index) {
        if (const auto page = m_index->resolveTag(tag))
            return *page;
        // A new page is named as the tag was first written (#Praca and #praca: "Praca.md"),
        // whichever spelling was clicked.
        const QString key = tag.normalized(QString::NormalizationForm_C).toLower();
        for (const TagCount &t : m_index->tags()) {
            if (t.tag == key) {
                if (!t.display.isEmpty())
                    tag = t.display;
                break;
            }
        }
    }
    return newPagePath(tag);
}

QString Workspace::newPagePath(const QString &target) const
{
    return m_vault ? notebook::newPagePath(*m_vault, target) : QString();
}

void Workspace::openUrl(const QUrl &url)
{
    // Only links meant for the browser / mail client; never file:, javascript: or app schemes
    // from note content.
    static const QStringList allowed{ u"http"_s, u"https"_s, u"mailto"_s };
    if (!url.isValid() || !allowed.contains(url.scheme().toLower())) {
        qCWarning(lcWorkspace) << "Refusing to open" << url.scheme() << "link";
        return;
    }
    QDesktopServices::openUrl(url);
}

// ---- Pages ------------------------------------------------------------------------------------

QString Workspace::createPage(const QString &title, const QString &folder, const QString &mode)
{
    if (!m_vault)
        return {};
    const notebook::CreatedPage page =
            notebook::createPage(*m_vault, title, folder, markdown::parseNoteMode(mode), QString(), ChangeSource::User);
    // A page whose name differs only in letter case ("dom" next to "Dom"): one file on Android's
    // shared storage and other case-insensitive folders a synced vault may land in, and one page
    // for links — that page is opened instead of a second one.
    if (page.existing)
        Q_EMIT noticeOccurred(tr("“%1” is the page “%2” (names that differ only in letter case are the same page).")
                                      .arg(paths::pageTitle(page.wanted), paths::pageTitle(page.path)));
    else if (page.problem.kind == notebook::PathProblem::Failed)
        Q_EMIT errorOccurred(tr("Could not create the page: %1").arg(page.problem.detail));
    return page.path;
}

QString Workspace::deletePage(const QString &rawPath)
{
    if (!m_vault)
        return {};
    const QString path = paths::normalize(rawPath);
    if (path.isEmpty() || !m_vault->exists(path))
        return {};
    const notebook::DeletedPath deleted = notebook::deletePath(*m_vault, path, ChangeSource::User);
    if (deleted.trashPath.isEmpty()) {
        Q_EMIT errorOccurred(
                tr("Could not move “%1” to the trash: %2").arg(paths::pageTitle(path), deleted.problem.detail));
        return {};
    }
    forgetPath(path);
    if (deleted.folder)
        notifyFolderChange({path}); // an empty folder (or one of folders only) has no file changes
    return deleted.trashPath;
}

QString Workspace::restore(const QString &trashPath)
{
    if (!m_vault)
        return {};
    const VaultPath path = m_vault->restore(trashPath, ChangeSource::User);
    if (path.isEmpty())
        Q_EMIT errorOccurred(tr("Could not restore the file: %1").arg(m_vault->errorString()));
    else if (const auto stat = m_vault->stat(path); stat && stat->isDir)
        notifyFolderChange({path});
    return path;
}

// ---- Journals ---------------------------------------------------------------------------------

QString Workspace::journalPath(const QString &date) const
{
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!d.isValid())
        return {};
    return markdown::journalPathForDate(d, m_vault ? m_vault->config() : VaultConfig());
}

QString Workspace::journalFileFor(const QString &date) const
{
    if (!m_vault)
        return {};
    return notebook::journalFileFor(*m_vault, QDate::fromString(date, Qt::ISODate));
}

QString Workspace::ensureJournal(const QString &date)
{
    const QDate d = QDate::fromString(date, Qt::ISODate);
    if (!m_vault || !d.isValid())
        return {};
    const notebook::EnsuredJournal journal =
            notebook::ensureJournal(*m_vault, d, now(), notebook::contentLocale(m_language), ChangeSource::User);
    if (journal.path.isEmpty())
        Q_EMIT errorOccurred(tr("Could not create the journal page: %1").arg(journal.error));
    return journal.path;
}

QString Workspace::captureToJournal(const QString &text)
{
    const QString block = notebook::blockFromText(text);
    if (block.isEmpty() || !m_vault)
        return {};
    return appendToTodaysJournal(block);
}

QString Workspace::quickCapture(const QString &text)
{
    if (!m_vault)
        return {};
    const notebook::CaptureLine line = notebook::buildCaptureLine(text, now(), m_vault->config().weekStartsOn,
                                                                  notebook::contentLocale(m_language));
    if (line.kind == notebook::CaptureLine::Kind::Empty)
        return {};
    return appendToTodaysJournal(line.block);
}

QVariantMap Workspace::quickCapturePreview(const QString &text) const
{
    const dates::UiLocale locale = notebook::contentLocale(m_language);
    const QDateTime current = now();
    const notebook::CaptureLine line =
            notebook::buildCaptureLine(text, current, m_vault ? m_vault->config().weekStartsOn : Qt::Monday, locale);
    const bool dated = line.when.isValid();
    return {
            {u"kind"_s, notebook::captureKindName(line.kind)},
            {u"text"_s, line.text},
            {u"date"_s, dated ? line.when.date.toString(Qt::ISODate) : QString()},
            {u"time"_s, dated && line.when.time ? line.when.time->toString(u"HH:mm"_s) : QString()},
            {u"when"_s, dated ? dates::formatHuman(line.when, locale, current.date()) : QString()},
            {u"line"_s, line.block.section(u'\n', 0, 0)},
    };
}

QString Workspace::appendJournalBlocks(const QString &blocks)
{
    return appendToTodaysJournal(blocks);
}

QString Workspace::appendToTodaysJournal(const QString &block)
{
    if (!m_vault || block.trimmed().isEmpty())
        return {};
    const QString path = ensureJournal(now().date().toString(Qt::ISODate));
    if (path.isEmpty())
        return {};
    bool ok = false;
    const QString text = m_vault->readText(path, &ok);
    if (!ok)
        return {};
    // A note that is not UTF-8 (windows-1250 from an old editor, synced in) is left alone: written
    // back, each of its other bytes would be U+FFFD. Read again only when the text shows one.
    if (text.contains(QChar::ReplacementCharacter) && !isUtf8Text(m_vault->readBinary(path))) {
        qCWarning(lcWorkspace) << "Not appending to" << path << "- it is not UTF-8 text";
        return {};
    }
    // A template's empty last bullet is filled, the note's line ending kept (notebook/journals.h).
    const QString content = notebook::appendBlock(text, block);
    return writeFile(path, content) ? path : QString();
}

// ---- Sidebar lists ---------------------------------------------------------------------------

QStringList Workspace::storedList(const QString &key) const
{
    QStringList out;
    if (!m_vault)
        return out;
    const QJsonArray array = m_vault->store()->get(key).toArray();
    for (const QJsonValue &v : array) {
        if (v.isString())
            out.append(v.toString());
    }
    return out;
}

void Workspace::storeList(const QString &key, const QStringList &list)
{
    if (!m_vault)
        return;
    m_vault->store()->set(key, QJsonArray::fromStringList(list));
}

QVariantList Workspace::favorites() const
{
    QVariantList out;
    for (const QString &path : storedList(QString::fromLatin1(kFavorites))) {
        if (m_vault && m_vault->exists(path))
            out.append(pageEntry(path));
    }
    return out;
}

QVariantList Workspace::recentPages() const
{
    QVariantList out;
    for (const QString &path : storedList(QString::fromLatin1(kRecentPages))) {
        if (m_vault && m_vault->exists(path))
            out.append(pageEntry(path));
    }
    return out;
}

bool Workspace::isFavorite(const QString &path) const
{
    return storedList(QString::fromLatin1(kFavorites)).contains(paths::normalize(path));
}

void Workspace::setFavorite(const QString &rawPath, bool favorite)
{
    const QString path = paths::normalize(rawPath);
    QStringList list = storedList(QString::fromLatin1(kFavorites));
    if (list.contains(path) == favorite || path.isEmpty())
        return;
    if (favorite)
        list.append(path);
    else
        list.removeAll(path);
    storeList(QString::fromLatin1(kFavorites), list);
    Q_EMIT favoritesChanged();
}

void Workspace::applicationStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationActive) {
        m_wasInactive = true;
        return;
    }
#ifndef Q_OS_WASM
    if (std::exchange(m_wasInactive, false) && m_vault)
        m_vault->rescan(); // emits changed() for what other programs did meanwhile
#endif
}

void Workspace::notePageOpened(const QString &rawPath)
{
    const QString path = paths::normalize(rawPath);
    if (!m_vault || path.isEmpty() || !m_vault->exists(path))
        return;
    QStringList list = storedList(QString::fromLatin1(kRecentPages));
    if (!list.isEmpty() && list.first() == path)
        return;
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > MaxRecentPages)
        list.removeLast();
    storeList(QString::fromLatin1(kRecentPages), list);
    Q_EMIT recentPagesChanged();
}

QVariantList Workspace::foldState(const QString &path) const
{
    QVariantList out;
    if (!m_vault)
        return out;
    for (const QJsonValue &v : m_vault->store()->get(foldStateKey(paths::normalize(path))).toArray()) {
        if (v.isDouble() && v.toInt() >= 0)
            out.append(v.toInt());
    }
    return out;
}

void Workspace::setFoldState(const QString &rawPath, const QVariantList &lines)
{
    const QString path = paths::normalize(rawPath);
    if (!m_vault || path.isEmpty())
        return;
    QList<int> sorted;
    for (const QVariant &v : lines) {
        bool ok = false;
        const int line = v.toInt(&ok);
        if (ok && line >= 0 && !sorted.contains(line))
            sorted.append(line);
    }
    std::sort(sorted.begin(), sorted.end());
    if (sorted.isEmpty()) {
        m_vault->store()->remove(foldStateKey(path));
        return;
    }
    QJsonArray array;
    for (int line : std::as_const(sorted))
        array.append(line);
    m_vault->store()->set(foldStateKey(path), array);
}

void Workspace::forgetPath(const QString &path)
{
    const QString prefix = path + u'/';
    if (m_vault) {
        m_vault->store()->remove(foldStateKey(path));
        m_vault->store()->removePrefix(foldStateKey(prefix));
    }
    for (const char *key : { kFavorites, kRecentPages }) {
        const QString k = QString::fromLatin1(key);
        QStringList list = storedList(k);
        const auto before = list.size();
        list.removeIf([&](const QString &p) { return p == path || p.startsWith(prefix); });
        if (list.size() != before) {
            storeList(k, list);
            if (key == kFavorites)
                Q_EMIT favoritesChanged();
            else
                Q_EMIT recentPagesChanged();
        }
    }
}

void Workspace::renamePathInLists(const QString &oldPath, const QString &newPath)
{
    if (m_vault) {
        LocalStore *store = m_vault->store();
        const QJsonValue folds = store->get(foldStateKey(oldPath));
        if (!folds.isUndefined()) {
            store->remove(foldStateKey(oldPath));
            store->set(foldStateKey(newPath), folds);
        }
    }
    for (const char *key : { kFavorites, kRecentPages }) {
        const QString k = QString::fromLatin1(key);
        QStringList list = storedList(k);
        const qsizetype i = list.indexOf(oldPath);
        if (i < 0)
            continue;
        list[i] = newPath;
        list.removeDuplicates();
        storeList(k, list);
        if (key == kFavorites)
            Q_EMIT favoritesChanged();
        else
            Q_EMIT recentPagesChanged();
    }
}

QVariantList Workspace::switcherPages(const QString &query, int limit) const
{
    QVariantList out;
    if (!m_vault || limit <= 0)
        return out;
    QSet<QString> seen;
    const auto add = [&](const QString &path, const QString &alias) {
        if (seen.contains(path) || out.size() >= limit || paths::isIgnored(path))
            return;
        seen.insert(path);
        const QString folder = paths::dirname(path);
        QString subtitle = folder;
        if (!alias.isEmpty())
            subtitle = folder.isEmpty() ? alias : alias + u" · "_s + folder;
        const bool journal = !journalDate(path).isEmpty();
        out.append(QVariantMap{ { u"title"_s, paths::pageTitle(path) },
                                { u"subtitle"_s, subtitle },
                                { u"icon"_s, journal ? u"calendar"_s : u"file-text"_s },
                                { u"path"_s, path } });
    };
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        for (const QString &path : storedList(QString::fromLatin1(kRecentPages))) {
            if (m_vault->exists(path))
                add(path, {});
        }
    }
    if (m_index) {
        for (const PageSuggestion &s : m_index->suggestPages(q, limit))
            add(s.path, s.alias);
    }
    return out;
}

} // namespace brulion::shell
