#include "caldav/calendarsync.h"

#include "caldav/tasksync_p.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "log/redact.h"
#include "text/text.h"
#include "vault/vault.h"

#include <QDir>
#include <QLockFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

QString fileNameFor(const QString &uid)
{
    QString out;
    for (const QChar c : uid) {
        if (c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'.')
            out += c;
        else
            out += u'-';
    }
    if (out.isEmpty())
        out = u"event"_s;
    return out.left(120) + u".ics"_s;
}

QString calendarPath(const QString &url)
{
    QString p = QUrl(url).path(QUrl::FullyDecoded);
    if (!p.endsWith(u'/'))
        p += u'/';
    return p;
}

/// The kind ("VEVENT", "VTODO") and UID of a member's data.
std::pair<QString, QString> kindOf(const QByteArray &data)
{
    const QList<io::ical::Component> roots = io::ical::parse(data);
    for (const io::ical::Component &root : roots) {
        for (const io::ical::Component &c : root.children) {
            if (c.name == u"VEVENT" || c.name == u"VTODO")
                return {c.name, c.text(u"UID").trimmed()};
        }
    }
    return {};
}

QDateTime lastModifiedOf(const QByteArray &data)
{
    const QList<io::ical::Component> roots = io::ical::parse(data);
    for (const io::ical::Component &root : roots) {
        for (const io::ical::Component &c : root.children) {
            if (c.name != u"VEVENT" && c.name != u"VTODO")
                continue;
            for (const QStringView name : {u"LAST-MODIFIED", u"DTSTAMP"}) {
                if (const io::ical::Property *p = c.property(name)) {
                    if (const auto v = io::ical::parseDateTime(*p); v && v->time && v->utc)
                        return QDateTime(v->date, *v->time, QTimeZone::UTC);
                }
            }
        }
    }
    return {};
}

QString summaryOf(const QByteArray &data)
{
    const QList<io::ical::Component> roots = io::ical::parse(data);
    for (const io::ical::Component &root : roots) {
        for (const io::ical::Component &c : root.children) {
            if (c.name == u"VEVENT" || c.name == u"VTODO")
                return c.text(u"SUMMARY").simplified();
        }
    }
    return {};
}

/// `data` with every UID replaced by `uid` (a copy of an event: "keep both").
QByteArray withUid(const QByteArray &data, const QString &uid)
{
    QList<io::ical::Component> roots = io::ical::parse(data);
    for (io::ical::Component &root : roots) {
        for (io::ical::Component &c : root.children) {
            for (io::ical::Property &p : c.properties) {
                if (p.name == u"UID")
                    p.value = io::ical::escapeText(uid);
            }
        }
    }
    return serialize(roots);
}

bool fatal(const sync::WebDavError &e)
{
    return e.is(sync::WebDavErrorCode::Network) || e.is(sync::WebDavErrorCode::Timeout);
}

} // namespace

sync::SyncReport CalendarRunReport::toSyncReport() const
{
    sync::SyncReport r;
    r.uploaded = uploaded;
    r.downloaded = downloaded + refreshed;
    r.deletedLocal = deletedLocal;
    r.deletedRemote = deletedRemote;
    r.merged = merged;
    for (const QString &c : conflicts)
        r.conflicts.append(c);
    for (const QString &id : failed)
        r.errors.append(sync::SyncFileError{id, error.message, sync::errorCodeName(error.code), error.status});
    // Only a dead network stops the scheduler's interval (back-off); a calendar that fails is a
    // per-item error.
    if (fatal(error) && refreshed == 0 && downloaded == 0 && uploaded == 0)
        r.remoteError = error;
    r.needsConfirmation = needsConfirmation;
    for (const QString &t : pendingDeletions)
        r.pendingDeletions.append(sync::SyncChange{t, sync::SyncAction::DeleteLocal});
    r.cancelled = cancelled;
    r.started = started;
    r.finished = finished;
    return r;
}

struct CalendarSync::Run {
    /// One process of the device syncs a notebook's calendars at a time (the app, Android's
    /// background check): held for the run.
    std::unique_ptr<QLockFile> lock;
    bool manual = false;
    bool confirm = false;
    QStringList queue; ///< "s:<id>" subscriptions, "c:<id>" calendars
    CalendarRunReport report;
    std::unique_ptr<FeedFetcher> fetcher;
};

CalendarSync::CalendarSync(CalendarStore *store, QObject *parent) : QObject(parent), m_store(store)
{
#if QT_CONFIG(timezone)
    m_zone = QTimeZone::systemTimeZone();
#else
    m_zone = QTimeZone(QTimeZone::LocalTime); // (no database: the browser's own local time)
#endif
}

CalendarSync::~CalendarSync() = default;

void CalendarSync::setVault(Vault *vault, VaultIndex *index)
{
    m_vault = vault;
    m_index = index;
}

void CalendarSync::setTransports(sync::HttpTransport *caldav, sync::HttpTransport *feeds)
{
    m_caldav = caldav;
    m_feeds = feeds;
    m_clients.clear();
}

void CalendarSync::setPasswords(std::function<QString(const QString &)> passwords)
{
    m_passwords = std::move(passwords);
    m_clients.clear();
}

QDateTime CalendarSync::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void CalendarSync::startSync(const sync::SyncRunOptions &options, sync::SyncDoneCallback done)
{
    const bool manual = options.trigger == sync::SyncTrigger::Manual || options.trigger == sync::SyncTrigger::Confirmation;
    run(manual, options.confirmDeletions || options.allowEmptySide,
        [done = std::move(done)](const CalendarRunReport &report) { done(report.toSyncReport()); });
}

void CalendarSync::cancelSync()
{
    if (m_running)
        m_cancel = true;
}

bool CalendarSync::begin()
{
    m_run = std::make_unique<Run>();
    m_run->report.started = now();
    if (m_store->folder().isEmpty())
        return true;
    QDir().mkpath(m_store->folder());
    m_run->lock = std::make_unique<QLockFile>(m_store->folder() + u"/sync.lock"_s);
    m_run->lock->setStaleLockTime(0); // only a dead owner makes the lock stale, never its age
    if (!m_run->lock->tryLock(0)) {
        if (m_run->lock->error() != QLockFile::LockFailedError) {
            // The folder cannot hold a lock file (the browser's storage, a read-only folder):
            // nobody to share it with — without one.
            m_run->lock.reset();
        } else {
            m_run->lock.reset();
            m_run->report.error = sync::WebDavError::make(sync::WebDavErrorCode::Cancelled,
                                                          u"Another process of this device syncs the calendars"_s);
            m_run->report.cancelled = true;
            m_running = true;
            Q_EMIT runningChanged();
            QTimer::singleShot(0, this, [this] { finish(); });
            return false;
        }
    }
    // What another process wrote meanwhile.
    m_store->reload();
    return true;
}

void CalendarSync::run(bool manual, bool confirmDeletions, std::function<void(const CalendarRunReport &)> done)
{
    if (done)
        m_waiters.append(std::move(done));
    if (m_running)
        return;
    if (!begin())
        return;
    m_run->manual = manual;
    m_run->confirm = confirmDeletions;
    const CalendarSettings settings = m_store->settings();
    const QDateTime at = now();
    for (const Subscription &s : settings.subscriptions) {
        const FeedMeta meta = m_store->feedMeta(s.id);
        const QDateTime last = meta.attemptedAt.isValid() ? meta.attemptedAt : meta.fetchedAt;
        const bool due = manual || !last.isValid() || last.secsTo(at) >= qint64(s.refreshMinutes) * 60
                         || (!meta.error.isEmpty() && last.secsTo(at) >= 15 * 60);
        if (due)
            m_run->queue.append(u"s:"_s + s.id);
    }
    for (const CalendarEntry &c : settings.calendars)
        m_run->queue.append(u"c:"_s + c.id);
    m_running = true;
    m_cancel = false;
    Q_EMIT runningChanged();
    QTimer::singleShot(0, this, [this] { next(); });
}

void CalendarSync::runOne(const QString &id, std::function<void(const CalendarRunReport &)> done)
{
    if (done)
        m_waiters.append(std::move(done));
    if (m_running)
        return; // the running run covers it (or the next one will)
    if (!begin())
        return;
    m_run->manual = true;
    const CalendarSettings settings = m_store->settings();
    if (settings.subscription(id))
        m_run->queue.append(u"s:"_s + id);
    else if (settings.calendar(id))
        m_run->queue.append(u"c:"_s + id);
    m_running = true;
    m_cancel = false;
    Q_EMIT runningChanged();
    QTimer::singleShot(0, this, [this] { next(); });
}

void CalendarSync::next()
{
    if (!m_run)
        return;
    if (m_cancel) {
        m_run->report.cancelled = true;
        finish();
        return;
    }
    if (m_run->queue.isEmpty()) {
        finish();
        return;
    }
    const QString item = m_run->queue.takeFirst();
    const CalendarSettings settings = m_store->settings();
    if (item.startsWith(u"s:"_s)) {
        if (const Subscription *s = settings.subscription(item.sliced(2))) {
            refresh(*s);
            return;
        }
    } else if (const CalendarEntry *c = settings.calendar(item.sliced(2))) {
        syncCalendar(*c);
        return;
    }
    QTimer::singleShot(0, this, [this] { next(); });
}

void CalendarSync::finish()
{
    CalendarRunReport report = m_run ? m_run->report : CalendarRunReport{};
    report.finished = now();
    m_run.reset();
    m_running = false;
    m_cancel = false;
    m_last = report;
    const auto waiters = std::exchange(m_waiters, {});
    Q_EMIT runningChanged();
    for (const auto &w : waiters)
        w(report);
}

void CalendarSync::log(LogEntry entry)
{
    if (!entry.at.isValid())
        entry.at = now();
    m_store->append(entry);
    Q_EMIT logChanged();
}

std::shared_ptr<CalDavClient> CalendarSync::client(const Account &account, QString *problem)
{
    const QString password = m_passwords ? m_passwords(account.id) : QString();
    if (password.isEmpty()) {
        *problem = u"auth"_s;
        return nullptr;
    }
    if (!m_caldav) {
        *problem = u"not-ready"_s;
        return nullptr;
    }
    const QString key = account.server + u'|' + account.user + u'|' + text::contentHash(password);
    ClientEntry &e = m_clients[account.id];
    if (!e.client || e.key != key) {
        e.key = key;
        const QUrl base = QUrl(account.homeUrl.isEmpty() ? account.server : account.homeUrl);
        e.client = std::make_shared<CalDavClient>(m_caldav, base, account.user, password);
        e.client->setBrowser(m_browser);
    }
    return e.client;
}

void CalendarSync::discover(const QString &server, const QString &user, const QString &password,
                            std::function<void(const Discovery &)> done)
{
    if (!m_caldav) {
        Discovery d;
        d.error = sync::WebDavError::make(sync::WebDavErrorCode::Network, u"no transport"_s);
        done(d);
        return;
    }
    QString address = server.trimmed();
    if (!address.contains(u"://"_s))
        address.prepend(u"https://"_s);
    auto c = std::make_shared<CalDavClient>(m_caldav, QUrl(address), user, password);
    c->setBrowser(m_browser);
    c->discover([c, done = std::move(done)](const Discovery &d) mutable {
        done(d);
        // `c` is released after this callback returns (the lambda that holds it is destroyed).
    });
}

// ---- subscriptions ------------------------------------------------------------------------------

void CalendarSync::refresh(const Subscription &subscription)
{
    if (!m_feeds) {
        QTimer::singleShot(0, this, [this] { next(); });
        return;
    }
    if (!m_run->fetcher)
        m_run->fetcher = std::make_unique<FeedFetcher>(m_feeds);
    m_run->fetcher->setBrowser(m_browser);
    FeedMeta meta = m_store->feedMeta(subscription.id);
    const bool haveCopy = !m_store->feedData(subscription.id).isEmpty();
    const QString etag = haveCopy ? meta.etag : QString();
    const QString lastModified = haveCopy ? meta.lastModified : QString();
    const QString id = subscription.id;
    m_run->fetcher->fetch(QUrl(subscription.url), etag, lastModified, [this, id, meta](const FeedResult &result) mutable {
        if (!m_run)
            return;
        CalendarSettings settings = m_store->settings();
        const Subscription *sub = settings.subscription(id);
        const QString name = sub ? sub->name : QString();
        meta.attemptedAt = now();
        if (!result.ok()) {
            const QString code = sync::errorCodeName(result.error.code);
            const bool repeated = meta.error == code;
            meta.error = code;
            meta.errorText = result.error.message;
            meta.cors = result.maybeCors;
            m_store->saveFeed(id, nullptr, meta);
            m_run->report.failed.append(id);
            if (!m_run->report.error.isError())
                m_run->report.error = result.error;
            if (!repeated || m_run->manual) {
                LogEntry e;
                e.kind = LogEntry::Kind::Error;
                e.calendar = id;
                e.name = name;
                e.code = result.maybeCors ? u"cors"_s : code;
                e.text = logging::redact(result.error.message);
                log(e);
            }
            qCInfo(logging::lcSync).noquote() << "Calendar subscription" << name << "failed:" << code
                                              << logging::redact(result.error.message);
            Q_EMIT calendarChanged(id);
            next();
            return;
        }
        meta.error.clear();
        meta.errorText.clear();
        meta.cors = false;
        meta.fetchedAt = meta.attemptedAt;
        ++m_run->report.refreshed;
        if (result.notModified) {
            meta.etag = result.etag;
            meta.lastModified = result.lastModified;
            m_store->saveFeed(id, nullptr, meta);
            Q_EMIT calendarChanged(id);
            next();
            return;
        }
        const CalendarData data = parseCalendarData(result.data);
        meta.etag = result.etag;
        meta.lastModified = result.lastModified;
        meta.bytes = result.data.size();
        meta.events = int(data.events.size());
        m_store->saveFeed(id, &result.data, meta);
        // A subscription without a name or colour of its own takes the feed's.
        if (sub) {
            Subscription updated = *sub;
            if (updated.name.isEmpty())
                updated.name = data.name.isEmpty() ? QUrl(updated.url).host() : data.name;
            if (updated.color.isEmpty() && !data.color.isEmpty())
                updated.color = data.color;
            if (!(updated == *sub)) {
                for (Subscription &s : settings.subscriptions) {
                    if (s.id == id)
                        s = updated;
                }
                m_store->saveSettings(settings);
            }
        }
        m_run->report.changedCalendars.append(id);
        LogEntry e;
        e.kind = LogEntry::Kind::Refreshed;
        e.calendar = id;
        e.name = name.isEmpty() ? data.name : name;
        e.downloaded = meta.events;
        e.text = data.truncated ? u"truncated"_s : QString();
        log(e);
        Q_EMIT calendarChanged(id);
        next();
    });
}

// ---- CalDAV calendars -----------------------------------------------------------------------------

namespace {

struct Job {
    CalendarEntry entry;
    Account account;
    std::shared_ptr<CalDavClient> client;
    CalendarState state;
    ChangeSet changes;
    int outboxIndex = 0;
    QList<EventChange> keep; ///< outbox entries not sent (failed: retried next run)
    std::unique_ptr<detail::TaskReconciler> tasks;
    QList<detail::RemoteOp> ops;
    int opIndex = 0;
    int uploaded = 0;
    int deletedRemote = 0;
    int events = 0;
    bool eventsChanged = false;
    QStringList conflicts;
    sync::WebDavError error;
};

} // namespace

void CalendarSync::syncCalendar(const CalendarEntry &entry)
{
    const CalendarSettings settings = m_store->settings();
    const Account *account = settings.account(entry.account);
    QString problem;
    const std::shared_ptr<CalDavClient> c = account ? client(*account, &problem) : nullptr;
    if (!c) {
        m_run->report.failed.append(entry.id);
        CalendarState state = m_store->state(entry.id);
        if (state.error != problem) {
            state.error = problem.isEmpty() ? u"not-ready"_s : problem;
            state.errorText = u"No password for this account"_s;
            m_store->saveState(entry.id, state);
            LogEntry e;
            e.kind = LogEntry::Kind::Error;
            e.calendar = entry.id;
            e.name = entry.name;
            e.code = state.error;
            log(e);
        }
        QTimer::singleShot(0, this, [this] { next(); });
        return;
    }
    auto job = std::make_shared<Job>();
    job->entry = entry;
    job->account = *account;
    job->client = c;
    job->state = m_store->state(entry.id);
    const QUrl url(entry.url);
    const ConflictPolicy policy = account->conflicts;

    // Steps, last to first.
    auto finishJob = [this, job]() {
        CalendarRunReport &report = m_run->report;
        job->state.syncedAt = now();
        if (job->error.isError()) {
            job->state.error = sync::errorCodeName(job->error.code);
            job->state.errorText = logging::redact(job->error.message);
            report.failed.append(job->entry.id);
            if (!report.error.isError())
                report.error = job->error;
            LogEntry e;
            e.kind = LogEntry::Kind::Error;
            e.calendar = job->entry.id;
            e.name = job->entry.name;
            e.code = job->state.error;
            e.text = job->state.errorText;
            log(e);
        } else {
            job->state.error.clear();
            job->state.errorText.clear();
        }
        int downloaded = job->events;
        int deletedLocal = 0;
        if (job->tasks) {
            const detail::TaskReport &t = job->tasks->report();
            downloaded += t.downloaded;
            deletedLocal = t.deletedLocal;
            report.merged += t.merged;
            report.changedNotes += t.changedNotes;
            if (t.needsConfirmation) {
                report.needsConfirmation = true;
                report.pendingDeletions += t.pendingDeletions;
                LogEntry e;
                e.kind = LogEntry::Kind::Deleted;
                e.calendar = job->entry.id;
                e.name = job->entry.name;
                e.code = u"held"_s;
                e.text = t.pendingDeletions.join(u", "_s);
                e.deleted = int(t.pendingDeletions.size());
                log(e);
            }
            for (const QString &c : t.conflicts) {
                LogEntry e;
                e.kind = LogEntry::Kind::Conflict;
                e.calendar = job->entry.id;
                e.name = job->entry.name;
                e.code = job->account.conflicts == ConflictPolicy::KeepBoth ? u"both"_s : u"newer"_s;
                e.text = c;
                log(e);
                report.conflicts.append(c);
            }
            for (const QString &r : t.restored) {
                LogEntry e;
                e.kind = LogEntry::Kind::Conflict;
                e.calendar = job->entry.id;
                e.name = job->entry.name;
                e.code = u"restored"_s;
                e.text = r;
                log(e);
            }
            for (const QString &err : t.errors)
                qCWarning(logging::lcSync).noquote() << "Calendar" << job->entry.name << ":" << err;
        }
        for (const QString &c : std::as_const(job->conflicts))
            report.conflicts.append(c);
        report.downloaded += downloaded;
        report.uploaded += job->uploaded;
        report.deletedLocal += deletedLocal;
        report.deletedRemote += job->deletedRemote;
        if (job->eventsChanged)
            report.changedCalendars.append(job->entry.id);
        m_store->saveState(job->entry.id, job->state);
        if (downloaded || job->uploaded || deletedLocal || job->deletedRemote) {
            LogEntry e;
            e.kind = LogEntry::Kind::Synced;
            e.calendar = job->entry.id;
            e.name = job->entry.name;
            e.downloaded = downloaded;
            e.uploaded = job->uploaded;
            e.deleted = deletedLocal + job->deletedRemote;
            log(e);
        }
        if (job->eventsChanged)
            Q_EMIT calendarChanged(job->entry.id);
        next();
    };

    // 4. The tasks' writes to the server, one by one.
    auto sendOps = std::make_shared<std::function<void()>>();
    *sendOps = [this, job, finishJob, weak = std::weak_ptr(sendOps)]() {
        const auto self = weak.lock();
        if (!m_run || !self)
            return;
        if (job->opIndex >= job->ops.size() || m_cancel) {
            finishJob();
            return;
        }
        const detail::RemoteOp op = job->ops.at(job->opIndex++);
        if (op.kind == detail::RemoteOp::Kind::Delete) {
            job->client->remove(op.path, op.ifMatch, [job, op, self](const sync::WebDavError &e) {
                job->tasks->remoteDone(op, {}, {}, !e.isError(), e.status);
                if (!e.isError())
                    ++job->deletedRemote;
                (*self)();
            });
            return;
        }
        job->client->put(op.path, op.data, op.kind == detail::RemoteOp::Kind::Create ? QString() : op.ifMatch,
                         [job, op, self](const WriteResult &r) {
                             job->tasks->remoteDone(op, r.etag, r.path, !r.error.isError(), r.error.status);
                             if (!r.error.isError())
                                 ++job->uploaded;
                             else if (fatal(r.error) && !job->error.isError())
                                 job->error = r.error;
                             (*self)();
                         });
    };

    // 3. The tasks reconciled with the notes.
    auto reconcile = [this, job, policy, sendOps, finishJob]() {
        if (!job->entry.syncTasks || !m_vault) {
            finishJob();
            return;
        }
        detail::TaskSyncInput in;
        in.entry = job->entry;
        in.policy = policy;
        in.vault = m_vault;
        in.index = m_index;
        in.zone = m_zone;
        in.now = now();
        in.confirmDeletions = m_run->confirm;
        const CalendarSettings all = m_store->settings();
        for (const CalendarEntry &other : all.calendars) {
            if (other.id == job->entry.id)
                continue;
            for (const TrackedTask &t : m_store->state(other.id).tasks)
                in.claimedIds.insert(t.blockId);
        }
        job->tasks = std::make_unique<detail::TaskReconciler>(in, job->state);
        job->tasks->run();
        job->ops = job->tasks->remoteOps();
        (*sendOps)();
    };

    // 2. The server's changes.
    auto fetchChanges = [this, job, url, reconcile, finishJob]() {
        job->client->changes(url, job->state.syncToken, [this, job, url, reconcile, finishJob](const ChangeSet &cs) {
            if (!m_run)
                return;
            if (cs.error.isError()) {
                job->error = cs.error;
                finishJob();
                return;
            }
            QStringList toFetch;
            QStringList removed = cs.removed;
            QSet<QString> listed;
            for (const RemoteResource &r : cs.changed) {
                listed.insert(r.path);
                const auto known = job->state.resources.constFind(r.path);
                if (known == job->state.resources.constEnd() || known->etag != r.etag || r.etag.isEmpty())
                    toFetch.append(r.path);
            }
            if (cs.complete) {
                for (auto it = job->state.resources.constBegin(); it != job->state.resources.constEnd(); ++it) {
                    if (!listed.contains(it.key()))
                        removed.append(it.key());
                }
                // A calendar that is suddenly empty: a wrong address, a broken server — nothing is
                // deleted here until the user confirms.
                if (listed.isEmpty() && job->state.resources.size() > 5 && !m_run->confirm) {
                    job->error = sync::WebDavError::make(sync::WebDavErrorCode::NotFound,
                                                         u"The calendar is empty on the server; nothing was deleted here"_s,
                                                         url.toString(QUrl::RemoveUserInfo));
                    finishJob();
                    return;
                }
            }
            job->client->fetch(url, toFetch, [this, job, removed, cs, reconcile, finishJob](const QList<RemoteResource> &got,
                                                                                         const sync::WebDavError &e) {
                if (!m_run)
                    return;
                for (const QString &path : removed) {
                    const auto it = job->state.resources.constFind(path);
                    if (it == job->state.resources.constEnd())
                        continue;
                    if (it->kind == u"VEVENT") {
                        job->eventsChanged = true;
                        ++job->events;
                    }
                    job->state.resources.remove(path);
                }
                for (const RemoteResource &r : got) {
                    const auto [kind, uid] = kindOf(r.data);
                    if (kind == u"VEVENT") {
                        job->eventsChanged = true;
                        ++job->events;
                    }
                    job->state.resources.insert(r.path, Resource{r.etag, uid, kind, r.data});
                }
                if (e.isError()) {
                    job->error = e; // what arrived is kept, the token is not: the rest comes next time
                    reconcile();
                    return;
                }
                job->state.syncToken = cs.syncToken;
                reconcile();
            });
        });
    };

    // 1. The events changed here.
    auto sendOutbox = std::make_shared<std::function<void()>>();
    *sendOutbox = [this, job, url, policy, fetchChanges, weak = std::weak_ptr(sendOutbox)]() {
        const auto self = weak.lock();
        if (!m_run || !self)
            return;
        if (job->outboxIndex >= job->state.outbox.size() || m_cancel) {
            job->state.outbox = job->keep + job->state.outbox.mid(job->outboxIndex);
            fetchChanges();
            return;
        }
        const EventChange change = job->state.outbox.at(job->outboxIndex++);
        if (job->entry.readOnly) {
            job->eventsChanged = true; // dropped: the server's version shows again
            (*self)();
            return;
        }
        if (change.kind == EventChange::Kind::Delete) {
            job->client->remove(change.path, change.ifMatch, [job, change, self](const sync::WebDavError &e) {
                if (!e.isError()) {
                    ++job->deletedRemote;
                } else if (e.is(sync::WebDavErrorCode::Precondition)) {
                    // Changed on the server meanwhile: the change wins over the deletion.
                    job->conflicts.append(summaryOf(change.data));
                } else {
                    job->keep.append(change);
                }
                (*self)();
            });
            return;
        }
        const QString ifMatch = change.kind == EventChange::Kind::Create ? QString() : change.ifMatch;
        job->client->put(change.path, change.data, ifMatch, [this, job, url, change, policy, self](const WriteResult &r) {
            if (!r.error.isError()) {
                job->state.resources.insert(r.path, Resource{r.etag, change.uid, u"VEVENT"_s, change.data});
                ++job->uploaded;
                (*self)();
                return;
            }
            if (!r.error.is(sync::WebDavErrorCode::Precondition)) {
                job->keep.append(change);
                (*self)();
                return;
            }
            // Changed on the server since: the account's policy decides.
            job->client->fetch(url, {change.path}, [this, job, change, policy, self](const QList<RemoteResource> &got,
                                                                                    const sync::WebDavError &) {
                const QString title = summaryOf(change.data);
                job->conflicts.append(title);
                LogEntry e;
                e.kind = LogEntry::Kind::Conflict;
                e.calendar = job->entry.id;
                e.name = job->entry.name;
                e.code = policy == ConflictPolicy::KeepBoth ? u"both"_s : u"newer"_s;
                e.text = title;
                log(e);
                if (got.isEmpty()) {
                    // Deleted on the server meanwhile: written again as new.
                    EventChange again = change;
                    again.kind = EventChange::Kind::Create;
                    again.ifMatch.clear();
                    job->keep.append(again);
                    (*self)();
                    return;
                }
                const RemoteResource server = got.first();
                const QDateTime serverTime = lastModifiedOf(server.data);
                if (policy == ConflictPolicy::KeepBoth) {
                    job->state.resources.insert(server.path, Resource{server.etag, change.uid, u"VEVENT"_s, server.data});
                    const QString uid = change.uid + u"-"_s + CalendarStore::newId(u'e').sliced(2);
                    EventChange copy;
                    copy.kind = EventChange::Kind::Create;
                    copy.uid = uid;
                    copy.path = calendarPath(job->entry.url) + fileNameFor(uid);
                    copy.data = withUid(change.data, uid);
                    copy.at = change.at;
                    job->state.resources.insert(copy.path, Resource{{}, uid, u"VEVENT"_s, copy.data});
                    job->keep.append(copy);
                } else if (serverTime.isValid() && serverTime > change.at) {
                    job->state.resources.insert(server.path, Resource{server.etag, change.uid, u"VEVENT"_s, server.data});
                } else {
                    EventChange again = change;
                    again.ifMatch = server.etag;
                    job->keep.append(again);
                }
                job->eventsChanged = true;
                (*self)();
            });
        });
    };
    (*sendOutbox)();
}

bool CalendarSync::queueEvent(const QString &calendarId, EventChange change)
{
    const CalendarSettings settings = m_store->settings();
    const CalendarEntry *entry = settings.calendar(calendarId);
    if (!entry || entry->readOnly)
        return false;
    CalendarState state = m_store->state(calendarId);
    if (change.path.isEmpty())
        change.path = calendarPath(entry->url) + fileNameFor(change.uid);
    if (!change.at.isValid())
        change.at = now().toUTC();
    const auto known = state.resources.constFind(change.path);
    if (change.kind != EventChange::Kind::Create && change.ifMatch.isEmpty() && known != state.resources.constEnd())
        change.ifMatch = known->etag;
    // One change per member waits: a later one takes the place of an earlier one.
    for (qsizetype i = 0; i < state.outbox.size(); ++i) {
        EventChange &earlier = state.outbox[i];
        if (earlier.path != change.path)
            continue;
        if (earlier.kind == EventChange::Kind::Create && change.kind == EventChange::Kind::Delete) {
            state.outbox.removeAt(i);
            state.resources.remove(change.path);
            m_store->saveState(calendarId, state);
            Q_EMIT calendarChanged(calendarId);
            return true;
        }
        if (earlier.kind == EventChange::Kind::Create)
            change.kind = EventChange::Kind::Create;
        change.ifMatch = earlier.ifMatch;
        state.outbox.removeAt(i);
        break;
    }
    if (change.kind == EventChange::Kind::Delete)
        state.resources.remove(change.path);
    else
        state.resources.insert(change.path, Resource{known != state.resources.constEnd() ? known->etag : QString(), change.uid,
                                                     u"VEVENT"_s, change.data});
    state.outbox.append(change);
    const bool ok = m_store->saveState(calendarId, state);
    Q_EMIT calendarChanged(calendarId);
    Q_EMIT localChange();
    return ok;
}

} // namespace brulion::caldav
