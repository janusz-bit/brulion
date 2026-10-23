#include "sync/synchistory.h"

#include "log/redact.h"
#include "vault/localstore.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

const QString kRunsKey = u"runs"_s;

QString timeText(const QDateTime &time)
{
    return time.isValid() ? time.toString(Qt::ISODateWithMs) : QString();
}

QDateTime timeFrom(const QJsonValue &value)
{
    return value.isString() ? QDateTime::fromString(value.toString(), Qt::ISODateWithMs) : QDateTime();
}

int intOf(const QJsonObject &json, const QString &key)
{
    return json.value(key).toInt();
}

qint64 longOf(const QJsonObject &json, const QString &key)
{
    return json.value(key).toInteger();
}

/// Codes after which the user has to do something (Brulion cannot fix it by retrying).
bool userCode(const QString &code)
{
    static const QStringList codes{
            u"auth"_s,        u"forbidden"_s,    u"not-found"_s,        u"parse"_s, u"insufficient-storage"_s,
            u"local-empty"_s, u"remote-empty"_s, u"vault-unavailable"_s};
    return codes.contains(code);
}

} // namespace

QString syncOutcomeName(SyncOutcome outcome)
{
    switch (outcome) {
    case SyncOutcome::Ok: return u"ok"_s;
    case SyncOutcome::Conflicts: return u"conflicts"_s;
    case SyncOutcome::FileErrors: return u"files"_s;
    case SyncOutcome::NeedsConfirmation: return u"needs-confirmation"_s;
    case SyncOutcome::Refused: return u"refused"_s;
    case SyncOutcome::Failed: return u"failed"_s;
    case SyncOutcome::Cancelled: return u"cancelled"_s;
    }
    return u"ok"_s;
}

std::optional<SyncOutcome> syncOutcomeFromName(QStringView name)
{
    for (int i = int(SyncOutcome::Ok); i <= int(SyncOutcome::Cancelled); ++i) {
        if (name == syncOutcomeName(SyncOutcome(i)))
            return SyncOutcome(i);
    }
    return std::nullopt;
}

SyncOutcome outcomeOf(const SyncReport &report, bool stalled)
{
    if (stalled)
        return SyncOutcome::Failed;
    if (report.cancelled)
        return SyncOutcome::Cancelled;
    if (report.syncError.isError())
        return SyncOutcome::Refused;
    if (report.remoteError.isError())
        return SyncOutcome::Failed;
    if (report.needsConfirmation)
        return SyncOutcome::NeedsConfirmation;
    if (!report.errors.isEmpty())
        return SyncOutcome::FileErrors;
    if (!report.conflicts.isEmpty())
        return SyncOutcome::Conflicts;
    return SyncOutcome::Ok;
}

bool needsUser(SyncOutcome outcome, const QString &errorCode)
{
    switch (outcome) {
    case SyncOutcome::NeedsConfirmation:
    case SyncOutcome::Conflicts: return true;
    case SyncOutcome::Refused:
    case SyncOutcome::Failed: return userCode(errorCode);
    case SyncOutcome::Ok:
    case SyncOutcome::FileErrors:
    case SyncOutcome::Cancelled: return false;
    }
    return false;
}

// ---- SyncRunRecord ------------------------------------------------------------------------------

SyncRunRecord SyncRunRecord::fromReport(const SyncReport &report, bool stalled)
{
    SyncRunRecord r;
    r.number = report.runNumber;
    r.trigger = report.trigger;
    r.started = report.started;
    r.finished = report.finished;
    r.durationMs = report.durationMs;
    r.outcome = outcomeOf(report, stalled);
    switch (r.outcome) {
    case SyncOutcome::Failed:
    case SyncOutcome::Refused:
        if (stalled) {
            r.errorCode = u"stalled"_s;
            r.errorMessage = report.failed() ? logging::redact(report.failureMessage())
                                             : u"No answer or progress; the run was stopped"_s;
        } else {
            r.errorCode = report.failureCode();
            r.errorMessage = logging::redact(report.failureMessage());
        }
        r.errorStatus = report.remoteError.status;
        break;
    case SyncOutcome::NeedsConfirmation:
        r.errorCode = u"needs-confirmation"_s;
        r.errorMessage = u"%1 deletion(s) wait for confirmation"_s.arg(report.pendingDeletions.size());
        break;
    case SyncOutcome::FileErrors:
        r.errorCode = u"files"_s;
        r.errorMessage = logging::redact(report.errors.first().path + u": "_s + report.errors.first().message);
        r.errorStatus = report.errors.first().status;
        break;
    case SyncOutcome::Conflicts: r.errorCode = u"conflicts"_s; break;
    case SyncOutcome::Ok:
    case SyncOutcome::Cancelled: break;
    }
    r.uploaded = report.uploaded;
    r.downloaded = report.downloaded;
    r.merged = report.merged;
    r.deletedLocal = report.deletedLocal;
    r.deletedRemote = report.deletedRemote;
    r.conflicts = int(report.conflicts.size());
    r.fileErrors = int(report.errors.size());
    r.skipped = int(report.skipped.size());
    r.pendingDeletions = int(report.pendingDeletions.size());
    r.foldersCreated = report.foldersCreated;
    r.foldersDeleted = report.foldersDeleted;
    r.foldersMoved = report.foldersMoved;
    r.phases = report.phases;
    r.stats = report.stats;
    for (const SyncFileError &e : report.errors) {
        if (r.errors.size() >= MaxErrors)
            break;
        SyncFileError copy = e;
        copy.message = logging::redact(e.message);
        r.errors.append(copy);
    }
    r.conflictPaths = report.conflicts.mid(0, MaxConflicts);
    return r;
}

bool SyncRunRecord::isQuiet() const
{
    return outcome == SyncOutcome::Ok && changeCount() == 0 && skipped == 0 && fileErrors == 0 && pendingDeletions == 0
           && conflicts == 0;
}

int SyncRunRecord::changeCount() const
{
    return uploaded + downloaded + merged + deletedLocal + deletedRemote + conflicts + foldersCreated + foldersDeleted
           + foldersMoved;
}

QJsonObject toJson(const SyncPhaseTime &phase)
{
    return {{u"p"_s, syncPhaseName(phase.phase)},
            {u"s"_s, phase.startMs},
            {u"d"_s, phase.durationMs},
            {u"i"_s, phase.items},
            {u"b"_s, phase.bytes}};
}

std::optional<SyncPhaseTime> phaseTimeFromJson(const QJsonObject &json)
{
    const std::optional<SyncPhase> phase = syncPhaseFromName(json.value(u"p"_s).toString());
    if (!phase)
        return std::nullopt;
    return SyncPhaseTime{*phase, longOf(json, u"s"_s), longOf(json, u"d"_s), intOf(json, u"i"_s), longOf(json, u"b"_s)};
}

QJsonObject toJson(const SyncStats &stats)
{
    QJsonObject methods;
    for (auto it = stats.requestsByMethod.cbegin(); it != stats.requestsByMethod.cend(); ++it)
        methods.insert(QString::fromLatin1(it.key()), it.value());
    QJsonArray slowest;
    for (const SyncRequestTime &r : stats.slowest) {
        slowest.append(QJsonObject{{u"m"_s, QString::fromLatin1(r.method)},
                                   {u"p"_s, logging::redact(r.path)},
                                   {u"s"_s, r.status},
                                   {u"ms"_s, r.ms},
                                   {u"b"_s, r.bytes}});
    }
    return {{u"requests"_s, stats.requests},
            {u"failed"_s, stats.failedRequests},
            {u"timeouts"_s, stats.timeouts},
            {u"retries"_s, stats.preconditionRetries},
            {u"methods"_s, methods},
            {u"requestMs"_s, stats.requestMs},
            {u"sent"_s, stats.bytesSent},
            {u"received"_s, stats.bytesReceived},
            {u"listing"_s, stats.listingBytes},
            {u"folders"_s, stats.remoteFolders},
            {u"remoteFiles"_s, stats.remoteFiles},
            {u"localFiles"_s, stats.localFiles},
            {u"tracked"_s, stats.trackedFiles},
            {u"paths"_s, stats.pathsChecked},
            {u"stall"_s, stats.longestStallMs},
            {u"protocol"_s, QString::fromLatin1(stats.protocol)},
            {u"slowest"_s, slowest}};
}

SyncStats statsFromJson(const QJsonObject &json)
{
    SyncStats st;
    st.requests = intOf(json, u"requests"_s);
    st.failedRequests = intOf(json, u"failed"_s);
    st.timeouts = intOf(json, u"timeouts"_s);
    st.preconditionRetries = intOf(json, u"retries"_s);
    const QJsonObject methods = json.value(u"methods"_s).toObject();
    for (auto it = methods.constBegin(); it != methods.constEnd(); ++it)
        st.requestsByMethod.insert(it.key().toLatin1(), it.value().toInt());
    st.requestMs = longOf(json, u"requestMs"_s);
    st.bytesSent = longOf(json, u"sent"_s);
    st.bytesReceived = longOf(json, u"received"_s);
    st.listingBytes = longOf(json, u"listing"_s);
    st.remoteFolders = intOf(json, u"folders"_s);
    st.remoteFiles = intOf(json, u"remoteFiles"_s);
    st.localFiles = intOf(json, u"localFiles"_s);
    st.trackedFiles = intOf(json, u"tracked"_s);
    st.pathsChecked = intOf(json, u"paths"_s);
    st.longestStallMs = longOf(json, u"stall"_s);
    st.protocol = json.value(u"protocol"_s).toString().toLatin1();
    for (const QJsonValue &v : json.value(u"slowest"_s).toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(u"m"_s).isString())
            continue;
        st.slowest.append(SyncRequestTime{o.value(u"m"_s).toString().toLatin1(), o.value(u"p"_s).toString(),
                                          intOf(o, u"s"_s), longOf(o, u"ms"_s), longOf(o, u"b"_s)});
    }
    return st;
}

QJsonObject toJson(const SyncFileError &error)
{
    QJsonObject json{{u"path"_s, error.path}, {u"message"_s, logging::redact(error.message)}, {u"code"_s, error.code}};
    if (error.status != 0)
        json.insert(u"status"_s, error.status);
    return json;
}

std::optional<SyncFileError> fileErrorFromJson(const QJsonObject &json)
{
    if (!json.value(u"path"_s).isString())
        return std::nullopt;
    return SyncFileError{json.value(u"path"_s).toString(), json.value(u"message"_s).toString(),
                         json.value(u"code"_s).toString(), intOf(json, u"status"_s)};
}

QJsonObject SyncRunRecord::toJson() const
{
    QJsonArray phaseArray;
    for (const SyncPhaseTime &p : phases)
        phaseArray.append(sync::toJson(p));
    QJsonArray errorArray;
    for (const SyncFileError &e : errors)
        errorArray.append(sync::toJson(e));
    QJsonObject json{
            {u"n"_s, number},
            {u"trigger"_s, syncTriggerName(trigger)},
            {u"started"_s, timeText(started)},
            {u"finished"_s, timeText(finished)},
            {u"ms"_s, durationMs},
            {u"outcome"_s, syncOutcomeName(outcome)},
            {u"up"_s, uploaded},
            {u"down"_s, downloaded},
            {u"merged"_s, merged},
            {u"delLocal"_s, deletedLocal},
            {u"delRemote"_s, deletedRemote},
            {u"conflicts"_s, conflicts},
            {u"fileErrors"_s, fileErrors},
            {u"skipped"_s, skipped},
            {u"pending"_s, pendingDeletions},
            {u"phases"_s, phaseArray},
            {u"stats"_s, sync::toJson(stats)},
    };
    // Folder counts only when there were any (records of older versions have none).
    if (foldersCreated > 0)
        json.insert(u"foldersNew"_s, foldersCreated);
    if (foldersDeleted > 0)
        json.insert(u"foldersDel"_s, foldersDeleted);
    if (foldersMoved > 0)
        json.insert(u"foldersMoved"_s, foldersMoved);
    if (!errorCode.isEmpty())
        json.insert(u"code"_s, errorCode);
    if (!errorMessage.isEmpty())
        json.insert(u"message"_s, logging::redact(errorMessage));
    if (errorStatus != 0)
        json.insert(u"status"_s, errorStatus);
    if (!errorArray.isEmpty())
        json.insert(u"errors"_s, errorArray);
    if (!conflictPaths.isEmpty())
        json.insert(u"conflictPaths"_s, QJsonArray::fromStringList(conflictPaths));
    if (repeats > 1) {
        json.insert(u"repeats"_s, repeats);
        json.insert(u"firstStarted"_s, timeText(firstStarted));
    }
    return json;
}

std::optional<SyncRunRecord> SyncRunRecord::fromJson(const QJsonObject &json)
{
    const int number = intOf(json, u"n"_s);
    const std::optional<SyncOutcome> outcome = syncOutcomeFromName(json.value(u"outcome"_s).toString());
    const QDateTime started = timeFrom(json.value(u"started"_s));
    if (number <= 0 || !outcome || !started.isValid())
        return std::nullopt;
    SyncRunRecord r;
    r.number = number;
    r.trigger = syncTriggerFromName(json.value(u"trigger"_s).toString()).value_or(SyncTrigger::Unknown);
    r.started = started;
    r.finished = timeFrom(json.value(u"finished"_s));
    r.durationMs = qMax<qint64>(0, longOf(json, u"ms"_s));
    r.outcome = *outcome;
    r.errorCode = json.value(u"code"_s).toString();
    r.errorMessage = json.value(u"message"_s).toString();
    r.errorStatus = intOf(json, u"status"_s);
    r.uploaded = intOf(json, u"up"_s);
    r.downloaded = intOf(json, u"down"_s);
    r.merged = intOf(json, u"merged"_s);
    r.deletedLocal = intOf(json, u"delLocal"_s);
    r.deletedRemote = intOf(json, u"delRemote"_s);
    r.conflicts = intOf(json, u"conflicts"_s);
    r.fileErrors = intOf(json, u"fileErrors"_s);
    r.skipped = intOf(json, u"skipped"_s);
    r.pendingDeletions = intOf(json, u"pending"_s);
    r.foldersCreated = qMax(0, intOf(json, u"foldersNew"_s));
    r.foldersDeleted = qMax(0, intOf(json, u"foldersDel"_s));
    r.foldersMoved = qMax(0, intOf(json, u"foldersMoved"_s));
    for (const QJsonValue &v : json.value(u"phases"_s).toArray()) {
        if (const std::optional<SyncPhaseTime> p = phaseTimeFromJson(v.toObject()))
            r.phases.append(*p);
    }
    r.stats = statsFromJson(json.value(u"stats"_s).toObject());
    for (const QJsonValue &v : json.value(u"errors"_s).toArray()) {
        if (r.errors.size() >= MaxErrors)
            break;
        if (const std::optional<SyncFileError> e = fileErrorFromJson(v.toObject()))
            r.errors.append(*e);
    }
    for (const QJsonValue &v : json.value(u"conflictPaths"_s).toArray()) {
        if (v.isString() && r.conflictPaths.size() < MaxConflicts)
            r.conflictPaths.append(v.toString());
    }
    r.repeats = qMax(1, intOf(json, u"repeats"_s));
    if (r.repeats > 1)
        r.firstStarted = timeFrom(json.value(u"firstStarted"_s));
    return r;
}

// ---- SyncHistory --------------------------------------------------------------------------------

SyncHistory::SyncHistory(LocalStore *store, int capacity, QObject *parent)
    : QObject(parent), m_store(store), m_capacity(qMax(1, capacity))
{
    if (!m_store)
        return;
    for (const QJsonValue &v : m_store->get(kRunsKey).toArray()) {
        if (const std::optional<SyncRunRecord> r = SyncRunRecord::fromJson(v.toObject()))
            m_runs.append(*r);
        if (m_runs.size() >= m_capacity)
            break;
    }
}

std::optional<SyncRunRecord> SyncHistory::run(int number) const
{
    for (const SyncRunRecord &r : m_runs) {
        if (r.number == number)
            return r;
    }
    return std::nullopt;
}

int SyncHistory::nextNumber() const
{
    int newest = 0;
    for (const SyncRunRecord &r : m_runs)
        newest = qMax(newest, r.number);
    return newest + 1;
}

void SyncHistory::add(const SyncReport &report, bool stalled)
{
    add(SyncRunRecord::fromReport(report, stalled));
}

void SyncHistory::add(const SyncRunRecord &record)
{
    if (record.isQuiet() && !m_runs.isEmpty() && m_runs.first().isQuiet()) {
        // Quiet runs (nothing changed) fold into one row: "bez zmian ×12, 09:00–12:00".
        const SyncRunRecord &previous = m_runs.first();
        SyncRunRecord folded = record;
        folded.repeats = previous.repeats + record.repeats;
        folded.firstStarted =
                previous.repeats > 1 && previous.firstStarted.isValid() ? previous.firstStarted : previous.started;
        folded.durationMs = qMax(previous.durationMs, record.durationMs);
        m_runs.first() = folded;
    } else {
        m_runs.prepend(record);
        while (m_runs.size() > m_capacity)
            m_runs.removeLast();
    }
    save();
    Q_EMIT changed();
}

void SyncHistory::clear()
{
    if (m_runs.isEmpty())
        return;
    m_runs.clear();
    save();
    Q_EMIT changed();
}

void SyncHistory::save()
{
    if (!m_store)
        return;
    QJsonArray array;
    for (const SyncRunRecord &r : m_runs)
        array.append(r.toJson());
    m_store->set(kRunsKey, array);
}

} // namespace brulion::sync
