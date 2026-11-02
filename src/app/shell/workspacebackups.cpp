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
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

const QString kEnabled = u"backup/enabled"_s;
const QString kInterval = u"backup/interval"_s;
const QString kKeep = u"backup/keep"_s;
const QString kFolder = u"backup/folder"_s;
const QString kLast = u"backup/last"_s;     // { time, file, bytes, fingerprint, folder }
const QString kOwn = u"backup/own"_s;       // file names this notebook wrote: pruning takes no others
const QString kStatus = u"backup/status"_s; // { failure, detail } | { skipped }

/// More names than any limit keeps: older entries are files long gone or in another folder.
constexpr qsizetype MaxOwnNames = 512;
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
    if (m_applier && m_applier->isRunning())
        qCWarning(logging::lcIo).noquote()
                << u"Backup: the notebook closed during a restore: the rest was not applied"_s;
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
    if (!available() || running() || !enabled() || !m_started) {
        Q_EMIT idle();
        return;
    }
    backup::Settings settings;
    settings.enabled = true;
    settings.interval = interval() == u"weekly" ? backup::Interval::Weekly : backup::Interval::Daily;
    settings.keep = keep();
    const QJsonObject last = m_vault->store()->get(kLast).toObject();
    const QDateTime lastTime = fromIso(last.value(u"time"_s).toString());
    const QDateTime now = m_clock();
    if (!backup::isDue(settings.interval, lastTime, now)) {
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
                     backup::ScheduleState state;
                     state.lastBackup = lastTime;
                     state.lastFingerprint = last.value(u"fingerprint"_s).toString();
                     state.lastFileExists = *exists;
                     const QDateTime checkedAt = m_clock();
                     const QString fingerprint = backup::fingerprint(sources);
                     switch (backup::decide(settings, state, fingerprint, sources.files.size(), checkedAt)) {
                     case backup::Decision::Run:
                         m_runAutomatic = true;
                         startWrite(Op::Backup);
                         break;
                     case backup::Decision::Unchanged:
                         qCDebug(logging::lcIo) << "Backup: nothing changed since the last one";
                         Q_EMIT idle();
                         break;
                     default: Q_EMIT idle(); break;
                     }
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
    if (backup::collectSources(*m_vault).files.isEmpty()) {
        Q_EMIT messageRequested(tr("This notebook is empty: there is nothing to back up."), u"info"_s);
        return false;
    }
    m_runAutomatic = false;
    startWrite(Op::Backup);
    return true;
}

void WorkspaceBackups::cancel()
{
    switch (m_op) {
    case Op::Backup:
    case Op::Safety: m_writer->cancel(); break;
    case Op::Check: m_checker->cancel(); break;
    case Op::Restore: m_applier->cancel(); break;
    case Op::None: break;
    }
}

void WorkspaceBackups::startWrite(Op op)
{
    const QString folderPath = folder();
    const QDateTime now = m_clock();
    const QString name = m_attachment.notebookName;
    const backup::Sources sources = backup::collectSources(*m_vault);
    m_runTime = now;
    m_runFingerprint = backup::fingerprint(sources);
    backup::WriteOptions options;
    options.targetPath = backup::freeBackupPath(folderPath, name, now);
    options.createFolder = usingDefaultFolder();
    options.workFolder = m_attachment.workFolder;
    options.maxBytes = m_attachment.maxBytes;
    setOp(op);
    m_writer->start(sources, options);
    inBackground([folderPath, name, now] { backup::removeStalePartials(folderPath, name, now); }, {});
}

void WorkspaceBackups::onWritten(const backup::WriteResult &result)
{
    const bool safety = m_op == Op::Safety;
    if (!result.ok()) {
        const auto failureOf = [&] {
            switch (result.error) {
            case backup::WriteError::FolderMissing: return Failure::FolderMissing;
            case backup::WriteError::FolderUnwritable: return Failure::FolderUnwritable;
            case backup::WriteError::TooLarge: return Failure::TooLarge;
            default: return Failure::WriteFailed;
            }
        };
        if (result.error == backup::WriteError::Cancelled) {
            qCInfo(logging::lcIo) << "Backup: cancelled";
            setOp(Op::None);
            if (safety)
                abortRestore(tr("The restore was cancelled. Nothing was changed."));
            return;
        }
        qCWarning(logging::lcIo).noquote() << u"Backup failed in %1: %2"_s.arg(folder(), result.detail);
        const Failure failure = failureOf();
        setOp(Op::None);
        setFailure(failure, result.detail);
        if (safety) {
            abortRestore(tr("The safety backup could not be made, so nothing was restored. %1")
                                 .arg(failureMessage(failure)));
            return;
        }
        if (!m_toldFailures.contains(int(failure)) || !m_runAutomatic) {
            m_toldFailures.insert(int(failure));
            Q_EMIT messageRequested(failureMessage(failure), u"warning"_s);
        }
        return;
    }
    afterWrite(result);
}

void WorkspaceBackups::afterWrite(const backup::WriteResult &result)
{
    const bool safety = m_op == Op::Safety;
    const QString name = QFileInfo(result.path).fileName();
    m_lastBeforeUnconfirmed = m_vault->store()->get(kLast);
    m_vault->store()->set(kLast, QJsonObject{{u"time"_s, iso(m_runTime)},
                                             {u"file"_s, name},
                                             {u"bytes"_s, double(result.bytes)},
                                             {u"fingerprint"_s, m_runFingerprint},
                                             {u"folder"_s, folder()}});
    rememberOwn(name);
    clearFailure(int(result.skipped.size()));
    qCInfo(logging::lcIo).noquote() << u"Backup written: %1, %2 files, %3%4"_s.arg(name)
                                               .arg(result.files)
                                               .arg(logging::formatBytes(result.bytes),
                                                    result.skipped.isEmpty()
                                                            ? QString()
                                                            : u", left out: %1"_s.arg(result.skipped.join(u", "_s)));
    Q_EMIT backupWritten(name);
    if (!result.skipped.isEmpty() && !safety) {
        Q_EMIT messageRequested(
                tr("The backup was made without %n file(s) that could not be read.", "", int(result.skipped.size())),
                u"warning"_s);
    } else if (!safety && !m_runAutomatic) {
        Q_EMIT messageRequested(tr("Backup created."), u"success"_s);
    }
    if (m_deferUntilStored) {
        // Pruning (and a restore) wait until the browser's storage has it.
        m_unconfirmed = name;
        if (safety) {
            m_waitingForStored = true;
            m_storedTimeout.start();
            refresh();
            return;
        }
        setOp(Op::None);
        refresh();
        return;
    }
    if (safety) {
        pruneThenRefresh();
        startApply();
        return;
    }
    setOp(Op::None);
    pruneThenRefresh();
}

void WorkspaceBackups::storageFlushed()
{
    if (m_unconfirmed.isEmpty())
        return;
    m_unconfirmed.clear();
    pruneThenRefresh();
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
        forgetOwn(m_unconfirmed);
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
    } else if (!m_toldFailures.contains(int(Failure::StorageRefused))) {
        m_toldFailures.insert(int(Failure::StorageRefused));
        Q_EMIT messageRequested(failureMessage(Failure::StorageRefused), u"warning"_s);
    }
    refresh();
}

void WorkspaceBackups::pruneThenRefresh()
{
    if (!available())
        return;
    const QSet<QString> own = ownNames();
    QSet<QString> protect;
    if (const QString name = QFileInfo(lastFile()).fileName(); !name.isEmpty())
        protect.insert(name);
    if (!m_restoreName.isEmpty())
        protect.insert(m_restoreName);
    const QString folderPath = folder();
    const QString notebook = m_attachment.notebookName;
    const int limit = keep();
    auto removed = std::make_shared<QStringList>();
    auto failed = std::make_shared<QStringList>();
    inBackground([=] { *removed = backup::pruneBackups(folderPath, notebook, limit, own, protect, failed.get()); },
                 [this, removed, failed] {
                     if (!available())
                         return;
                     for (const QString &name : std::as_const(*removed))
                         forgetOwn(name);
                     if (!removed->isEmpty())
                         qCInfo(logging::lcIo).noquote() << u"Backup: pruned %1"_s.arg(removed->join(u", "_s));
                     if (!failed->isEmpty())
                         qCWarning(logging::lcIo).noquote()
                                 << u"Backup: could not prune %1"_s.arg(failed->join(u", "_s));
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
    auto files = std::make_shared<QList<backup::BackupFile>>();
    inBackground([=] { *files = backup::listBackups(folderPath, notebook); },
                 [this, generation, files] {
                     if (generation != m_listGeneration)
                         return;
                     QVariantList out;
                     const QDate today = m_clock().date();
                     const auto locale = m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
                     for (const backup::BackupFile &file : std::as_const(*files)) {
                         const DateSpec spec{file.time.date(),
                                             QTime(file.time.time().hour(), file.time.time().minute())};
                         out.append(QVariantMap{{u"name"_s, file.fileName},
                                                {u"time"_s, iso(file.time)},
                                                {u"when"_s, dates::formatHuman(spec, locale, today)},
                                                {u"size"_s, file.size},
                                                {u"sizeText"_s, sizeText(file.size)}});
                     }
                     m_backups = out;
                     Q_EMIT backupsChanged();
                     Q_EMIT stateChanged(); // canShare, the status' texts
                 });
}

bool WorkspaceBackups::remove(const QString &name)
{
    if (!available() || running() || !backup::backupFileTime(name, m_attachment.notebookName))
        return false;
    const QString path = QDir(folder()).filePath(QFileInfo(name).fileName());
    if (!QFile::remove(path)) {
        Q_EMIT messageRequested(tr("The backup could not be deleted."), u"error"_s);
        return false;
    }
    forgetOwn(name);
    qCInfo(logging::lcIo).noquote() << u"Backup deleted: %1"_s.arg(name);
    refresh();
    return true;
}

bool WorkspaceBackups::prepareRestore(const QString &name)
{
    if (!available() || running() || !backup::backupFileTime(name, m_attachment.notebookName))
        return false;
    if (!notebookStorageOk()) {
        Q_EMIT messageRequested(tr("The notebook's folder is not available, so nothing can be restored."), u"error"_s);
        return false;
    }
    m_restoreName = name;
    m_planReady = false;
    setOp(Op::Check);
    m_checker->start(QDir(folder()).filePath(name), backup::notebookFiles(*m_vault), backup::notebookFolders(*m_vault));
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
                                        : tr("This file is not a backup Brulion can restore. Nothing was changed.");
        m_restoreName.clear();
        Q_EMIT messageRequested(message, u"error"_s);
        return;
    }
    m_plan = check.plan;
    m_planReady = true;
    const QDateTime time = backup::backupFileTime(m_restoreName, m_attachment.notebookName).value_or(QDateTime());
    const auto locale = m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    const DateSpec spec{time.date(), QTime(time.time().hour(), time.time().minute())};
    Q_EMIT restorePlanReady(QVariantMap{{u"name"_s, m_restoreName},
                                        {u"when"_s, dates::formatHuman(spec, locale, m_clock().date())},
                                        {u"changed"_s, int(m_plan.toChange.size())},
                                        {u"recreated"_s, int(m_plan.toRecreate.size())},
                                        {u"trashed"_s, int(m_plan.toTrash.size())},
                                        {u"identical"_s, m_plan.identical},
                                        {u"tooLarge"_s, int(m_plan.tooLarge.size())},
                                        {u"hasChanges"_s, m_plan.hasChanges()}});
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
    // A restore that cannot fit fails before anything changed.
    const QStorageInfo storage(m_vault->rootPath());
    if (storage.isValid() && storage.bytesAvailable() >= 0
        && m_plan.bytesToWrite + RestoreMarginBytes > storage.bytesAvailable()) {
        abortRestore(
                tr("There is not enough free space for this restore: it needs %1.").arg(sizeText(m_plan.bytesToWrite)));
        return;
    }
    if (backup::collectSources(*m_vault).files.isEmpty()) {
        startApply(); // an empty notebook has nothing to save first
        return;
    }
    m_runAutomatic = false;
    startWrite(Op::Safety);
}

void WorkspaceBackups::startApply()
{
    setOp(Op::Restore);
    qCInfo(logging::lcIo).noquote() << u"Backup: restoring %1 (%2 changed, %3 back, %4 to the trash)"_s
                                               .arg(m_restoreName)
                                               .arg(m_plan.toChange.size())
                                               .arg(m_plan.toRecreate.size())
                                               .arg(m_plan.toTrash.size());
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
    QString message;
    QString kind = u"success"_s;
    if (result.cancelled) {
        message = tr("The restore was stopped. What was already restored stays; the safety backup holds the rest.");
        kind = u"warning"_s;
    } else if (!result.ok()) {
        message = tr("The restore did not finish: %n file(s) could not be written. Nothing else went to the trash; the "
                     "safety backup holds the notebook as it was.",
                     "", int(result.failed.size()));
        kind = u"error"_s;
    } else {
        message = tr("Restored. What was replaced is in the version history and what the backup does not hold is in "
                     "the trash; a safety backup was made first.");
    }
    m_plan = {};
    m_restoreName.clear();
    Q_EMIT messageRequested(message, kind);
    refresh();
}

void WorkspaceBackups::abortRestore(const QString &message)
{
    m_plan = {};
    m_planReady = false;
    m_restoreName.clear();
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

QSet<QString> WorkspaceBackups::ownNames() const
{
    QSet<QString> out;
    const QJsonArray own = m_vault->store()->get(kOwn).toArray();
    for (const QJsonValue &name : own)
        out.insert(name.toString());
    return out;
}

void WorkspaceBackups::rememberOwn(const QString &name)
{
    QJsonArray own = m_vault->store()->get(kOwn).toArray();
    own.append(name);
    while (own.size() > MaxOwnNames)
        own.removeFirst();
    m_vault->store()->set(kOwn, own);
}

void WorkspaceBackups::forgetOwn(const QString &name)
{
    QJsonArray own = m_vault->store()->get(kOwn).toArray();
    for (qsizetype i = own.size() - 1; i >= 0; --i) {
        if (own.at(i).toString() == name)
            own.removeAt(i);
    }
    m_vault->store()->set(kOwn, own);
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
    return qBound(backup::MinKeep, m_vault->store()->get(kKeep, fallback).toInt(), backup::MaxKeep);
}

void WorkspaceBackups::setKeep(int value)
{
    if (!m_vault || value == keep())
        return;
    m_vault->store()->set(kKeep, qBound(backup::MinKeep, value, backup::MaxKeep));
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
    const QJsonObject status = m_vault->store()->get(kStatus).toObject();
    if (const int failure = status.value(u"failure"_s).toInt(); failure != int(Failure::None))
        return failureMessage(Failure(failure));
    if (const int skipped = status.value(u"skipped"_s).toInt(); skipped > 0)
        return tr("The last backup is missing %n file(s) that could not be read. The event log names them.", "",
                  skipped);
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
    const auto locale = m_language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    const DateSpec spec{time.date(), QTime(time.time().hour(), time.time().minute())};
    return dates::formatHuman(spec, locale, m_clock().date()) + u" · "_s
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
    if (canOpenFolder() && !folder().isEmpty()) {
        QDir().mkpath(folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
    }
}

} // namespace brulion::shell
