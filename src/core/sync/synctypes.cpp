#include "sync/synctypes.h"

#include <QJsonValue>

using namespace Qt::StringLiterals;

namespace brulion::sync {

QString remoteVersion(const RemoteEntry &entry)
{
    if (!entry.etag.isEmpty())
        return entry.etag;
    return u"m%1-s%2"_s.arg(entry.mtime).arg(entry.size);
}

QJsonObject SyncFileState::toJson() const
{
    QJsonObject json{
        {u"localHash"_s, localHash},
        {u"remoteEtag"_s, remoteEtag},
        {u"size"_s, size},
        {u"syncedAt"_s, syncedAt},
    };
    if (mtime)
        json.insert(u"mtime"_s, *mtime);
    return json;
}

std::optional<SyncFileState> SyncFileState::fromJson(const QJsonObject &json)
{
    const QJsonValue hash = json.value(u"localHash"_s);
    const QJsonValue etag = json.value(u"remoteEtag"_s);
    const QJsonValue size = json.value(u"size"_s);
    if (!hash.isString() || !etag.isString() || !size.isDouble())
        return std::nullopt;
    SyncFileState state;
    state.localHash = hash.toString();
    state.remoteEtag = etag.toString();
    state.size = size.toInteger();
    if (const QJsonValue mtime = json.value(u"mtime"_s); mtime.isDouble())
        state.mtime = mtime.toInteger();
    state.syncedAt = json.value(u"syncedAt"_s).toInteger();
    return state;
}

QString syncActionName(SyncAction action)
{
    switch (action) {
    case SyncAction::Upload: return u"upload"_s;
    case SyncAction::Download: return u"download"_s;
    case SyncAction::Merge: return u"merge"_s;
    case SyncAction::Conflict: return u"conflict"_s;
    case SyncAction::DeleteLocal: return u"delete-local"_s;
    case SyncAction::DeleteRemote: return u"delete-remote"_s;
    case SyncAction::CreateFolderLocal: return u"folder-create-local"_s;
    case SyncAction::CreateFolderRemote: return u"folder-create-remote"_s;
    case SyncAction::DeleteFolderLocal: return u"folder-delete-local"_s;
    case SyncAction::DeleteFolderRemote: return u"folder-delete-remote"_s;
    case SyncAction::MoveRemote: return u"move-remote"_s;
    }
    return {};
}

bool isFolderAction(SyncAction action)
{
    switch (action) {
    case SyncAction::CreateFolderLocal:
    case SyncAction::CreateFolderRemote:
    case SyncAction::DeleteFolderLocal:
    case SyncAction::DeleteFolderRemote:
    case SyncAction::MoveRemote: return true;
    case SyncAction::Upload:
    case SyncAction::Download:
    case SyncAction::Merge:
    case SyncAction::Conflict:
    case SyncAction::DeleteLocal:
    case SyncAction::DeleteRemote: return false;
    }
    return false;
}

QString syncTriggerName(SyncTrigger trigger)
{
    switch (trigger) {
    case SyncTrigger::Unknown: return u"unknown"_s;
    case SyncTrigger::Start: return u"start"_s;
    case SyncTrigger::Timer: return u"timer"_s;
    case SyncTrigger::Retry: return u"retry"_s;
    case SyncTrigger::Manual: return u"manual"_s;
    case SyncTrigger::LocalChange: return u"edit"_s;
    case SyncTrigger::NetworkBack: return u"network"_s;
    case SyncTrigger::Foreground: return u"foreground"_s;
    case SyncTrigger::Background: return u"background"_s;
    case SyncTrigger::Confirmation: return u"confirm"_s;
    }
    return u"unknown"_s;
}

std::optional<SyncTrigger> syncTriggerFromName(QStringView name)
{
    for (int i = int(SyncTrigger::Unknown); i <= int(SyncTrigger::Confirmation); ++i) {
        if (name == syncTriggerName(SyncTrigger(i)))
            return SyncTrigger(i);
    }
    return std::nullopt;
}

QString syncPhaseName(SyncPhase phase)
{
    switch (phase) {
    case SyncPhase::Connect: return u"connect"_s;
    case SyncPhase::ListRemote: return u"list-remote"_s;
    case SyncPhase::ListLocal: return u"list-local"_s;
    case SyncPhase::Compare: return u"compare"_s;
    case SyncPhase::Transfer: return u"transfer"_s;
    case SyncPhase::SaveState: return u"save-state"_s;
    }
    return {};
}

std::optional<SyncPhase> syncPhaseFromName(QStringView name)
{
    for (int i = int(SyncPhase::Connect); i <= int(SyncPhase::SaveState); ++i) {
        if (name == syncPhaseName(SyncPhase(i)))
            return SyncPhase(i);
    }
    return std::nullopt;
}

QString SyncReport::failureMessage() const
{
    if (remoteError.isError())
        return remoteError.message;
    if (syncError.isError())
        return syncError.message;
    return {};
}

QString SyncReport::failureCode() const
{
    if (remoteError.isError())
        return errorCodeName(remoteError.code);
    if (syncError.isError())
        return errorCodeName(syncError.code);
    return {};
}

int SyncReport::changeCount() const
{
    return static_cast<int>(changes.size());
}

} // namespace brulion::sync
