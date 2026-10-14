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
    }
    return {};
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
