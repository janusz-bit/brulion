#include "caldav/store.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

QString dateText(const QDateTime &t)
{
    return t.isValid() ? t.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime dateFrom(const QJsonValue &v)
{
    const QString s = v.toString();
    if (s.isEmpty())
        return {};
    return QDateTime::fromString(s, Qt::ISODateWithMs);
}

QString policyName(ConflictPolicy p)
{
    return p == ConflictPolicy::KeepBoth ? u"both"_s : u"newer"_s;
}

QString kindName(EventChange::Kind k)
{
    switch (k) {
    case EventChange::Kind::Create: return u"create"_s;
    case EventChange::Kind::Update: return u"update"_s;
    case EventChange::Kind::Delete: return u"delete"_s;
    }
    return {};
}

QString logKindName(LogEntry::Kind k)
{
    switch (k) {
    case LogEntry::Kind::Refreshed: return u"refreshed"_s;
    case LogEntry::Kind::Synced: return u"synced"_s;
    case LogEntry::Kind::Conflict: return u"conflict"_s;
    case LogEntry::Kind::Deleted: return u"deleted"_s;
    case LogEntry::Kind::Error: return u"error"_s;
    }
    return {};
}

/// File names hold only safe characters (ids are ours, but a config from a broken file is not).
QString safeName(const QString &id)
{
    QString out;
    for (const QChar c : id) {
        if (c.isLetterOrNumber() || c == u'-' || c == u'_')
            out += c;
    }
    return out.left(64);
}

} // namespace

const Account *CalendarSettings::account(const QString &id) const
{
    const auto it = std::ranges::find(accounts, id, &Account::id);
    return it == accounts.end() ? nullptr : &*it;
}

const CalendarEntry *CalendarSettings::calendar(const QString &id) const
{
    const auto it = std::ranges::find(calendars, id, &CalendarEntry::id);
    return it == calendars.end() ? nullptr : &*it;
}

const Subscription *CalendarSettings::subscription(const QString &id) const
{
    const auto it = std::ranges::find(subscriptions, id, &Subscription::id);
    return it == subscriptions.end() ? nullptr : &*it;
}

QJsonObject CalendarSettings::toJson() const
{
    QJsonArray acc;
    for (const Account &a : accounts)
        acc.append(QJsonObject{{u"id"_s, a.id},
                               {u"server"_s, a.server},
                               {u"user"_s, a.user},
                               {u"principal"_s, a.principalUrl},
                               {u"home"_s, a.homeUrl},
                               {u"name"_s, a.name},
                               {u"conflicts"_s, policyName(a.conflicts)},
                               {u"interval"_s, a.intervalMinutes}});
    QJsonArray cals;
    for (const CalendarEntry &c : calendars)
        cals.append(QJsonObject{{u"id"_s, c.id},
                                {u"account"_s, c.account},
                                {u"url"_s, c.url},
                                {u"name"_s, c.name},
                                {u"color"_s, c.color},
                                {u"components"_s, QJsonArray::fromStringList(c.components)},
                                {u"readOnly"_s, c.readOnly},
                                {u"showEvents"_s, c.showEvents},
                                {u"alarms"_s, c.alarms},
                                {u"syncTasks"_s, c.syncTasks},
                                {u"taskPage"_s, c.taskPage},
                                {u"visible"_s, c.visible}});
    QJsonArray subs;
    for (const Subscription &s : subscriptions)
        subs.append(QJsonObject{{u"id"_s, s.id},
                                {u"url"_s, s.url},
                                {u"name"_s, s.name},
                                {u"color"_s, s.color},
                                {u"visible"_s, s.visible},
                                {u"refresh"_s, s.refreshMinutes}});
    return QJsonObject{{u"version"_s, 1}, {u"accounts"_s, acc}, {u"calendars"_s, cals}, {u"subscriptions"_s, subs}};
}

CalendarSettings CalendarSettings::fromJson(const QJsonObject &json)
{
    CalendarSettings s;
    for (const QJsonValue &v : json.value(u"accounts"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Account a;
        a.id = o.value(u"id"_s).toString();
        a.server = o.value(u"server"_s).toString();
        a.user = o.value(u"user"_s).toString();
        a.principalUrl = o.value(u"principal"_s).toString();
        a.homeUrl = o.value(u"home"_s).toString();
        a.name = o.value(u"name"_s).toString();
        a.conflicts = o.value(u"conflicts"_s).toString() == u"both" ? ConflictPolicy::KeepBoth : ConflictPolicy::Newer;
        a.intervalMinutes = std::clamp(o.value(u"interval"_s).toInt(15), 5, 1440);
        if (!a.id.isEmpty())
            s.accounts.append(a);
    }
    for (const QJsonValue &v : json.value(u"calendars"_s).toArray()) {
        const QJsonObject o = v.toObject();
        CalendarEntry c;
        c.id = o.value(u"id"_s).toString();
        c.account = o.value(u"account"_s).toString();
        c.url = o.value(u"url"_s).toString();
        c.name = o.value(u"name"_s).toString();
        c.color = o.value(u"color"_s).toString();
        for (const QJsonValue &comp : o.value(u"components"_s).toArray())
            c.components.append(comp.toString());
        c.readOnly = o.value(u"readOnly"_s).toBool();
        c.showEvents = o.value(u"showEvents"_s).toBool(true);
        c.alarms = o.value(u"alarms"_s).toBool(true);
        c.syncTasks = o.value(u"syncTasks"_s).toBool();
        c.taskPage = o.value(u"taskPage"_s).toString();
        c.visible = o.value(u"visible"_s).toBool(true);
        if (!c.id.isEmpty() && s.account(c.account))
            s.calendars.append(c);
    }
    for (const QJsonValue &v : json.value(u"subscriptions"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Subscription sub;
        sub.id = o.value(u"id"_s).toString();
        sub.url = o.value(u"url"_s).toString();
        sub.name = o.value(u"name"_s).toString();
        sub.color = o.value(u"color"_s).toString();
        sub.visible = o.value(u"visible"_s).toBool(true);
        sub.refreshMinutes = std::clamp(o.value(u"refresh"_s).toInt(360), 15, 10'080);
        if (!sub.id.isEmpty() && !sub.url.isEmpty())
            s.subscriptions.append(sub);
    }
    return s;
}

QJsonObject FeedMeta::toJson() const
{
    return QJsonObject{{u"etag"_s, etag},
                       {u"lastModified"_s, lastModified},
                       {u"fetchedAt"_s, dateText(fetchedAt)},
                       {u"attemptedAt"_s, dateText(attemptedAt)},
                       {u"error"_s, error},
                       {u"errorText"_s, errorText},
                       {u"cors"_s, cors},
                       {u"bytes"_s, bytes},
                       {u"events"_s, events}};
}

FeedMeta FeedMeta::fromJson(const QJsonObject &o)
{
    FeedMeta m;
    m.etag = o.value(u"etag"_s).toString();
    m.lastModified = o.value(u"lastModified"_s).toString();
    m.fetchedAt = dateFrom(o.value(u"fetchedAt"_s));
    m.attemptedAt = dateFrom(o.value(u"attemptedAt"_s));
    m.error = o.value(u"error"_s).toString();
    m.errorText = o.value(u"errorText"_s).toString();
    m.cors = o.value(u"cors"_s).toBool();
    m.bytes = o.value(u"bytes"_s).toInteger();
    m.events = o.value(u"events"_s).toInt();
    return m;
}

QJsonObject CalendarState::toJson() const
{
    QJsonObject res;
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it)
        res.insert(it.key(), QJsonObject{{u"etag"_s, it->etag},
                                         {u"uid"_s, it->uid},
                                         {u"kind"_s, it->kind},
                                         {u"data"_s, QString::fromUtf8(it->data)}});
    QJsonObject t;
    for (auto it = tasks.constBegin(); it != tasks.constEnd(); ++it)
        t.insert(it.key(), QJsonObject{{u"path"_s, it->path},
                                       {u"blockId"_s, it->blockId},
                                       {u"note"_s, it->note},
                                       {u"local"_s, it->local.toJson()},
                                       {u"remote"_s, it->remote.toJson()}});
    QJsonArray out;
    for (const EventChange &c : outbox)
        out.append(QJsonObject{{u"kind"_s, kindName(c.kind)},
                               {u"path"_s, c.path},
                               {u"uid"_s, c.uid},
                               {u"data"_s, QString::fromUtf8(c.data)},
                               {u"ifMatch"_s, c.ifMatch},
                               {u"at"_s, dateText(c.at)}});
    return QJsonObject{{u"version"_s, 1},       {u"syncToken"_s, syncToken}, {u"ctag"_s, ctag},
                       {u"resources"_s, res},    {u"tasks"_s, t},             {u"outbox"_s, out},
                       {u"syncedAt"_s, dateText(syncedAt)}, {u"error"_s, error}, {u"errorText"_s, errorText}};
}

CalendarState CalendarState::fromJson(const QJsonObject &o)
{
    CalendarState s;
    s.syncToken = o.value(u"syncToken"_s).toString();
    s.ctag = o.value(u"ctag"_s).toString();
    const QJsonObject res = o.value(u"resources"_s).toObject();
    for (auto it = res.constBegin(); it != res.constEnd(); ++it) {
        const QJsonObject r = it->toObject();
        s.resources.insert(it.key(), Resource{r.value(u"etag"_s).toString(), r.value(u"uid"_s).toString(),
                                              r.value(u"kind"_s).toString(), r.value(u"data"_s).toString().toUtf8()});
    }
    const QJsonObject t = o.value(u"tasks"_s).toObject();
    for (auto it = t.constBegin(); it != t.constEnd(); ++it) {
        const QJsonObject r = it->toObject();
        s.tasks.insert(it.key(), TrackedTask{r.value(u"path"_s).toString(), r.value(u"blockId"_s).toString(),
                                             r.value(u"note"_s).toString(),
                                             TaskFields::fromJson(r.value(u"local"_s).toObject()),
                                             TaskFields::fromJson(r.value(u"remote"_s).toObject())});
    }
    for (const QJsonValue &v : o.value(u"outbox"_s).toArray()) {
        const QJsonObject c = v.toObject();
        EventChange change;
        const QString kind = c.value(u"kind"_s).toString();
        change.kind = kind == u"create" ? EventChange::Kind::Create
                      : kind == u"delete" ? EventChange::Kind::Delete
                                          : EventChange::Kind::Update;
        change.path = c.value(u"path"_s).toString();
        change.uid = c.value(u"uid"_s).toString();
        change.data = c.value(u"data"_s).toString().toUtf8();
        change.ifMatch = c.value(u"ifMatch"_s).toString();
        change.at = dateFrom(c.value(u"at"_s));
        if (!change.path.isEmpty())
            s.outbox.append(change);
    }
    s.syncedAt = dateFrom(o.value(u"syncedAt"_s));
    s.error = o.value(u"error"_s).toString();
    s.errorText = o.value(u"errorText"_s).toString();
    return s;
}

QJsonObject LogEntry::toJson() const
{
    return QJsonObject{{u"at"_s, dateText(at)}, {u"kind"_s, logKindName(kind)}, {u"calendar"_s, calendar},
                       {u"name"_s, name},       {u"code"_s, code},            {u"text"_s, text},
                       {u"down"_s, downloaded}, {u"up"_s, uploaded},          {u"deleted"_s, deleted}};
}

LogEntry LogEntry::fromJson(const QJsonObject &o)
{
    LogEntry e;
    e.at = dateFrom(o.value(u"at"_s));
    const QString kind = o.value(u"kind"_s).toString();
    e.kind = kind == u"refreshed"  ? Kind::Refreshed
             : kind == u"conflict" ? Kind::Conflict
             : kind == u"deleted"  ? Kind::Deleted
             : kind == u"error"    ? Kind::Error
                                   : Kind::Synced;
    e.calendar = o.value(u"calendar"_s).toString();
    e.name = o.value(u"name"_s).toString();
    e.code = o.value(u"code"_s).toString();
    e.text = o.value(u"text"_s).toString();
    e.downloaded = o.value(u"down"_s).toInt();
    e.uploaded = o.value(u"up"_s).toInt();
    e.deleted = o.value(u"deleted"_s).toInt();
    return e;
}

CalendarStore::CalendarStore(QString folder) : m_folder(std::move(folder))
{
    reload();
}

void CalendarStore::reload()
{
    if (m_folder.isEmpty())
        return;
    m_states.clear();
    m_settings = CalendarSettings::fromJson(QJsonDocument::fromJson(read(u"calendars.json"_s)).object());
    m_log.clear();
    for (const QJsonValue &v : QJsonDocument::fromJson(read(u"log.json"_s)).array())
        m_log.append(LogEntry::fromJson(v.toObject()));
}

QString CalendarStore::path(const QString &name) const
{
    return m_folder + u'/' + name;
}

bool CalendarStore::write(const QString &name, const QByteArray &data) const
{
    if (m_folder.isEmpty())
        return true;
    if (!QDir().mkpath(m_folder))
        return false;
    QFile::setPermissions(m_folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path(name));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    if (!f.commit())
        return false;
    QFile::setPermissions(path(name), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QByteArray CalendarStore::read(const QString &name) const
{
    if (m_folder.isEmpty())
        return {};
    QFile f(path(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool CalendarStore::saveSettings(const CalendarSettings &settings)
{
    m_settings = settings;
    return write(u"calendars.json"_s, QJsonDocument(settings.toJson()).toJson(QJsonDocument::Indented));
}

QByteArray CalendarStore::feedData(const QString &id) const
{
    if (m_folder.isEmpty())
        return m_feeds.value(id);
    return read(safeName(id) + u".ics"_s);
}

FeedMeta CalendarStore::feedMeta(const QString &id) const
{
    if (m_folder.isEmpty())
        return m_metas.value(id);
    return FeedMeta::fromJson(QJsonDocument::fromJson(read(safeName(id) + u".meta.json"_s)).object());
}

bool CalendarStore::saveFeed(const QString &id, const QByteArray *data, const FeedMeta &meta)
{
    if (m_folder.isEmpty()) {
        if (data)
            m_feeds.insert(id, *data);
        m_metas.insert(id, meta);
        return true;
    }
    bool ok = true;
    if (data)
        ok = write(safeName(id) + u".ics"_s, *data);
    return write(safeName(id) + u".meta.json"_s, QJsonDocument(meta.toJson()).toJson(QJsonDocument::Compact)) && ok;
}

CalendarState CalendarStore::state(const QString &calendarId) const
{
    const auto it = m_states.constFind(calendarId);
    if (it != m_states.constEnd())
        return *it;
    CalendarState s;
    if (!m_folder.isEmpty())
        s = CalendarState::fromJson(QJsonDocument::fromJson(read(safeName(calendarId) + u".state.json"_s)).object());
    m_states.insert(calendarId, s);
    return s;
}

bool CalendarStore::saveState(const QString &calendarId, const CalendarState &state)
{
    m_states.insert(calendarId, state);
    return write(safeName(calendarId) + u".state.json"_s, QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact));
}

void CalendarStore::forget(const QString &id)
{
    m_states.remove(id);
    m_feeds.remove(id);
    m_metas.remove(id);
    if (m_folder.isEmpty())
        return;
    const QString name = safeName(id);
    QFile::remove(path(name + u".ics"_s));
    QFile::remove(path(name + u".meta.json"_s));
    QFile::remove(path(name + u".state.json"_s));
}

void CalendarStore::append(const LogEntry &entry)
{
    m_log.append(entry);
    while (m_log.size() > MaxLogEntries)
        m_log.removeFirst();
    QJsonArray list;
    for (const LogEntry &e : std::as_const(m_log))
        list.append(e.toJson());
    write(u"log.json"_s, QJsonDocument(list).toJson(QJsonDocument::Compact));
}

void CalendarStore::clearLog()
{
    m_log.clear();
    write(u"log.json"_s, "[]");
}

QString CalendarStore::newId(QChar kind)
{
    static constexpr char Alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString id(kind);
    id += u'-';
    for (int i = 0; i < 10; ++i)
        id += QChar(QLatin1Char(Alphabet[QRandomGenerator::global()->bounded(36)]));
    return id;
}

} // namespace brulion::caldav
