#include "notificationcenter.h"

#include "vault/localstore.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The notebook's device state key of the log.
const QString LogKey = u"notify/log"_s;
constexpr qint64 Hour = 3600 * 1000;

const QList<std::pair<NotificationCenter::Kind, QString>> &kindNames()
{
    using Kind = NotificationCenter::Kind;
    static const QList<std::pair<Kind, QString>> names{
            {Kind::Reminder, u"reminder"_s},   {Kind::Chat, u"chat"_s},     {Kind::Agent, u"agent"_s},
            {Kind::Extension, u"extension"_s}, {Kind::Script, u"script"_s}, {Kind::Timer, u"timer"_s}};
    return names;
}

std::optional<NotificationCenter::Kind> kindOf(const QString &name)
{
    for (const auto &[kind, n] : kindNames())
        if (n == name)
            return kind;
    return std::nullopt;
}

} // namespace

NotificationCenter::NotificationCenter(std::function<QDateTime()> clock, QObject *parent)
    : QObject(parent), m_clock(std::move(clock))
{ }

QString NotificationCenter::kindName(Kind kind)
{
    for (const auto &[k, name] : kindNames())
        if (k == kind)
            return name;
    return {};
}

void NotificationCenter::attach(LocalStore *store)
{
    m_store = store;
    m_entries.clear();
    m_posted.clear();
    load();
    recount();
}

void NotificationCenter::detach()
{
    attach(nullptr);
}

bool NotificationCenter::appActive() const
{
    return m_appActive ? m_appActive()
                       : qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr
                                 && QGuiApplication::applicationState() == Qt::ApplicationActive;
}

void NotificationCenter::setAllowed(Kind kind, bool allowed)
{
    if (kind == Kind::Agent)
        m_agents = allowed;
    else if (kind == Kind::Extension)
        m_extensions = allowed;
}

bool NotificationCenter::allowed(Kind kind) const
{
    return kind == Kind::Agent ? m_agents : kind == Kind::Extension ? m_extensions : true;
}

QString NotificationCenter::clean(const QString &text, int max)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text)
        if (c == u'\n' || c == u'\t' || c.category() != QChar::Other_Control)
            out += c; // (no other control characters: a terminal's escapes, a carriage return)
    out = out.trimmed();
    while (out.contains(u"\n\n\n"_s))
        out.replace(u"\n\n\n"_s, u"\n\n"_s);
    if (out.size() <= max)
        return out;
    out.truncate(max - 1);
    if (!out.isEmpty() && out.back().isHighSurrogate())
        out.chop(1);
    return out.trimmed() + u'…';
}

int NotificationCenter::record(Kind kind, const QString &source, const QString &title, const QString &text,
                               const QString &open)
{
    Entry e;
    e.kind = kind;
    e.source = source;
    e.title = clean(title, 200).replace(u'\n', u' ');
    e.text = clean(text, 4000);
    e.open = open;
    // (Seen when it came while the app was in front: the window showed it.)
    e.read = appActive();
    return add(std::move(e));
}

QString NotificationCenter::post(const Post &post, qint64 *retryMs)
{
    if (!allowed(post.kind))
        return u"off"_s;
    const QString body = clean(post.text, MaxText);
    if (body.isEmpty())
        return u"empty"_s;
    // At most MaxPerHour of one source an hour (the workspace's clock): one that posts in a loop
    // cannot flood the user.
    const qint64 now = m_clock().toMSecsSinceEpoch();
    QList<qint64> &times = m_posted[kindName(post.kind) + u':' + post.key.toLower()];
    times.removeIf([now](qint64 at) { return now - at >= Hour || at > now; });
    if (times.size() >= MaxPerHour) {
        if (retryMs)
            *retryMs = std::max<qint64>(1000, times.first() + Hour - now);
        return u"limit"_s;
    }
    times.append(now);
    // Who sends it is always said.
    const QString heading = clean(post.title, MaxTitle).replace(u'\n', u' ');
    const QString who = clean(post.who, MaxTitle).replace(u'\n', u' ');
    QString title = heading;
    if (!who.isEmpty())
        title = heading.isEmpty() ? who
                                  : tr("%1: %2", "a notification's title: who sends it, its title").arg(who, heading);
    else if (title.isEmpty())
        title = u"Brulion"_s;
    const bool inApp = appActive();
    Entry e;
    e.kind = post.kind;
    e.source = who;
    e.title = title;
    e.text = body;
    e.open = post.open;
    e.read = inApp;
    add(std::move(e));
    Q_EMIT shown(kindName(post.kind), post.open, title, body, inApp);
    return {};
}

int NotificationCenter::add(Entry entry)
{
    entry.id = m_nextId++;
    entry.at = m_clock();
    const int id = entry.id;
    m_entries.prepend(std::move(entry));
    prune();
    save();
    recount();
    return id;
}

void NotificationCenter::prune()
{
    const QDateTime oldest = m_clock().addDays(-MaxAgeDays);
    m_entries.removeIf([&oldest](const Entry &e) { return e.at < oldest; });
    if (m_entries.size() > MaxEntries)
        m_entries.resize(MaxEntries);
}

QVariantList NotificationCenter::entries(int revision, bool unreadOnly) const
{
    Q_UNUSED(revision);
    QVariantList out;
    const QDate today = m_clock().date();
    for (const Entry &e : m_entries) {
        if (unreadOnly && e.read)
            continue;
        const QDate day = e.at.date();
        const QString section = day == today               ? u"today"_s
                                : day == today.addDays(-1) ? u"yesterday"_s
                                                           : day.toString(u"yyyy-MM-dd"_s);
        out.append(QVariantMap{{u"entryId"_s, e.id},
                               {u"kind"_s, kindName(e.kind)},
                               {u"source"_s, e.source},
                               {u"title"_s, e.title},
                               {u"text"_s, e.text},
                               {u"open"_s, e.open},
                               {u"read"_s, e.read},
                               {u"at"_s, e.at.toString(u"yyyy-MM-ddTHH:mm"_s)},
                               {u"time"_s, e.at.toString(u"HH:mm"_s)},
                               {u"day"_s, e.at.toString(u"yyyy-MM-dd"_s)},
                               {u"section"_s, section}});
    }
    return out;
}

void NotificationCenter::markRead(int id)
{
    for (Entry &e : m_entries) {
        if (e.id == id && !e.read) {
            e.read = true;
            save();
            recount();
            return;
        }
    }
}

void NotificationCenter::markAllRead()
{
    bool any = false;
    for (Entry &e : m_entries)
        any = std::exchange(e.read, true) == false || any;
    if (!any)
        return;
    save();
    recount();
}

void NotificationCenter::markOpened(const QString &open)
{
    if (open.isEmpty())
        return;
    bool any = false;
    for (Entry &e : m_entries) {
        if (!e.read && e.open.compare(open, Qt::CaseInsensitive) == 0) {
            e.read = true;
            any = true;
        }
    }
    if (!any)
        return;
    save();
    recount();
}

void NotificationCenter::remove(int id)
{
    if (m_entries.removeIf([id](const Entry &e) { return e.id == id; }) == 0)
        return;
    save();
    recount();
}

void NotificationCenter::clear()
{
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    save();
    recount();
}

void NotificationCenter::recount()
{
    m_unread = int(std::ranges::count_if(m_entries, [](const Entry &e) { return !e.read; }));
    ++m_revision;
    Q_EMIT changed();
}

void NotificationCenter::load()
{
    if (!m_store)
        return;
    for (const QJsonValue &v : m_store->get(LogKey).toArray()) {
        const QJsonObject o = v.toObject();
        const auto kind = kindOf(o.value(u"kind"_s).toString());
        const QDateTime at = QDateTime::fromString(o.value(u"at"_s).toString(), Qt::ISODate);
        if (!kind || !at.isValid())
            continue; // (one written by a later version, or broken: left out)
        Entry e;
        e.id = m_nextId++;
        e.kind = *kind;
        e.source = o.value(u"source"_s).toString().left(MaxTitle);
        e.title = o.value(u"title"_s).toString().left(200);
        e.text = o.value(u"text"_s).toString().left(4000);
        e.open = o.value(u"open"_s).toString();
        e.at = at;
        e.read = o.value(u"read"_s).toBool();
        m_entries.append(std::move(e));
    }
    std::ranges::stable_sort(m_entries, [](const Entry &a, const Entry &b) { return a.at > b.at; });
    prune();
}

void NotificationCenter::save()
{
    if (!m_store)
        return;
    QJsonArray all;
    for (const Entry &e : std::as_const(m_entries)) {
        QJsonObject o{{u"kind"_s, kindName(e.kind)},
                      {u"title"_s, e.title},
                      {u"text"_s, e.text},
                      {u"at"_s, e.at.toString(Qt::ISODate)}};
        if (!e.source.isEmpty())
            o.insert(u"source"_s, e.source);
        if (!e.open.isEmpty())
            o.insert(u"open"_s, e.open);
        if (e.read)
            o.insert(u"read"_s, true);
        all.append(o);
    }
    m_store->set(LogKey, all.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(all));
}

} // namespace brulion::shell
