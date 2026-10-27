#include "androidpayload.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::notify {

namespace {

/// The wall-clock time of an occurrence, as notificationId() writes it.
const QString LocalFormat = u"yyyy-MM-ddTHH:mm"_s;

} // namespace

QJsonObject androidReminderItem(const ScheduledNotification &n)
{
    const int id = n.nativeId > 0 ? n.nativeId : nativeIdFor(n.id);
    // `at` is local wall-clock time; its epoch value is the moment that wall-clock time occurs
    // here (AlarmManager works in epoch time).
    return QJsonObject{
            {u"id"_s, id},
            {u"title"_s, n.title},
            {u"body"_s, n.body},
            {u"at"_s, QJsonValue(qint64(n.at.toMSecsSinceEpoch()))},
            {u"alarm"_s, n.alarm},
            {u"ringMinutes"_s, std::clamp(n.ringMinutes, 1, 60)},
            {u"path"_s, n.path},
            {u"line"_s, std::max(0, n.line)},
            {u"key"_s, n.id},
            {u"local"_s, n.at.toString(LocalFormat)},
            {u"task"_s, n.taskLine >= 0},
    };
}

QList<AndroidDoneRequest> androidDoneRequests(const QJsonArray &array)
{
    QList<AndroidDoneRequest> out;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        AndroidDoneRequest request;
        ScheduledNotification &n = request.notification;
        n.id = o.value(u"key"_s).toString();
        n.at = QDateTime::fromString(o.value(u"local"_s).toString(), LocalFormat); // local time
        n.path = o.value(u"path"_s).toString();
        n.line = std::max(0, o.value(u"line"_s).toInt());
        n.title = o.value(u"title"_s).toString();
        if (n.id.isEmpty() || n.path.isEmpty() || !n.at.isValid())
            continue;
        request.day = QDate::fromString(o.value(u"day"_s).toString(), Qt::ISODate);
        out.append(request);
    }
    return out;
}

QJsonArray androidReminderPayload(const QList<ScheduledNotification> &notifications)
{
    QJsonArray out;
    for (const ScheduledNotification &n : notifications) {
        if (n.at.isValid())
            out.append(androidReminderItem(n));
    }
    return out;
}

QJsonObject androidSoundItem(const SoundChoice &choice)
{
    return QJsonObject::fromVariantMap(soundToVariant(normalizedSound(choice)));
}

QJsonObject androidSoundSettings(const SoundChoice &reminder, const SoundChoice &alarm)
{
    return QJsonObject{
            {u"reminder"_s, androidSoundItem(reminder)},
            {u"alarm"_s, androidSoundItem(alarm)},
    };
}

QJsonObject androidSoundPreview(SoundKind kind, const SoundChoice &choice)
{
    QJsonObject item = androidSoundItem(choice);
    item.insert(u"kind"_s, soundKindKey(kind));
    return item;
}

SoundChoice androidPickedSound(const QJsonObject &answer, bool *ok)
{
    SoundChoice::Mode mode = SoundChoice::Mode::Default;
    const bool valid = !answer.contains(u"error"_s) && soundModeFromKey(answer.value(u"mode"_s).toString(), &mode);
    if (ok)
        *ok = valid;
    if (!valid)
        return {};
    return soundFromVariant(answer.toVariantMap());
}

} // namespace brulion::notify
