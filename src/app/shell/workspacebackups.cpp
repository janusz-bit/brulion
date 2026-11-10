#include "workspacebackups.h"

#include "documentpaths.h"

#include "dates/dates.h"
#include "log/categories.h"
#include "log/slowscope.h"
#include "vault/vault.h"

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif
#ifdef Q_OS_WASM
#include "wasm/wasmfiles.h"
#endif

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

const QString kEnabled = u"backup/enabled"_s;
const QString kInterval = u"backup/interval"_s;
const QString kKeep = u"backup/keep"_s;
const QString kFolder = u"backup/folder"_s;
const QString kLast = u"backup/last"_s;           // { time, file, bytes, fingerprint, folder }
const QString kStatus = u"backup/status"_s;       // { failure, detail } | { skipped }
const QString kRestoring = u"backup/restoring"_s; // { backup, safety, time }: a restore being applied

constexpr int DefaultFirstCheckMs = 30 * 1000;
constexpr int DefaultPeriodMs = 60 * 60 * 1000;
constexpr int StoredTimeoutMs = 60 * 1000;
constexpr int ProgressMs = 200;
/// Room left on the notebook's storage after a restore (bytes it would write are added).
constexpr qint64 RestoreMarginBytes = 16 << 20;

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString iso(const QDateTime &time)
{
    return time.toString(u"yyyy-MM-ddTHH:mm:ss"_s);
}

QDateTime fromIso(const QString &text)
{
    return QDateTime::fromString(text, u"yyyy-MM-ddTHH:mm:ss"_s);
}

/// "today 14:30" for `time`, to the minute, as seen on `today` in the UI language `language`.
QString whenText(const QDateTime &time, const QString &language, QDate today)
{
    const auto locale = language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    return dates::formatHuman(DateSpec{time.date(), QTime(time.time().hour(), time.time().minute())}, locale, today);
}

} // namespace

WorkspaceBackups::WorkspaceBackups(std::function<QDateTime()> clock, QObject *parent)
    : QObject(parent), m_clock(std::move(clock))
{
#ifdef Q_OS_WASM
    m_browser = true;
#endif
    m_firstCheck.setSingleShot(true);
    m_firstCheck.setInterval(DefaultFirstCheckMs);
    connect(&m_firstCheck, &QTimer::timeout, this, &WorkspaceBackups::check);
    m_periodicCheck.setInterval(DefaultPeriodMs);
    connect(&m_periodicCheck, &QTimer::timeout, this, &WorkspaceBackups::check);
    m_progressTimer.setInterval(ProgressMs);
    connect(&m_progressTimer, &QTimer::timeout, this, &WorkspaceBackups::progressChanged);
    m_storedTimeout.setSingleShot(true);
    m_storedTimeout.setInterval(StoredTimeoutMs);
    connect(&m_storedTimeout, &QTimer::timeout, this, [this] {
        if (m_waitingForStored)
            storageRefused(u"no answer from the browser's storage"_s);
    });
}

WorkspaceBackups::~WorkspaceBackups()
{
    detach();
}

void WorkspaceBackups::attach(Vault *vault, const Attachment &attachment)
{
    detach();
    m_vault = vault;
    m_attachment = attachment;
    if (m_paused || attachment.defaultFolder.isEmpty()) {
        Q_EMIT stateChanged();
        return;
    }
    m_writer = std::make_unique<backup::BackupWriter>(attachment.pool);
    connect(m_writer.get(), &backup::BackupWriter::finished, this, &WorkspaceBackups::onWritten);
    m_checker = std::make_unique<backup::RestoreChecker>(attachment.pool);
    connect(m_checker.get(), &backup::RestoreChecker::finished, this, &WorkspaceBackups::onChecked);
    m_applier = std::make_unique<backup::RestoreApplier>(*vault, attachment.pool);
    connect(m_applier.get(), &backup::RestoreApplier::finished, this, &WorkspaceBackups::onApplied);
    connect(m_applier.get(), &backup::RestoreApplier::aboutToRemove, this, &WorkspaceBackups::aboutToRemove);
    m_work = std::make_unique<backup::FolderWork>(attachment.pool);
    Q_EMIT stateChanged();
    refresh();
}

void WorkspaceBackups::detach()
{
    m_firstCheck.stop();
    m_periodicCheck.stop();
    m_progressTimer.stop();
    m_storedTimeout.stop();
    if (m_applier && m_applier->isRunning()) {
        // The marker stays: the next open of this notebook says so again.
        qCWarning(logging::lcIo).noquote()
                << u"Backup: the notebook closed during a restore: the rest was not applied"_s;
        Q_EMIT messageRequested(tr("The notebook was closed during a restore, so it is only partly restored. "
                                   "Open it again and see Settings → Backups."),
                                u"warning"_s);
    }
    releaseSync();
    ++m_holdGeneration;
    m_writer.reset(); // cancels: their jobs hold no reference to the vault or to this
    m_checker.reset();
    m_applier.reset(); // before the vault
    m_work.reset();
    m_vault = nullptr;
    m_attachment = {};
    m_started = false;
    m_op = Op::None;
    m_plan = {};
    m_planReady = false;
    m_restoreName.clear();
    m_safetyName.clear();
    m_waitingForStored = false;
    m_unconfirmed.clear();
    m_toldFailures.clear();
    ++m_listGeneration;
    if (!m_backups.isEmpty()) {
        m_backups.clear();
        Q_EMIT backupsChanged();
    }
    Q_EMIT stateChanged();
}

void WorkspaceBackups::start()
{
    if (!available())
        return;
    m_started = true;
    m_firstCheck.start();
    m_periodicCheck.start();
    if (interrupted()) {
        qCWarning(logging::lcIo).noquote()
                << u"Backup: a restore of this notebook did not finish: %1"_s.arg(QString::fromUtf8(
                           QJsonDocument(m_vault->store()->get(kRestoring).toObject()).toJson(QJsonDocument::Compact)));
        Q_EMIT messageRequested(statusText(), u"warning"_s);
    }
}

void WorkspaceBackups::setStorageUnavailable(const QString &why)
{
    qCWarning(logging::lcIo).noquote() << u"Backup: the browser's storage of backups is unavailable: %1"_s.arg(why);
    m_storageUnavailable = why.isEmpty() ? u"?"_s : why;
    Q_EMIT stateChanged();
}

void WorkspaceBackups::setSyncControl(std::function<void(std::function<void()>)> hold, std::function<void()> release)
{
    m_holdSync = std::move(hold);
    m_releaseSync = std::move(release);
}

void WorkspaceBackups::releaseSync()
{
    if (std::exchange(m_syncHeld, false) && m_releaseSync)
        m_releaseSync();
}

bool WorkspaceBackups::canWrite()
{
    if (!m_storageUnavailable.isEmpty()) {
        Q_EMIT messageRequested(statusText(), u"error"_s);
        return false;
    }
    if (backup::isInsideFolder(folder(), m_vault->rootPath())) {
        qCWarning(logging::lcIo).noquote() << u"Backup folder %1 lies inside the notebook"_s.arg(folder());
        setFailure(Failure::InsideNotebook, {});
        tellFailure(Failure::InsideNotebook, u"error"_s, !m_runAutomatic);
        return false;
    }
    return true;
}

QVariantMap WorkspaceBackups::listedBackup(const QString &name) const
{
    for (const QVariant &entry : m_backups) {
        if (QVariantMap map = entry.toMap(); map.value(u"name"_s).toString() == name)
            return map;
    }
    return {};
}

bool WorkspaceBackups::mayUse(const QString &name, bool allowForeign) const
{
    if (!available() || running() || !backup::backupFileTime(name, m_attachment.notebookName))
        return false;
    const QVariantMap listed = listedBackup(name);
    return !listed.isEmpty() && (allowForeign || !listed.value(u"foreign"_s).toBool());
}

bool WorkspaceBackups::interrupted() const
{
    return m_vault && m_vault->store()->contains(kRestoring);
}

void WorkspaceBackups::dismissInterrupted()
{
    if (m_vault) {
        m_vault->store()->remove(kRestoring);
        Q_EMIT stateChanged();
    }
}

void WorkspaceBackups::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    Q_EMIT stateChanged();
}

void WorkspaceBackups::setCheckTimings(int firstMs, int periodMs)
{
    m_firstCheck.setInterval(firstMs);
    m_periodicCheck.setInterval(periodMs);
}

void WorkspaceBackups::applicationActive()
{
    if (m_started)
        check();
}

void WorkspaceBackups::setBrowser(bool browser)
{
    m_browser = browser;
    Q_EMIT stateChanged();
}

void WorkspaceBackups::setLanguage(const QString &language)
{
    m_language = language;
    Q_EMIT stateChanged();
    refresh();
}

bool WorkspaceBackups::notebookStorageOk() const
{
    return m_vault && m_vault->isStorageAvailable() && m_vault->scanStatus().isOk();
}

void WorkspaceBackups::check()
{
    // A half-restored notebook is backed up by hand only: copies of it would push good ones out.
    if (!available() || running() || !enabled() || !m_started || interrupted() || !m_storageUnavailable.isEmpty()) {
        Q_EMIT idle();
        return;
    }
    backup::Settings settings;
    settings.enabled = true;
    settings.interval = interval() == u"weekly" ? backup::Interval::Weekly : backup::Interval::Daily;
    settings.keep = keep();
    const QJsonObject last = m_vault->store()->get(kLast).toObject();
    const QDateTime lastTime = fromIso(last.value(u"time"_s).toString());
    if (!backup::isDue(settings.interval, lastTime, m_clock())) {
        Q_EMIT idle();
        return;
    }
    if (!notebookStorageOk()) {
        // Never a backup of what cannot be seen: it would push good ones out.
        qCInfo(logging::lcIo).noquote() << u"Backup: skipped, the notebook's storage is not available"_s;
        Q_EMIT idle();
        return;
    }
    // Whether the last backup is still there may need a slow or missing drive: off this thread.
    const QString lastPath = lastFile();
    auto exists = std::make_shared<bool>(false);
    inBackground([lastPath, exists] { *exists = !lastPath.isEmpty() && QFileInfo::exists(lastPath); },
                 [this, settings, last, lastTime, exists] {
                     if (!available() || running()) {
                         Q_EMIT idle();
                         return;
                     }
                     const backup::Sources sources = backup::collectSources(*m_vault);
                     const backup::ScheduleState state{.lastBackup = lastTime,
                                                       .lastFingerprint = last.value(u"fingerprint"_s).toString(),
                                                       .lastFileExists = *exists};
                     const QDateTime checkedAt = m_clock();
                     const QString fingerprint = backup::fingerprint(sources);
                     const backup::Decision decision =
                             backup::decide(settings, state, fingerprint, sources.notebookFileCount(), checkedAt);
                     if (decision == backup::Decision::Unchanged)
                         qCDebug(logging::lcIo) << "Backup: nothing changed since the last one";
                     if (decision != backup::Decision::Run) {
                         Q_EMIT idle();
                         return;
                     }
                     m_runAutomatic = true;
                     if (canWrite())
                         startWrite(Op::Backup, sources);
                     else
                         Q_EMIT idle();
                 });
}

bool WorkspaceBackups::createNow()
{
    if (!available() || running())
        return false;
    if (!notebookStorageOk()) {
        Q_EMIT messageRequested(tr("The notebook's folder is not available, so no backup was made."), u"error"_s);
        return false;
    }
    const backup::Sources sources = backup::collectSources(*m_vault);
    if (sources.notebookFileCount() == 0) {
        Q_EMIT messageRequested(tr("This notebook is empty: there is nothing to back up."), u"info"_s);
        return false;
    }
    m_runAutomatic = false;
    if (!canWrite())
        return false;
    startWrite(Op::Backup, sources);
    return true;
}

void WorkspaceBackups::cancel()
{
    switch (m_op) {
    case Op::Backup: m_writer->cancel(); return;
    case Op::Safety:
        if (!m_waitingForStored) {
            m_writer->cancel();
            return;
        }
        // Written, but not in the browser's storage yet: the restore has not begun.
        m_waitingForStored = false;
        m_storedTimeout.stop();
        break;
    case Op::Check: m_checker->cancel(); return;
    case Op::WaitForSync: ++m_holdGeneration; break;
    case Op::Restore: m_applier->cancel(); return;
    case Op::None: return;
    }
    setOp(Op::None);
    abortRestore(tr("The restore was cancelled. Nothing was changed."));
}

void WorkspaceBackups::startWrite(Op op, const backup::Sources &sources)
{
    const QString folderPath = folder();
    const QString name = m_attachment.notebookName;
    m_runTime = m_clock();
    m_runFingerprint = backup::fingerprint(sources);
    const backup::WriteOptions options{.targetPath = backup::freeBackupPath(folderPath, name, m_runTime),
                                       .notebookId = m_vault->id(),
                                       .createFolder = usingDefaultFolder(),
                                       .workFolder = m_attachment.workFolder,
                                       .maxBytes = m_attachment.maxBytes,
                                       .onRead = m_readHook};
    setOp(op);
    m_writer->start(sources, options);
    inBackground([folderPath, name, now = m_runTime] { backup::removeStalePartials(folderPath, name, now); }, {});
}

void WorkspaceBackups::onWritten(backup::WriteResult result)
{
    const bool safety = m_op == Op::Safety;
    if (result.ok() && !result.complete() && !notebookStorageOk()) {
        // The drive went under the run: what it wrote is a fragment, not a copy of the notebook.
        QFile::remove(result.path);
        result.error = backup::WriteError::NothingArchived;
        result.detail = u"the notebook's storage went during the run"_s;
        result.path.clear();
    }
    if (result.ok()) {
        afterWrite(result);
        return;
    }
    if (result.error == backup::WriteError::Cancelled) {
        qCInfo(logging::lcIo) << "Backup: cancelled";
        setOp(Op::None);
        if (safety)
            abortRestore(tr("The restore was cancelled. Nothing was changed."));
        return;
    }
    qCWarning(logging::lcIo).noquote() << u"Backup failed in %1: %2"_s.arg(folder(), result.detail);
    const Failure failure = [&] {
        switch (result.error) {
        case backup::WriteError::FolderMissing: return Failure::FolderMissing;
        case backup::WriteError::FolderUnwritable: return Failure::FolderUnwritable;
        case backup::WriteError::TooLarge: return Failure::TooLarge;
        case backup::WriteError::NothingArchived:
            return notebookStorageOk() ? Failure::NothingArchived : Failure::NotebookUnavailable;
        default: return Failure::WriteFailed;
        }
    }();
    setOp(Op::None);
    setFailure(failure, result.detail);
    if (safety) {
        abortRestore(
                tr("The safety backup could not be made, so nothing was restored. %1").arg(failureMessage(failure)));
        return;
    }
    tellFailure(failure, u"warning"_s, !m_runAutomatic);
}

void WorkspaceBackups::afterWrite(const backup::WriteResult &result)
{
    const bool safety = m_op == Op::Safety;
    const bool complete = result.complete();
    const QString name = QFileInfo(result.path).fileName();
    m_lastBeforeUnconfirmed = m_vault->store()->get(kLast);
    // Without a fingerprint an incomplete copy never counts as "nothing changed": the next due
    // check tries again.
    m_vault->store()->set(kLast, QJsonObject{{u"time"_s, iso(m_runTime)},
                                             {u"file"_s, name},
                                             {u"bytes"_s, double(result.bytes)},
                                             {u"fingerprint"_s, complete ? m_runFingerprint : QString()},
                                             {u"folder"_s, folder()}});
    const int leftOut = int(result.skipped.size() + result.vanished.size());
    clearFailure(leftOut);
    if (safety)
        m_safetyName = name;
    // One arg() call: a "%2" in the name is never read as a placeholder.
    qCInfo(logging::lcIo).noquote() << u"Backup written: %1, %2 files, %3%4%5"_s.arg(
            name, QString::number(result.files), logging::formatBytes(result.bytes),
            result.skipped.isEmpty() ? QString() : u", left out: "_s + result.skipped.join(u", "_s),
            result.vanished.isEmpty() ? QString() : u", gone meanwhile: "_s + result.vanished.join(u", "_s));
    Q_EMIT backupWritten(name);
    if (leftOut > 0 && !safety) {
        Q_EMIT messageRequested(tr("The backup was made without %n file(s) that could not be read or disappeared while "
                                   "it ran. Older backups are kept.",
                                   "", leftOut),
                                u"warning"_s);
    } else if (!safety && !m_runAutomatic) {
        Q_EMIT messageRequested(tr("Backup created."), u"success"_s);
    }
    if (safety) {
        // What the plan overwrites or trashes must be in the safety backup, or it would be lost.
        QSet<QString> touched;
        for (const backup::PlannedWrite &write : std::as_const(m_plan.toChange))
            touched.insert(write.target);
        for (const VaultPath &path : std::as_const(m_plan.toTrash))
            touched.insert(path);
        for (const QString &path : result.skipped) {
            const auto within = [&](const VaultPath &b) { return path == b || path.startsWith(b + u'/'); };
            if (touched.contains(path) || std::ranges::any_of(std::as_const(m_plan.removeFirst), within)) {
                qCWarning(logging::lcIo).noquote()
                        << u"Backup: restore aborted: %1 is not in the safety backup"_s.arg(path);
                setOp(Op::None);
                abortRestore(tr("Nothing was restored: “%1” could not be read for the safety backup, and the restore "
                                "would replace it.")
                                     .arg(path));
                refresh();
                return;
            }
        }
    }
    if (m_deferUntilStored) {
        // Pruning (and a restore) wait until the browser's storage has it.
        m_unconfirmed = name;
        m_unconfirmedComplete = complete;
        if (safety) {
            m_waitingForStored = true;
            m_storedTimeout.start();
        } else {
            setOp(Op::None);
        }
        refresh();
        return;
    }
    if (!safety)
        setOp(Op::None);
    pruneThenRefresh(complete);
    if (safety)
        startApply();
}

void WorkspaceBackups::storageFlushed()
{
    if (m_unconfirmed.isEmpty())
        return;
    m_unconfirmed.clear();
    pruneThenRefresh(m_unconfirmedComplete);
    if (m_waitingForStored) {
        m_waitingForStored = false;
        m_storedTimeout.stop();
        startApply();
    }
}

void WorkspaceBackups::storageRefused(const QString &why)
{
    qCWarning(logging::lcIo).noquote() << u"Backup: the browser's storage refused the backups: %1"_s.arg(why);
    if (!m_unconfirmed.isEmpty() && available()) {
        // The transaction of this mount failed as a whole: IndexedDB still has the older copies.
        QFile::remove(QDir(folder()).filePath(m_unconfirmed));
        m_vault->store()->set(kLast, m_lastBeforeUnconfirmed); // undefined: the key goes
        m_unconfirmed.clear();
    }
    setFailure(Failure::StorageRefused, why);
    if (m_waitingForStored) {
        m_waitingForStored = false;
        m_storedTimeout.stop();
        setOp(Op::None);
        abortRestore(tr("The safety backup could not be made, so nothing was restored. %1")
                             .arg(failureMessage(Failure::StorageRefused)));
    } else {
        tellFailure(Failure::StorageRefused, u"warning"_s);
    }
    refresh();
}

void WorkspaceBackups::pruneThenRefresh(bool pruneComplete)
{
    if (!available())
        return;
    QSet<QString> protect;
    if (const QString name = QFileInfo(lastFile()).fileName(); !name.isEmpty())
        protect.insert(name);
    if (!m_restoreName.isEmpty())
        protect.insert(m_restoreName);
    // A half-done restore's way back, until the user has dealt with it.
    if (const QString safety = m_vault->store()->get(kRestoring).toObject().value(u"safety"_s).toString();
        !safety.isEmpty())
        protect.insert(safety);
    const QString folderPath = folder();
    const QString notebook = m_attachment.notebookName;
    const QString id = m_vault->id();
    const int limit = keep();
    auto removed = std::make_shared<QStringList>();
    auto failed = std::make_shared<QStringList>();
    inBackground(
            [=] {
                *removed = backup::pruneBackups(folderPath, notebook, limit, id, protect, pruneComplete, failed.get());
            },
            [this, removed, failed] {
                if (!available())
                    return;
                if (!removed->isEmpty())
                    qCInfo(logging::lcIo).noquote() << u"Backup: pruned %1"_s.arg(removed->join(u", "_s));
                if (!failed->isEmpty())
                    qCWarning(logging::lcIo).noquote() << u"Backup: could not prune %1"_s.arg(failed->join(u", "_s));
                refresh();
            });
}

void WorkspaceBackups::refresh()
{
    if (!available())
        return;
    const int generation = ++m_listGeneration;
    const QString folderPath = folder();
    const QString notebook = m_attachment.notebookName;
    const QString notebookId = m_vault->id();
    auto files = std::make_shared<QList<backup::BackupFile>>();
    inBackground([=] { *files = backup::listBackups(folderPath, notebook); },
                 [this, generation, files, notebookId] {
                     if (generation != m_listGeneration)
                         return;
                     QVariantList out;
                     const QDate today = m_clock().date();
                     for (const backup::BackupFile &file : std::as_const(*files)) {
                         out.append(QVariantMap{{u"name"_s, file.fileName},
                                                {u"time"_s, iso(file.time)},
                                                {u"when"_s, whenText(file.time, m_language, today)},
                                                {u"size"_s, file.size},
                                                {u"sizeText"_s, sizeText(file.size)},
                                                {u"foreign"_s, file.notebookId != notebookId},
                                                {u"complete"_s, file.complete}});
                     }
                     m_backups = out;
                     Q_EMIT backupsChanged();
                     Q_EMIT stateChanged(); // canShare, the status' texts
                 });
}

bool WorkspaceBackups::remove(const QString &name, bool allowForeign)
{
    if (!mayUse(name, allowForeign))
        return false;
    const QString path = QDir(folder()).filePath(QFileInfo(name).fileName());
    if (!QFile::remove(path)) {
        Q_EMIT messageRequested(tr("The backup could not be deleted."), u"error"_s);
        return false;
    }
    qCInfo(logging::lcIo).noquote() << u"Backup deleted: %1"_s.arg(name);
    refresh();
    return true;
}

bool WorkspaceBackups::prepareRestore(const QString &name, bool allowForeign)
{
    if (!mayUse(name, allowForeign))
        return false;
    if (!m_storageUnavailable.isEmpty()) {
        Q_EMIT messageRequested(statusText(), u"error"_s);
        return false;
    }
    if (!notebookStorageOk()) {
        Q_EMIT messageRequested(tr("The notebook's folder is not available, so nothing can be restored."), u"error"_s);
        return false;
    }
    m_restoreName = name;
    m_planReady = false;
    setOp(Op::Check);
    backup::RestoreCheckOptions options;
    options.notebookRoot = m_vault->rootPath();
    m_checker->start(QDir(folder()).filePath(name), backup::notebookFiles(*m_vault), backup::notebookFolders(*m_vault),
                     options);
    return true;
}

void WorkspaceBackups::onChecked(const backup::RestoreCheck &check)
{
    setOp(Op::None);
    if (check.cancelled) {
        m_restoreName.clear();
        return;
    }
    if (!check.ok()) {
        qCWarning(logging::lcIo).noquote() << u"Backup %1 refused: %2"_s.arg(m_restoreName, check.detail);
        const QString message = check.problem == backup::ArchiveProblem::Damaged
                                        ? tr("This backup is damaged and cannot be restored. Nothing was changed.")
                                : check.problem == backup::ArchiveProblem::Unreadable
                                        ? tr("This backup cannot be read. Is its folder still available?")
                                : check.problem == backup::ArchiveProblem::NoFiles
                                        ? tr("This backup holds no files of a notebook, so restoring it would only "
                                             "empty this one. Nothing was changed.")
                                : check.problem == backup::ArchiveProblem::CaseCollision
                                        ? tr("This backup holds names that differ only in letter case, which this "
                                             "storage cannot keep apart. Nothing was changed; unpack the ZIP by hand.")
                                        : tr("This file is not a backup Brulion can restore. Nothing was changed.");
        m_restoreName.clear();
        Q_EMIT messageRequested(message, u"error"_s);
        return;
    }
    m_plan = check.plan;
    m_planReady = true;
    const QDateTime time = backup::backupFileTime(m_restoreName, m_attachment.notebookName).value_or(QDateTime());
    Q_EMIT restorePlanReady(QVariantMap{{u"name"_s, m_restoreName},
                                        {u"when"_s, whenText(time, m_language, m_clock().date())},
                                        {u"changed"_s, int(m_plan.toChange.size())},
                                        {u"recreated"_s, int(m_plan.toRecreate.size())},
                                        {u"trashed"_s, int(m_plan.toTrash.size())},
                                        {u"identical"_s, m_plan.identical},
                                        {u"tooLarge"_s, int(m_plan.tooLarge.size())},
                                        {u"hasChanges"_s, m_plan.hasChanges()},
                                        {u"incomplete"_s, m_plan.incomplete},
                                        {u"foreign"_s, listedBackup(m_restoreName).value(u"foreign"_s, true)}});
}

void WorkspaceBackups::dismissRestore()
{
    m_plan = {};
    m_planReady = false;
    m_restoreName.clear();
}

void WorkspaceBackups::confirmRestore()
{
    if (!available() || running() || !m_planReady)
        return;
    m_planReady = false;
    if (!notebookStorageOk()) {
        abortRestore(tr("The notebook's folder is not available, so nothing can be restored."));
        return;
    }
    m_runAutomatic = false;
    if (!canWrite()) {
        dismissRestore();
        Q_EMIT idle();
        return;
    }
    // A restore that cannot fit fails before anything changed.
    const QStorageInfo storage(m_vault->rootPath());
    if (storage.isValid() && storage.bytesAvailable() >= 0
        && m_plan.bytesToWrite + RestoreMarginBytes > storage.bytesAvailable()) {
        abortRestore(
                tr("There is not enough free space for this restore: it needs %1.").arg(sizeText(m_plan.bytesToWrite)));
        return;
    }
    // What editors hold goes into the safety backup too; a running sync ends before anything else.
    Q_EMIT saveOpenEditors();
    holdSyncThenSafetyBackup();
}

void WorkspaceBackups::holdSyncThenSafetyBackup()
{
    const auto proceed = [this] {
        if (!available())
            return;
        m_safetyName.clear();
        if (const backup::Sources sources = backup::collectSources(*m_vault); sources.notebookFileCount() == 0)
            startApply(); // an empty notebook has nothing to save first
        else
            startWrite(Op::Safety, sources);
    };
    if (!m_holdSync) {
        proceed();
        return;
    }
    setOp(Op::WaitForSync);
    m_syncHeld = true;
    m_holdSync([this, proceed, generation = ++m_holdGeneration] {
        if (generation == m_holdGeneration && m_op == Op::WaitForSync)
            proceed();
    });
}

void WorkspaceBackups::startApply()
{
    setOp(Op::Restore);
    // Survives a kill: LocalStore's debounced save would not.
    m_vault->store()->set(
            kRestoring,
            QJsonObject{{u"backup"_s, m_restoreName}, {u"safety"_s, m_safetyName}, {u"time"_s, iso(m_clock())}});
    m_vault->store()->flush();
    qCInfo(logging::lcIo).noquote() << u"Backup: restoring %1 (%2 changed, %3 back, %4 to the trash)"_s.arg(
            m_restoreName, QString::number(m_plan.toChange.size()), QString::number(m_plan.toRecreate.size()),
            QString::number(m_plan.toTrash.size()));
    m_applier->start(m_plan, QDir(folder()).filePath(m_restoreName));
}

void WorkspaceBackups::onApplied(const backup::RestoreResult &result)
{
    setOp(Op::None);
    qCInfo(logging::lcIo).noquote() << u"Backup: restored: %1 changed, %2 back, %3 to the trash, %4 failed %5"_s
                                               .arg(result.changed)
                                               .arg(result.recreated)
                                               .arg(result.trashed)
                                               .arg(result.failed.size())
                                               .arg(result.detail);
    const bool hadSafety = !m_safetyName.isEmpty();
    const QString asItWas = hadSafety ? u" "_s + tr("The safety backup holds the notebook as it was.") : QString();
    QString message;
    QString kind = u"error"_s;
    if (result.cancelled) {
        message = hadSafety ? tr("The restore was stopped: the notebook is only partly restored. The safety backup "
                                 "holds it as it was.")
                            : tr("The restore was stopped: the notebook is only partly restored.");
        kind = u"warning"_s;
    } else if (!result.failed.isEmpty()) {
        message = (result.trashSkipped
                           ? tr("The restore did not finish: %n file(s) could not be written, so nothing else went to "
                                "the trash.",
                                "", int(result.failed.size()))
                           : tr("The restore did not finish: %n file(s) could not be written or moved to the trash.",
                                "", int(result.failed.size())))
                  + asItWas;
    } else if (!result.ok()) {
        message = tr("The restore did not finish: the backup or the notebook's folder became unavailable.") + asItWas;
    } else {
        message = tr("Restored. Replaced notes are in the version history, and files the backup does not hold are in "
                     "the trash.")
                  + (hadSafety ? u" "_s + tr("The safety backup holds everything as it was.") : QString());
        kind = u"success"_s;
    }
    m_vault->store()->remove(kRestoring);
    m_vault->store()->flush();
    m_plan = {};
    m_restoreName.clear();
    m_safetyName.clear();
    releaseSync(); // one sync now: every deletion in one run, before the mass-deletion guard
    Q_EMIT messageRequested(message, kind);
    Q_EMIT stateChanged();
    refresh();
}

void WorkspaceBackups::abortRestore(const QString &message)
{
    m_plan = {};
    m_planReady = false;
    m_restoreName.clear();
    m_safetyName.clear();
    releaseSync();
    Q_EMIT messageRequested(message, u"error"_s);
    Q_EMIT idle();
}

void WorkspaceBackups::setOp(Op op)
{
    if (m_op == op)
        return;
    m_op = op;
    if (op == Op::None)
        m_progressTimer.stop();
    else
        m_progressTimer.start();
    Q_EMIT stateChanged();
    Q_EMIT progressChanged();
    if (op == Op::None)
        Q_EMIT idle();
}

void WorkspaceBackups::setFailure(Failure failure, const QString &detail)
{
    if (!m_vault)
        return;
    m_vault->store()->set(kStatus, QJsonObject{{u"failure"_s, int(failure)}, {u"detail"_s, detail.left(300)}});
    Q_EMIT stateChanged();
}

void WorkspaceBackups::clearFailure(int skipped)
{
    if (skipped > 0)
        m_vault->store()->set(kStatus, QJsonObject{{u"skipped"_s, skipped}});
    else
        m_vault->store()->remove(kStatus);
    m_toldFailures.clear();
    Q_EMIT stateChanged();
}

void WorkspaceBackups::tellFailure(Failure failure, const QString &kind, bool byHand)
{
    if (!byHand && m_toldFailures.contains(int(failure)))
        return;
    m_toldFailures.insert(int(failure));
    Q_EMIT messageRequested(failureMessage(failure), kind);
}

QString WorkspaceBackups::lastFile() const
{
    if (!m_vault)
        return {};
    const QJsonObject last = m_vault->store()->get(kLast).toObject();
    const QString file = last.value(u"file"_s).toString();
    // The name only (a store is no hostile input, but a path never leaves the folder in use).
    return file.isEmpty() || last.value(u"folder"_s).toString() != folder()
                   ? QString()
                   : QDir(folder()).filePath(QFileInfo(file).fileName());
}

void WorkspaceBackups::inBackground(std::function<void()> work, std::function<void()> done)
{
    if (m_work)
        m_work->run(std::move(work), std::move(done));
}

bool WorkspaceBackups::available() const
{
    return m_vault && m_writer && !m_paused;
}

bool WorkspaceBackups::enabled() const
{
    return m_vault && m_vault->store()->get(kEnabled, !m_browser).toBool();
}

void WorkspaceBackups::setEnabled(bool on)
{
    if (!m_vault || on == enabled())
        return;
    m_vault->store()->set(kEnabled, on);
    Q_EMIT stateChanged();
    if (on)
        check();
}

QString WorkspaceBackups::interval() const
{
    return m_vault && m_vault->store()->get(kInterval).toString() == u"weekly" ? u"weekly"_s : u"daily"_s;
}

void WorkspaceBackups::setInterval(const QString &value)
{
    if (!m_vault || value == interval() || (value != u"daily" && value != u"weekly"))
        return;
    m_vault->store()->set(kInterval, value);
    Q_EMIT stateChanged();
    check();
}

int WorkspaceBackups::keep() const
{
    const int fallback = m_browser ? backup::BrowserDefaultKeep : backup::DefaultKeep;
    if (!m_vault)
        return fallback;
    return std::clamp(m_vault->store()->get(kKeep, fallback).toInt(), backup::MinKeep, backup::MaxKeep);
}

void WorkspaceBackups::setKeep(int value)
{
    if (!m_vault || value == keep())
        return;
    m_vault->store()->set(kKeep, std::clamp(value, backup::MinKeep, backup::MaxKeep));
    Q_EMIT stateChanged();
}

QVariantList WorkspaceBackups::keepChoices() const
{
    const QList<int> values = m_browser ? QList<int>{1, 2, 3} : QList<int>{3, 5, 7, 14, 30};
    QVariantList out;
    for (const int value : values)
        out.append(QVariantMap{{u"value"_s, value}, {u"text"_s, QString::number(value)}});
    return out;
}

QString WorkspaceBackups::folder() const
{
    if (!m_vault)
        return {};
    const QString chosen = m_browser ? QString() : m_vault->store()->get(kFolder).toString();
    return chosen.isEmpty() ? m_attachment.defaultFolder : chosen;
}

bool WorkspaceBackups::usingDefaultFolder() const
{
    return folder() == m_attachment.defaultFolder;
}

bool WorkspaceBackups::canShare() const
{
#ifdef Q_OS_ANDROID
    return usingDefaultFolder(); // the share provider serves the app's own files
#else
    return false;
#endif
}

bool WorkspaceBackups::canOpenFolder() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    return false;
#else
    return true;
#endif
}

QString WorkspaceBackups::phase() const
{
    switch (m_op) {
    case Op::None: return {};
    case Op::Backup: return u"backup"_s;
    case Op::Check: return u"check"_s;
    case Op::WaitForSync: return u"sync"_s;
    case Op::Safety: return u"safety"_s;
    case Op::Restore: return u"restore"_s;
    }
    return {};
}

qreal WorkspaceBackups::progress() const
{
    switch (m_op) {
    case Op::Backup:
    case Op::Safety: return m_writer ? m_writer->progress() : 0;
    case Op::Restore: return m_applier ? m_applier->progress() : 0;
    default: return 0;
    }
}

QString WorkspaceBackups::status() const
{
    if (!m_vault)
        return u"none"_s;
    if (interrupted() || !m_storageUnavailable.isEmpty())
        return u"failed"_s;
    const QJsonObject status = m_vault->store()->get(kStatus).toObject();
    if (status.value(u"failure"_s).toInt() != int(Failure::None))
        return u"failed"_s;
    if (status.value(u"skipped"_s).toInt() > 0)
        return u"warning"_s;
    return m_vault->store()->contains(kLast) ? u"ok"_s : u"none"_s;
}

QString WorkspaceBackups::statusText() const
{
    if (!m_vault)
        return {};
    if (interrupted()) {
        const QString safety = m_vault->store()->get(kRestoring).toObject().value(u"safety"_s).toString();
        return safety.isEmpty() ? tr("A restore of this notebook did not finish, so it is only partly restored. Run "
                                     "the restore again.")
                                : tr("A restore of this notebook did not finish, so it is only partly restored. The "
                                     "backup “%1” holds it as it was: restore that one, or run the restore again.")
                                          .arg(safety);
    }
    if (!m_storageUnavailable.isEmpty())
        return tr("This browser's storage of backups could not be read, so backups and restoring are off until the "
                  "page is reloaded. Older backups have not been touched.");
    const QJsonObject status = m_vault->store()->get(kStatus).toObject();
    if (const int failure = status.value(u"failure"_s).toInt(); failure != int(Failure::None))
        return failureMessage(Failure(failure));
    if (const int skipped = status.value(u"skipped"_s).toInt(); skipped > 0)
        return tr("The last backup is missing %n file(s) that could not be read or disappeared while it ran. The event "
                  "log names them. Older complete backups are kept.",
                  "", skipped);
    return {};
}

QString WorkspaceBackups::failureMessage(Failure failure) const
{
    const QString older = tr("Older backups are kept.");
    switch (failure) {
    case Failure::FolderMissing:
        return tr("The last backup failed: the folder %1 is not available. Is the drive connected? %2")
                .arg(QDir::toNativeSeparators(folder()), older);
    case Failure::FolderUnwritable:
#ifdef Q_OS_ANDROID
        return tr("The last backup failed: Brulion cannot write to %1. Allow “All files access” for Brulion in "
                  "Android's settings, or use this device's own folder. %2")
                .arg(folder(), older);
#else
        return tr("The last backup failed: Brulion cannot write to %1. %2")
                .arg(QDir::toNativeSeparators(folder()), older);
#endif
    case Failure::WriteFailed: return tr("The last backup failed while writing; the disk may be full. %1").arg(older);
    case Failure::TooLarge:
    case Failure::StorageRefused: return tr("The last backup did not fit in this browser's storage. %1").arg(older);
    case Failure::NothingArchived:
        return tr("The last backup failed: none of the notebook's files could be read. %1").arg(older);
    case Failure::NotebookUnavailable:
        return tr("The last backup failed: the notebook's folder went away while it ran. %1").arg(older);
    case Failure::InsideNotebook:
        return tr("No backup was made: the folder %1 lies inside the notebook. Choose a folder outside it.")
                .arg(QDir::toNativeSeparators(folder()));
    case Failure::None: break;
    }
    return {};
}

QString WorkspaceBackups::lastBackupText() const
{
    if (!m_vault)
        return {};
    const QJsonObject last = m_vault->store()->get(kLast).toObject();
    const QDateTime time = fromIso(last.value(u"time"_s).toString());
    if (!time.isValid())
        return {};
    return whenText(time, m_language, m_clock().date()) + u" · "_s
           + sizeText(qint64(last.value(u"bytes"_s).toDouble()));
}

void WorkspaceBackups::chooseFolder(const QUrl &url)
{
    if (!available() || m_browser)
        return;
    QString path;
    if (url.isLocalFile()) {
        path = url.toLocalFile();
    } else if (url.scheme() == u"content") {
        // Android's picker gives document URIs; the storage's own providers name the folder in
        // them, and Brulion writes there with its all-files access. Others (cloud drives) cannot
        // be written file by file like this.
        QString primary = platform::primaryStorageOf(QFileInfo(m_vault->rootPath()).canonicalFilePath());
        if (primary.isEmpty())
            primary = platform::primaryStorageOf(
                    QFileInfo(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).canonicalFilePath());
        path = platform::localPathOfDocument(url, primary);
        if (path.isEmpty()) {
            Q_EMIT messageRequested(tr("Brulion cannot write backups to this place. Choose a folder on the phone's "
                                       "storage or a memory card; cloud drives are not supported."),
                                    u"error"_s);
            return;
        }
    } else {
        Q_EMIT messageRequested(tr("Brulion cannot write backups to this place."), u"error"_s);
        return;
    }
    path = QDir::cleanPath(path);
    if (backup::isInsideFolder(path, m_vault->rootPath())) {
        Q_EMIT messageRequested(tr("Backups cannot be kept inside the notebook: sync would spread them and each backup "
                                   "would hold the ones before it. Choose a folder outside it."),
                                u"error"_s);
        return;
    }
    auto problem = std::make_shared<QString>();
    auto writable = std::make_shared<bool>(false);
    inBackground([path, problem, writable] { *writable = backup::checkFolderWritable(path, problem.get()); },
                 [this, path, problem, writable] {
                     if (!available())
                         return;
                     if (!*writable) {
                         qCWarning(logging::lcIo).noquote() << u"Backup folder %1 refused: %2"_s.arg(path, *problem);
#ifdef Q_OS_ANDROID
                         const QString message = tr("Brulion cannot write to this folder. Allow “All files access” for "
                                                    "Brulion in Android's settings, then choose it again.");
#else
                         const QString message = tr("Brulion cannot write to this folder.");
#endif
                         Q_EMIT messageRequested(message, u"error"_s);
                         return;
                     }
                     qCInfo(logging::lcIo).noquote() << u"Backup folder: %1"_s.arg(path);
                     m_vault->store()->set(kFolder, path);
                     m_vault->store()->remove(kStatus);
                     m_toldFailures.clear();
                     Q_EMIT stateChanged();
                     refresh();
                 });
}

void WorkspaceBackups::useDefaultFolder()
{
    if (!m_vault || usingDefaultFolder())
        return;
    m_vault->store()->remove(kFolder);
    m_vault->store()->remove(kStatus);
    m_toldFailures.clear();
    Q_EMIT stateChanged();
    refresh();
}

bool WorkspaceBackups::exportBackup(const QString &name)
{
    if (!available() || !backup::backupFileTime(name, m_attachment.notebookName))
        return false;
    const QString path = QDir(folder()).filePath(QFileInfo(name).fileName());
#if defined(Q_OS_WASM)
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT messageRequested(tr("The backup could not be read."), u"error"_s);
        return false;
    }
    platform::wasm::saveFile(file.readAll(), name); // in the tab's memory already (docs/WEB.md)
    return true;
#elif defined(Q_OS_ANDROID)
    if (!platform::AndroidBridge::instance()->shareFile(path, u"application/zip"_s, name,
                                                        tr("Save or send the backup"))) {
        Q_EMIT messageRequested(tr("Could not open the share menu."), u"error"_s);
        return false;
    }
    return true;
#else
    Q_UNUSED(path)
    return false;
#endif
}

void WorkspaceBackups::openFolder()
{
    if (const QString path = folder(); canOpenFolder() && !path.isEmpty()) {
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

} // namespace brulion::shell
