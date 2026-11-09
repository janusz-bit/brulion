#include "backup/restore.h"

#include "backup/taskrunner_p.h"
#include "log/categories.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

namespace brulion::backup {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

constexpr int OwnerSliceMs = 8;
/// Bytes read ahead at once (a single larger file is read alone).
constexpr qint64 BatchBytes = 32 << 20;
constexpr qsizetype BatchFiles = 64;
/// History keeps text; anything larger is compared by size elsewhere in the vault as well.
constexpr qint64 MaxHistoryBytes = 16 << 20;

/// The notebook's file an entry's file lands on: its own; on case-insensitive storage also a
/// spelling in another letter case, unless another entry has that very name.
class TargetLookup {
public:
    /// `current` must outlive the lookup: find() points into it.
    TargetLookup(const QList<NotebookFile> &current, const QList<ArchiveEntry> &entries, bool caseInsensitive)
        : m_caseInsensitive(caseInsensitive)
    {
        for (const NotebookFile &file : current) {
            m_files.insert(file.path, &file);
            if (caseInsensitive)
                m_folded.insert(file.path.toCaseFolded(), &file);
        }
        if (caseInsensitive) {
            for (const ArchiveEntry &entry : entries)
                m_entries.insert(entry.path);
        }
    }
    /// nullptr: the entry's file lands on none of the notebook's.
    const NotebookFile *find(const VaultPath &path) const
    {
        if (const NotebookFile *file = m_files.value(path))
            return file;
        if (!m_caseInsensitive)
            return nullptr;
        const NotebookFile *twin = m_folded.value(path.toCaseFolded());
        return twin && !m_entries.contains(twin->path) ? twin : nullptr;
    }

private:
    bool m_caseInsensitive;
    QHash<VaultPath, const NotebookFile *> m_files;
    QHash<QString, const NotebookFile *> m_folded;
    QSet<VaultPath> m_entries;
};

/// `path` is one of `folders` or lies inside one.
bool isInsideAny(QStringView path, const QList<VaultPath> &folders)
{
    return std::ranges::any_of(folders, [path](QStringView folder) {
        return path.startsWith(folder) && (path.size() == folder.size() || path.at(folder.size()) == u'/');
    });
}

/// Names one case-insensitive storage cannot hold apart: two files, or a file and a folder.
std::optional<QString> caseCollision(const QList<ArchiveEntry> &entries)
{
    QHash<QString, VaultPath> files;
    QSet<QString> folders;
    for (const ArchiveEntry &entry : entries) {
        const QString folded = entry.path.toCaseFolded();
        if (entry.folder)
            folders.insert(folded);
        else if (files.contains(folded))
            return entry.path;
        else
            files.insert(folded, entry.path);
        for (qsizetype slash = folded.indexOf(u'/'); slash > 0; slash = folded.indexOf(u'/', slash + 1))
            folders.insert(folded.first(slash));
    }
    for (const auto &[folded, path] : std::as_const(files).asKeyValueRange()) {
        if (folders.contains(folded))
            return path;
    }
    return std::nullopt;
}

} // namespace

RestorePlan planRestore(const QList<ArchiveEntry> &entries, const QList<NotebookFile> &current,
                        const QStringList &currentFolders, const QHash<VaultPath, quint32> &crcOf, bool caseInsensitive,
                        qint64 maxEntryBytes)
{
    RestorePlan plan;
    const TargetLookup lookup(current, entries, caseInsensitive);
    QSet<VaultPath> kept;
    const auto folderKey = [caseInsensitive](const QString &path) {
        return caseInsensitive ? path.toCaseFolded() : path;
    };
    QHash<QString, QString> folders;
    for (const QString &folder : currentFolders)
        folders.insert(folderKey(folder), folder);
    QSet<VaultPath> blockers;
    const auto noteBlockers = [&](const VaultPath &path, bool entryIsFile) {
        // A file of the notebook where the entry needs a folder…
        for (qsizetype slash = path.indexOf(u'/'); slash > 0; slash = path.indexOf(u'/', slash + 1)) {
            if (const NotebookFile *file = lookup.find(path.first(slash)))
                blockers.insert(file->path);
        }
        // … and for a file, a folder of the notebook where it would go.
        if (entryIsFile && !lookup.find(path)) {
            if (const auto folder = folders.constFind(folderKey(path)); folder != folders.cend())
                blockers.insert(*folder);
        }
    };

    QList<PlannedWrite> identical;
    for (const ArchiveEntry &entry : entries) {
        if (entry.folder) {
            noteBlockers(entry.path, false);
            if (const NotebookFile *file = lookup.find(entry.path))
                blockers.insert(file->path);
            if (!folders.contains(folderKey(entry.path)))
                plan.foldersToCreate.append(entry.path);
            continue;
        }
        const NotebookFile *target = lookup.find(entry.path);
        if (target)
            kept.insert(target->path);
        if (entry.size > maxEntryBytes) {
            plan.tooLarge.append(entry.path);
            continue;
        }
        if (!target) {
            noteBlockers(entry.path, true);
            plan.toRecreate.append(PlannedWrite{entry, entry.path});
            plan.bytesToWrite += entry.size;
            continue;
        }
        const auto crc = crcOf.constFind(target->path);
        if (target->size == entry.size && crc != crcOf.cend() && *crc == entry.crc32) {
            identical.append(PlannedWrite{entry, target->path});
        } else {
            plan.toChange.append(PlannedWrite{entry, target->path});
            plan.bytesToWrite += entry.size;
        }
    }
    for (const NotebookFile &file : current) {
        if (!kept.contains(file.path) && file.path != vaultConfigPath())
            plan.toTrash.append(file.path);
    }
    plan.removeFirst = blockers.values();
    plan.removeFirst.sort();
    // A blocker goes with all it holds: what is identical below it must be written back.
    for (const PlannedWrite &write : std::as_const(identical)) {
        if (isInsideAny(write.target, plan.removeFirst)) {
            plan.toRecreate.append(write);
            plan.bytesToWrite += write.entry.size;
        } else {
            ++plan.identical;
        }
    }
    return plan;
}

QList<NotebookFile> notebookFiles(const Vault &vault)
{
    QList<NotebookFile> out;
    const Sources sources = collectSources(vault);
    out.reserve(sources.files.size());
    for (const SourceFile &file : sources.files)
        out.append(NotebookFile{file.path, file.absolutePath, file.size});
    return out;
}

QStringList notebookFolders(const Vault &vault)
{
    QStringList out;
    const QList<FileStat> folders = vault.folders();
    out.reserve(folders.size());
    for (const FileStat &folder : folders)
        out.append(folder.path);
    return out;
}

// ---- Checking ---------------------------------------------------------------------------------

namespace detail {

class CheckTask : public Task {
public:
    CheckTask(QString archive, QList<NotebookFile> current, QStringList folders, RestoreCheckOptions options)
        : m_archive(std::move(archive))
        , m_current(std::move(current))
        , m_folders(std::move(folders))
        , m_options(std::move(options))
    { }

    bool step(int budgetMs) override
    {
        QElapsedTimer clock;
        clock.start();
        while (!advance()) {
            if (clock.elapsed() >= budgetMs)
                return false;
        }
        m_reader.reset(); // the archive is closed as soon as the check is over
        return true;
    }
    void cancel() { m_cancel.store(true); }
    const RestoreCheck &result() const { return m_result; }

private:
    enum class Phase { Entries, Hash, Verify };

    /// One piece of the check: the entries, a file's checksum or an entry inflated. True when over.
    bool advance()
    {
        if (m_cancel.load()) {
            m_result.cancelled = true;
            return true;
        }
        switch (m_phase) {
        case Phase::Entries: readEntries(); return !m_result.ok();
        case Phase::Hash:
            if (m_next < m_toHash.size()) {
                hashNext();
                return false;
            }
            m_result.plan = planRestore(m_entries, m_current, m_folders, m_crcs, m_caseInsensitive);
            applyManifest();
            m_toVerify = m_result.plan.toChange + m_result.plan.toRecreate;
            m_next = 0;
            m_phase = Phase::Verify;
            return false;
        case Phase::Verify:
            if (m_next >= m_toVerify.size())
                return true;
            if (m_reader->verify(m_toVerify.at(m_next).entry, &m_cancel) || m_cancel.load()) {
                ++m_next;
                return false;
            }
            m_result.problem = ArchiveProblem::Damaged;
            m_result.detail = m_toVerify.at(m_next).entry.path;
            return true;
        }
        return true;
    }

    void readEntries()
    {
        m_reader = std::make_unique<ArchiveReader>(m_archive);
        const ArchiveInfo info = m_reader->readEntries(m_options.limits);
        if (!info.ok()) {
            m_result.problem = info.problem;
            m_result.detail = info.detail;
            return;
        }
        m_entries = info.entries;
        // Restoring an archive without a single file of a notebook would only empty this one.
        const bool hasFiles = std::ranges::any_of(std::as_const(m_entries), [](const ArchiveEntry &entry) {
            return !entry.folder && entry.path != vaultConfigPath();
        });
        if (!hasFiles) {
            m_result.problem = ArchiveProblem::NoFiles;
            m_result.detail = u"no files in the archive"_s;
            return;
        }
        if (m_options.caseInsensitive) // (not value_or(): that would probe the storage all the same)
            m_caseInsensitive = *m_options.caseInsensitive;
        else
            m_caseInsensitive = !m_options.notebookRoot.isEmpty() && isCaseInsensitiveFolder(m_options.notebookRoot);
        if (m_caseInsensitive) {
            if (const std::optional<QString> collision = caseCollision(m_entries)) {
                m_result.problem = ArchiveProblem::CaseCollision;
                m_result.detail = u"“%1” collides on this storage"_s.arg(*collision);
                return;
            }
        }
        const TargetLookup lookup(m_current, m_entries, m_caseInsensitive);
        for (const ArchiveEntry &entry : std::as_const(m_entries)) {
            const NotebookFile *target = entry.folder ? nullptr : lookup.find(entry.path);
            if (target && target->size == entry.size && entry.size <= MaxRestoredEntryBytes)
                m_toHash.append(*target);
        }
        m_phase = Phase::Hash;
    }

    /// A backup that left files out (or does not say) cannot tell what the notebook should not
    /// hold: of the trash only what stands in the way of its files stays in the plan.
    void applyManifest()
    {
        const BackupManifest manifest = readBackupManifest(m_archive);
        if (manifest.valid && manifest.complete)
            return;
        RestorePlan &plan = m_result.plan;
        plan.incomplete = true;
        plan.toTrash.removeIf([&plan](const VaultPath &path) { return !isInsideAny(path, plan.removeFirst); });
    }

    void hashNext()
    {
        const NotebookFile &file = m_toHash.at(m_next++);
        if (const std::optional<quint32> crc = fileCrc32(file.absolutePath, &m_cancel))
            m_crcs.insert(file.path, *crc);
    }

    QString m_archive;
    QList<NotebookFile> m_current;
    QStringList m_folders;
    RestoreCheckOptions m_options;
    bool m_caseInsensitive = false;
    std::atomic_bool m_cancel{false};
    Phase m_phase = Phase::Entries;
    std::unique_ptr<ArchiveReader> m_reader;
    QList<ArchiveEntry> m_entries;
    QList<NotebookFile> m_toHash; ///< the notebook's files of an entry's size
    QHash<VaultPath, quint32> m_crcs;
    QList<PlannedWrite> m_toVerify;
    qsizetype m_next = 0;
    RestoreCheck m_result;
};

} // namespace detail

RestoreChecker::RestoreChecker(const notebook::WorkerPool &pool, QObject *parent)
    : QObject(parent), m_runner(std::make_unique<detail::TaskRunner>(pool))
{
    qRegisterMetaType<RestoreCheck>();
}

RestoreChecker::~RestoreChecker()
{
    cancel();
}

void RestoreChecker::start(const QString &archivePath, const QList<NotebookFile> &current,
                           const QStringList &currentFolders, const RestoreCheckOptions &options)
{
    if (m_task)
        return;
    m_task = std::make_shared<detail::CheckTask>(archivePath, current, currentFolders, options);
    m_runner->start(m_task, [this] {
        const RestoreCheck check = m_task->result();
        m_task.reset();
        Q_EMIT finished(check);
    });
}

void RestoreChecker::cancel()
{
    if (m_task)
        m_task->cancel();
}

bool RestoreChecker::isRunning() const
{
    return m_task != nullptr;
}

// ---- Applying ---------------------------------------------------------------------------------

/// One read-ahead: the next files' bytes, read off the owner's thread.
struct RestoreApplier::ReadBatch : detail::Task {
    std::shared_ptr<ArchiveReader> reader;
    std::shared_ptr<std::atomic_bool> cancel;
    QList<PlannedWrite> checkFirst; ///< the first batch: every write of the plan
    QList<PlannedWrite> writes;
    QList<std::optional<QByteArray>> bytes;
    QString problem;

    bool step(int budgetMs) override
    {
        QElapsedTimer clock;
        clock.start();
        if (const QList<PlannedWrite> planned = std::exchange(checkFirst, {}); !planned.isEmpty()) {
            // The file was checked before the user confirmed: it must still be that file.
            const ArchiveInfo info = reader->readEntries();
            if (!info.ok()) {
                problem = info.detail;
                return true;
            }
            const auto unchanged = [&info](const PlannedWrite &write) {
                const ArchiveEntry now = info.entries.value(write.entry.index); // (out of range: an empty one)
                return now.path == write.entry.path && now.crc32 == write.entry.crc32 && now.size == write.entry.size;
            };
            if (!std::ranges::all_of(planned, unchanged)) {
                problem = u"the backup changed since it was checked"_s;
                return true;
            }
        }
        while (bytes.size() < writes.size()) {
            if (cancel->load())
                return true;
            bytes.append(reader->read(writes.at(bytes.size()).entry));
            if (clock.elapsed() >= budgetMs)
                return bytes.size() >= writes.size();
        }
        return true;
    }
};

RestoreApplier::RestoreApplier(Vault &vault, const notebook::WorkerPool &pool, QObject *parent)
    : QObject(parent), m_vault(vault), m_runner(std::make_unique<detail::TaskRunner>(pool))
{
    qRegisterMetaType<RestoreResult>();
    m_timer = new QTimer(this);
    // 1 ms, not 0: a repeating 0 ms timer stalls Qt's timers in the browser (docs/WEB.md).
    m_timer->setInterval(1ms);
}

RestoreApplier::~RestoreApplier()
{
    m_cancel->store(true);
}

void RestoreApplier::start(const RestorePlan &plan, const QString &archivePath)
{
    if (m_running)
        return;
    m_running = true;
    m_plan = plan;
    m_writes = plan.toChange + plan.toRecreate;
    m_reader = std::make_shared<ArchiveReader>(archivePath);
    m_readerChecked = false;
    m_nextRead = m_nextWrite = m_nextTrash = 0;
    m_result = RestoreResult();
    m_cancel->store(false);
    removeBlockers();
}

void RestoreApplier::cancel()
{
    m_cancel->store(true);
}

qreal RestoreApplier::progress() const
{
    const qsizetype total = m_writes.size() + m_plan.toTrash.size();
    return total > 0 ? qreal(m_nextWrite + m_nextTrash) / qreal(total) : 1.0;
}

void RestoreApplier::removeBlockers()
{
    for (const VaultPath &path : std::as_const(m_plan.removeFirst)) {
        Q_EMIT aboutToRemove(path);
        if (!m_vault.remove(path, ChangeSource::Import))
            m_result.failed.append(path);
    }
    readNextBatch();
}

void RestoreApplier::readNextBatch()
{
    if (m_cancel->load()) {
        m_result.cancelled = true;
        finish();
        return;
    }
    if (m_nextRead >= m_writes.size()) {
        createFolders();
        return;
    }
    if (!m_vault.isStorageAvailable()) {
        m_result.detail = u"the notebook's storage is gone"_s;
        finish();
        return;
    }
    m_batch = std::make_shared<ReadBatch>();
    m_batch->reader = m_reader;
    m_batch->cancel = m_cancel;
    if (!std::exchange(m_readerChecked, true))
        m_batch->checkFirst = m_writes;
    qint64 bytes = 0;
    while (m_nextRead < m_writes.size() && m_batch->writes.size() < BatchFiles) {
        const PlannedWrite &write = m_writes.at(m_nextRead);
        if (!m_batch->writes.isEmpty() && bytes + write.entry.size > BatchBytes)
            break;
        bytes += write.entry.size;
        m_batch->writes.append(write);
        ++m_nextRead;
    }
    m_runner->start(m_batch, [this] {
        if (!m_batch->problem.isEmpty()) {
            m_result.detail = m_batch->problem;
            finish();
            return;
        }
        m_batch->bytes.resize(m_batch->writes.size());
        m_timer->disconnect(this);
        connect(m_timer, &QTimer::timeout, this, &RestoreApplier::writeSlice);
        m_timer->start();
    });
}

void RestoreApplier::writeSlice()
{
    QElapsedTimer clock;
    clock.start();
    const qsizetype base = m_nextRead - m_batch->writes.size();
    while (clock.elapsed() < OwnerSliceMs) {
        if (m_cancel->load()) {
            m_result.cancelled = true;
            finish();
            return;
        }
        const qsizetype i = m_nextWrite - base;
        if (i >= m_batch->writes.size()) {
            m_timer->stop();
            m_batch.reset();
            readNextBatch();
            return;
        }
        const PlannedWrite &write = m_batch->writes.at(i);
        std::optional<QByteArray> &bytes = m_batch->bytes[i];
        if (!bytes || !writeOne(write, *bytes))
            m_result.failed.append(write.target);
        else if (m_nextWrite < m_plan.toChange.size())
            ++m_result.changed;
        else
            ++m_result.recreated;
        bytes.reset(); // the memory goes as soon as it is written
        ++m_nextWrite;
    }
}

bool RestoreApplier::writeOne(const PlannedWrite &write, const QByteArray &bytes)
{
    // A file that exists but cannot be read is never replaced: neither History nor a safety
    // backup could have its bytes. The vault snapshots what it holds in memory; what a restore
    // replaces is recorded from the disk. Binary files have no History: the safety backup holds them.
    if (const std::optional<FileStat> stat = m_vault.stat(write.target); stat && !stat->isDir) {
        bool read = false;
        QByteArray current;
        if (stat->size <= MaxHistoryBytes) {
            current = m_vault.readBinary(write.target, &read);
        } else {
            QFile file(m_vault.absolutePath(write.target));
            read = file.open(QIODevice::ReadOnly);
        }
        if (!read) {
            qCWarning(logging::lcVault).noquote() << u"Restore: %1 cannot be read: not replaced"_s.arg(write.target);
            return false;
        }
        History *history = m_vault.history();
        if (history && stat->size <= MaxHistoryBytes && current != bytes && !current.contains('\0')
            && !history->snapshotBytes(write.target, current, changeSourceName(ChangeSource::Import))) {
            qCWarning(logging::lcVault).noquote()
                    << u"Restore: no history of %1: %2"_s.arg(write.target, history->errorString());
            return false;
        }
    }
    if (!m_vault.writeBinary(write.target, bytes, ChangeSource::Import)) {
        qCWarning(logging::lcVault).noquote() << u"Restore: %1"_s.arg(m_vault.errorString());
        return false;
    }
    return true;
}

void RestoreApplier::createFolders()
{
    for (const QString &folder : std::as_const(m_plan.foldersToCreate)) {
        if (m_vault.createFolder(folder))
            ++m_result.foldersCreated;
        else
            m_result.failed.append(folder);
    }
    if (!m_result.failed.isEmpty()) {
        // What the backup holds is not all back: nothing else leaves the notebook.
        m_result.trashSkipped = !m_plan.toTrash.isEmpty();
        finish();
        return;
    }
    m_timer->disconnect(this);
    connect(m_timer, &QTimer::timeout, this, &RestoreApplier::trashSlice);
    m_timer->start();
}

void RestoreApplier::trashSlice()
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < OwnerSliceMs) {
        if (m_cancel->load()) {
            m_result.cancelled = true;
            finish();
            return;
        }
        if (m_nextTrash >= m_plan.toTrash.size()) {
            finish();
            return;
        }
        // Vault::remove(), not notebook::deletePath(): that one lists the whole trash for each
        // file to name its entry, which nobody needs here.
        const VaultPath &path = m_plan.toTrash.at(m_nextTrash++);
        // Already there with its blocker; what the backup put in its place is not to be touched.
        const bool gone = isInsideAny(path, m_plan.removeFirst);
        if (!gone)
            Q_EMIT aboutToRemove(path);
        if (gone || m_vault.remove(path, ChangeSource::Import))
            ++m_result.trashed;
        else
            m_result.failed.append(path);
    }
}

void RestoreApplier::finish()
{
    m_timer->stop();
    m_timer->disconnect(this);
    m_batch.reset();
    m_reader.reset();
    m_running = false;
    Q_EMIT finished(m_result);
}

} // namespace brulion::backup
