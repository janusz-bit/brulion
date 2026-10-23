// WorkspaceSync: what the user sees of sync runs — the throttled live progress, the history of
// the last runs (sync::SyncHistory in <device state>/sync-history.json) and the details of one
// run. Every text is translated here; technical messages are redacted (logging::redact).
#include "workspacesync.h"

#include "dates/dates.h"
#include "log/redact.h"
#include "sync/sync.h"
#include "sync/synchistory.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QLocale>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

QString isoOf(const QDateTime &time)
{
    return time.isValid() ? time.toLocalTime().toString(u"yyyy-MM-ddTHH:mm:ss"_s) : QString();
}

QString progressPhaseId(sync::SyncProgress::Phase phase)
{
    switch (phase) {
    case sync::SyncProgress::Phase::Connecting: return u"connecting"_s;
    case sync::SyncProgress::Phase::Listing: return u"listing"_s;
    case sync::SyncProgress::Phase::ListingLocal: return u"listing-local"_s;
    case sync::SyncProgress::Phase::Comparing: return u"comparing"_s;
    case sync::SyncProgress::Phase::Syncing: return u"syncing"_s;
    case sync::SyncProgress::Phase::Saving: return u"saving"_s;
    case sync::SyncProgress::Phase::Done: return u"done"_s;
    }
    return u"connecting"_s;
}

QString titleOf(const QString &path)
{
    return path.isEmpty() ? QString() : paths::pageTitle(path);
}

} // namespace

QDateTime WorkspaceSync::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

// ---- history ------------------------------------------------------------------------------------

void WorkspaceSync::openHistory()
{
    m_history.reset();
    m_historyStore.reset();
    if (!m_vault)
        return;
    // A small store of its own: the sync bookkeeping store rewrites every merge base on flush.
    const QString dir = m_vault->deviceStatePath();
    m_historyStore = std::make_unique<LocalStore>(dir.isEmpty() ? QString() : dir + u"/sync-history.json"_s);
    m_history = std::make_unique<sync::SyncHistory>(m_historyStore.get());
    connect(m_history.get(), &sync::SyncHistory::changed, this, &WorkspaceSync::historyChanged);
    Q_EMIT historyChanged();
}

void WorkspaceSync::onRunFinished(const sync::SyncReport &report, bool stalled)
{
    if (m_history)
        m_history->add(report, stalled);
    QStringList folders;
    for (const sync::SyncChange &change : report.changes) {
        if (change.action == sync::SyncAction::CreateFolderLocal
            || change.action == sync::SyncAction::DeleteFolderLocal)
            folders.append(change.path);
    }
    if (!folders.isEmpty())
        Q_EMIT foldersChanged(folders);
    scheduleLive();
}

QVariantList WorkspaceSync::history() const
{
    QVariantList out;
    if (!m_history)
        return out;
    for (const sync::SyncRunRecord &record : m_history->runs())
        out.append(historyRow(record));
    return out;
}

QVariantMap WorkspaceSync::lastRun() const
{
    if (!m_history || m_history->runs().isEmpty())
        return {};
    return historyRow(m_history->runs().first());
}

void WorkspaceSync::clearHistory()
{
    if (m_history)
        m_history->clear();
}

qint64 WorkspaceSync::nextSyncMs() const
{
    if (!m_scheduler)
        return 0;
    const QDateTime next = m_scheduler->status().nextSync;
    return next.isValid() ? next.toMSecsSinceEpoch() : 0;
}

bool WorkspaceSync::retrying() const
{
    return m_scheduler && m_scheduler->status().retrying;
}

QString WorkspaceSync::triggerText(const QString &trigger) const
{
    if (trigger == u"start")
        return tr("after start");
    if (trigger == u"timer")
        return tr("scheduled");
    if (trigger == u"retry")
        return tr("retry");
    if (trigger == u"manual")
        return tr("by hand");
    if (trigger == u"edit")
        return tr("after an edit");
    if (trigger == u"network")
        return tr("network is back");
    if (trigger == u"foreground")
        return tr("back in the app");
    if (trigger == u"background")
        return tr("before going to the background");
    if (trigger == u"confirm")
        return tr("deletions confirmed");
    return {};
}

QString WorkspaceSync::phaseText(const QString &phase) const
{
    if (phase == u"connect" || phase == u"connecting")
        return tr("Connecting to the server");
    if (phase == u"list-remote" || phase == u"listing")
        return tr("Checking files on the server");
    if (phase == u"list-local" || phase == u"listing-local")
        return tr("Checking files on this device");
    if (phase == u"compare" || phase == u"comparing")
        return tr("Comparing changes");
    if (phase == u"transfer" || phase == u"syncing")
        return tr("Sending and downloading changes");
    if (phase == u"save-state" || phase == u"saving")
        return tr("Saving the sync state");
    return tr("Finishing");
}

QString WorkspaceSync::fileErrorText(const QString &code, int status) const
{
    if (code == u"locked")
        return tr("The file is locked on the server (open in another app?).");
    if (code == u"forbidden")
        return tr("The server does not allow saving this file.");
    if (code == u"too-large")
        return tr("The file is too large for the server.");
    if (code == u"insufficient-storage")
        return tr("There is no space left on the server.");
    if (code == u"server")
        return tr("The server reported an error — Brulion will try again.");
    if (code == u"conflict")
        return tr("A folder is missing on the server — Brulion will create it again.");
    if (code == u"read-failed")
        return tr("The file cannot be read on this device.");
    if (code == u"write-failed")
        return tr("The file cannot be written on this device.");
    if (code == u"case-clash")
        return tr("Another file has the same name in different letter case — rename one of them.");
    if (code == u"unseen-local-file")
        return tr("The file appeared on this device during the sync — the next sync takes it.");
    if (code == u"remote-busy")
        return tr("The file keeps changing on the server — the next sync tries again.");
    if (code == u"conflict-copy-failed")
        return tr("The conflict copy cannot be saved.");
    if (code == u"timeout" || code == u"network")
        return tr("No answer from the server — Brulion will try again.");
    if (code == u"auth")
        return tr("Wrong user name or app password.");
    if (status > 0)
        return tr("The file could not be synced (HTTP %1).").arg(status);
    return code.isEmpty() ? tr("The file could not be synced.") : tr("The file could not be synced (%1).").arg(code);
}

QString WorkspaceSync::durationText(qint64 ms) const
{
    const QLocale locale = m_language == u"en" ? QLocale(QLocale::English, QLocale::UnitedStates)
                                               : QLocale(QLocale::Polish, QLocale::Poland);
    const qint64 m = qMax<qint64>(0, ms);
    if (m < 10'000)
        return tr("%1 s").arg(locale.toString(double(m) / 1000.0, 'f', 1));
    const qint64 s = (m + 500) / 1000;
    if (s < 60)
        return tr("%1 s").arg(s);
    if (s < 3600)
        return tr("%1 min %2 s").arg(s / 60).arg(s % 60);
    return tr("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

QString WorkspaceSync::bytesText(qint64 bytes) const
{
    const QLocale locale = m_language == u"en" ? QLocale(QLocale::English, QLocale::UnitedStates)
                                               : QLocale(QLocale::Polish, QLocale::Poland);
    return locale.formattedDataSize(qMax<qint64>(0, bytes), 1, QLocale::DataSizeTraditionalFormat);
}

QString WorkspaceSync::shortErrorText(const QString &code) const
{
    if (code == u"network")
        return tr("cannot reach the server");
    if (code == u"timeout")
        return tr("the server did not answer in time");
    if (code == u"stalled")
        return tr("the server stopped answering");
    if (code == u"auth")
        return tr("wrong user name or app password");
    if (code == u"forbidden")
        return tr("access refused");
    if (code == u"not-found")
        return tr("folder not found");
    if (code == u"parse")
        return tr("not a WebDAV address");
    if (code == u"insufficient-storage")
        return tr("no space left on the server");
    if (code == u"server")
        return tr("server error");
    if (code == u"local-empty")
        return tr("the notebook folder looks empty");
    if (code == u"remote-empty")
        return tr("the server folder is empty");
    if (code == u"vault-unavailable")
        return tr("the notebook folder is not available");
    return code.isEmpty() ? tr("unknown error") : code;
}

QString WorkspaceSync::outcomeText(const sync::SyncRunRecord &r) const
{
    switch (r.outcome) {
    case sync::SyncOutcome::Ok:
        if (!r.isQuiet())
            return tr("Synced");
        return r.repeats > 1 ? tr("No changes ×%1").arg(r.repeats) : tr("No changes");
    case sync::SyncOutcome::Conflicts: return tr("Conflict in %n note(s) — merge the versions", "", r.conflicts);
    case sync::SyncOutcome::FileErrors: return tr("Synced with errors: %n file(s)", "", r.fileErrors);
    case sync::SyncOutcome::NeedsConfirmation:
        return tr("Waiting for your decision: %n file(s) to delete", "", r.pendingDeletions);
    case sync::SyncOutcome::Refused: return tr("Paused: %1").arg(shortErrorText(r.errorCode));
    case sync::SyncOutcome::Failed: return tr("Failed: %1").arg(shortErrorText(r.errorCode));
    case sync::SyncOutcome::Cancelled: return tr("Stopped — what was synced stays synced");
    }
    return {};
}

QVariantMap WorkspaceSync::historyRow(const sync::SyncRunRecord &r) const
{
    const QString outcome = sync::syncOutcomeName(r.outcome);
    const bool user = sync::needsUser(r.outcome, r.errorCode);
    QString tone = u"success"_s;
    switch (r.outcome) {
    case sync::SyncOutcome::Ok: tone = u"success"_s; break;
    case sync::SyncOutcome::Conflicts:
    case sync::SyncOutcome::FileErrors:
    case sync::SyncOutcome::NeedsConfirmation: tone = u"warning"_s; break;
    case sync::SyncOutcome::Refused: tone = u"danger"_s; break;
    case sync::SyncOutcome::Failed: tone = user ? u"danger"_s : u"warning"_s; break;
    case sync::SyncOutcome::Cancelled: tone = u"info"_s; break;
    }
    QStringList parts;
    if (r.uploaded > 0)
        parts.append(u"↑%1"_s.arg(r.uploaded));
    if (r.downloaded > 0)
        parts.append(u"↓%1"_s.arg(r.downloaded));
    if (r.merged > 0)
        parts.append(tr("%n merged", "", r.merged));
    if (r.deletedLocal + r.deletedRemote > 0)
        parts.append(tr("%n deleted", "", r.deletedLocal + r.deletedRemote));
    if (r.foldersMoved > 0)
        parts.append(tr("%n folder(s) moved", "", r.foldersMoved));
    if (r.foldersCreated > 0)
        parts.append(tr("%n new folder(s)", "", r.foldersCreated));
    if (r.foldersDeleted > 0)
        parts.append(tr("%n folder(s) deleted", "", r.foldersDeleted));
    if (r.isQuiet() && r.repeats > 1 && r.firstStarted.isValid())
        parts.append(u"%1–%2"_s.arg(r.firstStarted.toLocalTime().toString(u"HH:mm"_s),
                                    r.finished.toLocalTime().toString(u"HH:mm"_s)));
    parts.append(durationText(r.durationMs));
    QString errorText;
    switch (r.outcome) {
    case sync::SyncOutcome::Refused:
    case sync::SyncOutcome::Failed: errorText = this->errorText(r.errorCode, r.errorMessage); break;
    case sync::SyncOutcome::FileErrors:
        errorText = r.errors.isEmpty() ? QString()
                                       : titleOf(r.errors.first().path) + u": "_s
                                                 + fileErrorText(r.errors.first().code, r.errors.first().status);
        break;
    case sync::SyncOutcome::NeedsConfirmation:
        errorText = tr("%n file(s) would be deleted. Check the list and confirm in the sync center.", "",
                       r.pendingDeletions);
        break;
    case sync::SyncOutcome::Conflicts: errorText = tr("Both versions were kept. Merge them in Conflicts."); break;
    case sync::SyncOutcome::Ok:
    case sync::SyncOutcome::Cancelled: break;
    }
    const QString trigger = sync::syncTriggerName(r.trigger);
    return {
            {u"number"_s, r.number},
            {u"started"_s, isoOf(r.started)},
            {u"startedText"_s, human(r.started)},
            {u"finished"_s, isoOf(r.finished)},
            {u"durationMs"_s, r.durationMs},
            {u"durationText"_s, durationText(r.durationMs)},
            {u"trigger"_s, trigger},
            {u"triggerText"_s, triggerText(trigger)},
            {u"outcome"_s, outcome},
            {u"outcomeText"_s, outcomeText(r)},
            {u"tone"_s, tone},
            {u"summary"_s, parts.join(u" · "_s)},
            {u"errorCode"_s, r.errorCode},
            {u"errorText"_s, errorText},
            {u"needsUser"_s, user},
            {u"repeats"_s, r.repeats},
            {u"quiet"_s, r.isQuiet()},
    };
}

QVariantMap WorkspaceSync::runDetails(int number) const
{
    const std::optional<sync::SyncRunRecord> found = m_history ? m_history->run(number) : std::nullopt;
    if (!found)
        return {};
    const sync::SyncRunRecord &r = *found;
    QVariantMap out = historyRow(r);
    QVariantList phases;
    for (const sync::SyncPhaseTime &p : r.phases) {
        const QString id = sync::syncPhaseName(p.phase);
        QStringList detail;
        switch (p.phase) {
        case sync::SyncPhase::ListRemote:
            detail.append(tr("%n folder(s)", "", p.items));
            if (p.bytes > 0)
                detail.append(bytesText(p.bytes));
            break;
        case sync::SyncPhase::ListLocal: detail.append(tr("%n file(s)", "", p.items)); break;
        case sync::SyncPhase::Compare: detail.append(tr("%n tracked file(s)", "", p.items)); break;
        case sync::SyncPhase::Transfer:
            detail.append(tr("%n file(s) checked", "", p.items));
            if (p.bytes > 0)
                detail.append(bytesText(p.bytes));
            break;
        case sync::SyncPhase::Connect:
        case sync::SyncPhase::SaveState: break;
        }
        phases.append(QVariantMap{
                {u"id"_s, id},
                {u"text"_s, phaseText(id)},
                {u"durationMs"_s, p.durationMs},
                {u"durationText"_s, durationText(p.durationMs)},
                {u"share"_s,
                 r.durationMs > 0 ? std::clamp(double(p.durationMs) / double(r.durationMs), 0.0, 1.0) : 0.0},
                {u"items"_s, p.items},
                {u"detailText"_s, detail.join(u" · "_s)},
        });
    }
    out.insert(u"phases"_s, phases);

    const sync::SyncStats &st = r.stats;
    QStringList methods;
    for (auto it = st.requestsByMethod.cbegin(); it != st.requestsByMethod.cend(); ++it)
        methods.append(u"%1 %2"_s.arg(QString::fromLatin1(it.key())).arg(it.value()));
    QVariantList slowest;
    for (const sync::SyncRequestTime &t : st.slowest) {
        slowest.append(QVariantMap{
                {u"method"_s, QString::fromLatin1(t.method)},
                {u"path"_s, t.path},
                {u"title"_s, t.path.isEmpty() ? tr("the notebook folder") : t.path},
                {u"status"_s, t.status},
                {u"msText"_s, durationText(t.ms)},
        });
    }
    out.insert(
            u"stats"_s,
            QVariantMap{
                    {u"requests"_s, st.requests},
                    {u"requestsText"_s, methods.isEmpty() ? QString::number(st.requests)
                                                          : u"%1 (%2)"_s.arg(st.requests).arg(methods.join(u", "_s))},
                    {u"averageText"_s, st.requests > 0 ? durationText(st.requestMs / st.requests) : QString()},
                    {u"bytesText"_s, u"↑ %1 · ↓ %2"_s.arg(bytesText(st.bytesSent), bytesText(st.bytesReceived))},
                    {u"protocol"_s, QString::fromLatin1(st.protocol)},
                    {u"timeouts"_s, st.timeouts},
                    {u"failedRequests"_s, st.failedRequests},
                    {u"stallText"_s, st.longestStallMs >= 5000 ? durationText(st.longestStallMs) : QString()},
                    {u"slowest"_s, slowest},
            });

    QVariantList errors;
    for (const sync::SyncFileError &e : r.errors) {
        errors.append(QVariantMap{
                {u"path"_s, e.path},
                {u"title"_s, titleOf(e.path)},
                {u"code"_s, e.code},
                {u"text"_s, fileErrorText(e.code, e.status)},
                {u"message"_s, logging::redact(e.message)},
        });
    }
    out.insert(u"errors"_s, errors);
    out.insert(u"moreErrors"_s, qMax(0, r.fileErrors - int(r.errors.size())));
    QVariantList conflicts;
    for (const VaultPath &path : r.conflictPaths)
        conflicts.append(QVariantMap{{u"path"_s, path}, {u"title"_s, titleOf(path)}});
    out.insert(u"conflicts"_s, conflicts);
    out.insert(u"errorMessage"_s, logging::redact(r.errorMessage));
    out.insert(u"counts"_s, QVariantMap{
                                    {u"uploaded"_s, r.uploaded},
                                    {u"downloaded"_s, r.downloaded},
                                    {u"merged"_s, r.merged},
                                    {u"deletedLocal"_s, r.deletedLocal},
                                    {u"deletedRemote"_s, r.deletedRemote},
                                    {u"foldersCreated"_s, r.foldersCreated},
                                    {u"foldersDeleted"_s, r.foldersDeleted},
                                    {u"foldersMoved"_s, r.foldersMoved},
                                    {u"conflicts"_s, r.conflicts},
                                    {u"fileErrors"_s, r.fileErrors},
                                    {u"skipped"_s, r.skipped},
                            });
    // The run's time in the event log (a little margin: the log's clock is the wall clock).
    const QDateTime from = r.repeats > 1 && r.firstStarted.isValid() ? r.firstStarted : r.started;
    out.insert(u"fromMs"_s, from.isValid() ? from.toMSecsSinceEpoch() - 2000 : qint64(0));
    out.insert(u"toMs"_s, r.finished.isValid() ? r.finished.toMSecsSinceEpoch() + 2000 : qint64(0));
    return out;
}

QString WorkspaceSync::folderCountsText(int created, int deleted, int moved) const
{
    QStringList parts;
    if (created > 0)
        parts.append(tr("%n new", "folders", created));
    if (deleted > 0)
        parts.append(tr("%n deleted", "folders", deleted));
    if (moved > 0)
        parts.append(tr("%n moved on the server", "folders", moved));
    return parts.isEmpty() ? QString() : tr("Folders: %1").arg(parts.join(u", "_s));
}

QString WorkspaceSync::runDetailsText(int number) const
{
    const QVariantMap d = runDetails(number);
    if (d.isEmpty())
        return {};
    QStringList lines;
    lines.append(tr("Sync #%1 · %2 · %3")
                         .arg(number)
                         .arg(d.value(u"startedText"_s).toString(), d.value(u"triggerText"_s).toString()));
    lines.append(tr("Result: %1").arg(d.value(u"outcomeText"_s).toString()));
    if (const QString e = d.value(u"errorText"_s).toString(); !e.isEmpty())
        lines.append(e);
    if (const QString code = d.value(u"errorCode"_s).toString(); !code.isEmpty())
        lines.append(tr("Code: %1").arg(code));
    if (const QString m = d.value(u"errorMessage"_s).toString(); !m.isEmpty())
        lines.append(tr("Details: %1").arg(m));
    lines.append(tr("Duration: %1").arg(d.value(u"durationText"_s).toString()));
    const QVariantMap c = d.value(u"counts"_s).toMap();
    lines.append(tr("Sent %1 · downloaded %2 · merged %3 · deleted %4 here, %5 on the server · conflicts %6 · "
                    "errors %7")
                         .arg(c.value(u"uploaded"_s).toInt())
                         .arg(c.value(u"downloaded"_s).toInt())
                         .arg(c.value(u"merged"_s).toInt())
                         .arg(c.value(u"deletedLocal"_s).toInt())
                         .arg(c.value(u"deletedRemote"_s).toInt())
                         .arg(c.value(u"conflicts"_s).toInt())
                         .arg(c.value(u"fileErrors"_s).toInt()));
    const int foldersCreated = c.value(u"foldersCreated"_s).toInt();
    const int foldersDeleted = c.value(u"foldersDeleted"_s).toInt();
    const int foldersMoved = c.value(u"foldersMoved"_s).toInt();
    if (foldersCreated + foldersDeleted + foldersMoved > 0)
        lines.append(folderCountsText(foldersCreated, foldersDeleted, foldersMoved));
    for (const QVariant &v : d.value(u"phases"_s).toList()) {
        const QVariantMap p = v.toMap();
        QString line = u"  %1: %2"_s.arg(p.value(u"text"_s).toString(), p.value(u"durationText"_s).toString());
        if (const QString detail = p.value(u"detailText"_s).toString(); !detail.isEmpty())
            line += u" ("_s + detail + u')';
        lines.append(line);
    }
    const QVariantMap st = d.value(u"stats"_s).toMap();
    lines.append(tr("Requests: %1, average %2, %3 %4")
                         .arg(st.value(u"requestsText"_s).toString(), st.value(u"averageText"_s).toString(),
                              st.value(u"bytesText"_s).toString(), st.value(u"protocol"_s).toString())
                         .trimmed());
    for (const QVariant &v : st.value(u"slowest"_s).toList()) {
        const QVariantMap s = v.toMap();
        lines.append(u"  %1 %2 → %3 · %4"_s.arg(s.value(u"method"_s).toString(), s.value(u"title"_s).toString(),
                                                QString::number(s.value(u"status"_s).toInt()),
                                                s.value(u"msText"_s).toString()));
    }
    for (const QVariant &v : d.value(u"errors"_s).toList()) {
        const QVariantMap e = v.toMap();
        lines.append(u"  ✕ %1: %2 [%3] %4"_s.arg(e.value(u"path"_s).toString(), e.value(u"text"_s).toString(),
                                                 e.value(u"code"_s).toString(), e.value(u"message"_s).toString()));
    }
    for (const QVariant &v : d.value(u"conflicts"_s).toList())
        lines.append(u"  ⇄ %1"_s.arg(v.toMap().value(u"path"_s).toString()));
    return logging::redact(lines.join(u'\n'));
}

QStringList WorkspaceSync::historyLines(int max) const
{
    QStringList out;
    if (!m_history)
        return out;
    const QList<sync::SyncRunRecord> runs = m_history->runs();
    for (qsizetype i = 0; i < runs.size() && i < max; ++i) {
        const sync::SyncRunRecord &r = runs.at(i);
        QString line = u"#%1 %2 · %3 · %4 · %5"_s.arg(
                QString::number(r.number), r.started.toLocalTime().toString(u"yyyy-MM-dd HH:mm"_s),
                triggerText(sync::syncTriggerName(r.trigger)), outcomeText(r), durationText(r.durationMs));
        if (!r.errorCode.isEmpty())
            line += u" · "_s + r.errorCode;
        out.append(line);
    }
    return out;
}

// ---- live progress ------------------------------------------------------------------------------

void WorkspaceSync::scheduleLive()
{
    if (!m_liveTimer.isActive())
        m_liveTimer.start();
}

void WorkspaceSync::updateLive()
{
    QVariantMap live;
    const bool running = m_scheduler && m_scheduler->isRunning();
    if (running) {
        const sync::SyncStatus s = m_scheduler->status();
        const sync::SyncProgress p = s.progress.value_or(sync::SyncProgress{});
        const QString phase = progressPhaseId(p.phase);
        const qint64 wall =
                s.runStarted.isValid() ? QDateTime::currentMSecsSinceEpoch() - s.runStarted.toMSecsSinceEpoch() : 0;
        const qint64 elapsed = qMax(p.elapsedMs, wall);
        QString detail;
        double progress = -1;
        if (p.phase == sync::SyncProgress::Phase::Listing && p.total > 0) {
            detail = tr("folder %1 of about %2").arg(p.done).arg(p.total);
        } else if (p.phase == sync::SyncProgress::Phase::Syncing && p.total > 0) {
            detail = tr("%1 of %2 files checked · changes: %3").arg(p.done).arg(p.total).arg(p.changes);
            progress = std::clamp(double(p.done) / double(p.total), 0.0, 1.0);
        }
        QStringList titles;
        for (const VaultPath &path : p.active) {
            if (titles.size() >= 3)
                break;
            titles.append(titleOf(path));
        }
        const QString trigger = sync::syncTriggerName(s.trigger);
        const int number = m_engine && m_engine->isSyncing() ? m_engine->nextRunNumber() - 1 : 0;
        QString stallText;
        if (s.abandoning)
            stallText = tr("No answer for too long: the sync is being stopped and will be tried again.");
        else if (s.stalled)
            stallText = tr("The server has not answered for %1. Brulion waits a little longer, then tries again.")
                                .arg(durationText(s.stalledMs));
        live = {
                {u"running"_s, true},
                {u"number"_s, number},
                {u"trigger"_s, trigger},
                {u"triggerText"_s, triggerText(trigger)},
                {u"phase"_s, phase},
                {u"phaseText"_s, QString(phaseText(phase) + u'…')},
                {u"detailText"_s, detail},
                {u"done"_s, p.done},
                {u"total"_s, p.total},
                {u"progress"_s, progress},
                {u"active"_s, titles},
                {u"activeText"_s, titles.isEmpty() ? QString() : tr("Now: %1").arg(titles.join(u", "_s))},
                {u"elapsedMs"_s, elapsed},
                {u"elapsedText"_s, tr("Running for %1").arg(durationText(elapsed))},
                {u"bytesText"_s, p.bytesDone > 0 ? bytesText(p.bytesDone) : QString()},
                {u"changes"_s, p.changes},
                {u"errors"_s, p.errors},
                {u"stalled"_s, s.stalled || s.abandoning},
                {u"stallText"_s, stallText},
                {u"waitingFor"_s, s.waitingFor},
                {u"abandoning"_s, s.abandoning},
        };
        if (!m_liveTick.isActive())
            m_liveTick.start();
    } else {
        m_liveTick.stop();
    }
    if (live != m_live) {
        m_live = live;
        Q_EMIT liveChanged();
    }
    // The one-line status (sidebar footer, settings) follows the progress at the same pace.
    if (const QString text = statusText(); text != m_lastStatusText) {
        m_lastStatusText = text;
        Q_EMIT statusChanged();
    }
}

} // namespace brulion::shell
