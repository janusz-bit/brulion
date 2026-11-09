#include "backgroundcheck.h"

#include "androidpayload.h"

#include "index/vaultindex.h"
#include "log/redact.h"
#include "notebook/session.h"
#include "reminders/reminders.h"
#include "sync/syncengine.h"
#include "sync/webdavclient.h"
#include "vault/vault.h"

#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>
#include <cstdlib>
#include <iterator>

using namespace Qt::StringLiterals;

namespace brulion::background {

Q_STATIC_LOGGING_CATEGORY(lcBackground, "brulion.background")

int normalizedInterval(int minutes)
{
    if (minutes <= 0)
        return 0;
    // The nearest choice above "off" (ties go to the longer period: easier on the battery).
    int best = IntervalChoices[1];
    for (const int choice : IntervalChoices) {
        if (choice > 0 && std::abs(choice - minutes) <= std::abs(best - minutes))
            best = choice;
    }
    return best;
}

// ---- Target -------------------------------------------------------------------------------------

bool Target::isValid() const
{
    return !root.isEmpty() && !stateDir.isEmpty() && !vault.isEmpty() && intervalMinutes > 0;
}

QJsonObject Target::toJson() const
{
    QJsonObject json{
            {u"root"_s, root},         {u"stateDir"_s, stateDir},
            {u"vault"_s, vault},       {u"ringMinutes"_s, ringMinutes},
            {u"language"_s, language}, {u"intervalMinutes"_s, intervalMinutes},
    };
    if (nextcloud) {
        json.insert(u"nextcloud"_s, QJsonObject{{u"davUrl"_s, nextcloud->davUrl},
                                                {u"user"_s, nextcloud->user},
                                                {u"device"_s, nextcloud->device}});
    }
    return json;
}

std::optional<Target> Target::fromJson(const QJsonObject &json)
{
    Target t;
    t.root = json.value(u"root"_s).toString();
    t.stateDir = json.value(u"stateDir"_s).toString();
    t.vault = json.value(u"vault"_s).toString();
    if (t.root.isEmpty() || t.stateDir.isEmpty() || t.vault.isEmpty())
        return std::nullopt;
    t.ringMinutes = std::clamp(json.value(u"ringMinutes"_s).toInt(ScheduledNotification::DefaultRingMinutes), 1, 60);
    t.language = json.value(u"language"_s).toString() == u"en" ? u"en"_s : u"pl"_s;
    t.intervalMinutes = normalizedInterval(json.value(u"intervalMinutes"_s).toInt(DefaultIntervalMinutes));
    const QJsonObject nc = json.value(u"nextcloud"_s).toObject();
    const QString davUrl = nc.value(u"davUrl"_s).toString();
    const QString user = nc.value(u"user"_s).toString();
    if (!davUrl.isEmpty() && !user.isEmpty())
        t.nextcloud = Nextcloud{davUrl, user, nc.value(u"device"_s).toString()};
    return t;
}

QList<ScheduledNotification> upcomingFor(const VaultIndex &index, const QDateTime &now, const Target &target)
{
    // ReminderScheduler::refreshAt(): its defaults (WorkspaceReminders changes neither), the ring
    // time for every item, the notebook's id (WorkspaceReminders' proxy) — the same set the app
    // hands Android, so the armed set does not flip between the two.
    QList<ScheduledNotification> list = upcomingNotifications(
            index, now, ReminderScheduler::DefaultHorizonDays, ReminderScheduler::DefaultMaxScheduled, nullptr,
            target.language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish);
    for (ScheduledNotification &n : list) {
        n.ringMinutes = target.ringMinutes;
        n.vault = target.vault;
    }
    return list;
}

// ---- Check --------------------------------------------------------------------------------------

Check::Check(Target target, QString password, sync::HttpTransport *transport, std::function<QDateTime()> clock,
             QObject *parent)
    : QObject(parent)
    , m_target(std::move(target))
    , m_password(std::move(password))
    , m_transport(transport)
    , m_clock(std::move(clock))
{
    m_syncTimer.setSingleShot(true);
    connect(&m_syncTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcBackground) << "Sync took too long: cancelled";
        if (m_engine)
            m_engine->cancel();
    });
}

// Members go in reverse order of declaration: the engine first (it abandons a running run), then
// the client, then the notebook (its vault).
Check::~Check() = default;

QDateTime Check::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void Check::start()
{
    if (m_started)
        return;
    m_started = true;
    m_elapsed.start();
    m_result = QJsonObject{{u"vault"_s, m_target.vault}, {u"sync"_s, u"off"_s}};
    if (!m_target.isValid()) {
        fail(u"invalid"_s, u"no notebook to check"_s);
        return;
    }
    notebook::Notebook::Options options;
    options.stateDir = m_target.stateDir;
    options.historyReason = u"sync"_s;
    m_notebook = std::make_unique<notebook::Notebook>();
    QString error;
    notebook::Notebook::OpenError kind = notebook::Notebook::OpenError::None;
    if (!m_notebook->open(m_target.root, options, &error, &kind)) {
        fail(kind == notebook::Notebook::OpenError::Missing      ? u"missing"_s
             : kind == notebook::Notebook::OpenError::NotAFolder ? u"not-a-folder"_s
                                                                 : u"failed"_s,
             error);
        return;
    }
    // An unmounted card, a revoked storage permission: the folder reads as empty. Arming what an
    // empty folder holds would cancel every reminder — keep the armed set instead.
    if (m_notebook->vault().files().isEmpty()) {
        fail(u"empty"_s, u"the notebook folder holds no files"_s);
        return;
    }
    // From the event loop: finished() never comes from inside start().
    QMetaObject::invokeMethod(this, &Check::syncThenRead, Qt::QueuedConnection);
}

void Check::syncThenRead()
{
    if (!m_target.nextcloud || !m_transport || m_password.isEmpty()) {
        if (m_target.nextcloud)
            m_result.insert(u"sync"_s, u"skipped"_s); // no password on this device (any more)
        readReminders();
        return;
    }
    const Target::Nextcloud &nc = *m_target.nextcloud;
    m_client = std::make_unique<sync::WebDavClient>(nc.davUrl, nc.user, m_password, m_transport);
    Vault &vault = m_notebook->vault();
    m_engine = std::make_unique<sync::SyncEngine>(&vault, m_client.get(), vault.store(),
                                                  nc.device.isEmpty() ? u"Android"_s : nc.device);
    if (m_clock)
        m_engine->setClock(m_clock);
    sync::SyncRunOptions options;
    options.trigger = sync::SyncTrigger::Timer;
    m_syncTimer.start(m_syncTimeoutMs);
    m_engine->sync(options, [this](const sync::SyncReport &report) {
        // After the engine's finished(): leave its call stack before tearing it down.
        QMetaObject::invokeMethod(this, [this, report] { syncDone(report); }, Qt::QueuedConnection);
    });
}

void Check::syncDone(const sync::SyncReport &report)
{
    m_syncTimer.stop();
    QString outcome = u"ok"_s;
    if (report.syncError.is(sync::SyncErrorCode::OtherProcess))
        outcome = u"busy"_s; // the app syncs it right now: it reads the notes itself afterwards
    else if (report.failed() || report.cancelled)
        outcome = u"failed"_s;
    m_result.insert(u"sync"_s, outcome);
    if (report.failed())
        m_result.insert(u"syncError"_s, report.failureCode());
    m_result.insert(u"uploaded"_s, report.uploaded);
    m_result.insert(u"downloaded"_s, report.downloaded + report.merged);
    m_engine.reset();
    m_client.reset();
    // A failed sync changes nothing about the notes already here: their reminders still count.
    readReminders();
}

void Check::readReminders()
{
    VaultIndex &index = m_notebook->index();
    const QList<ScheduledNotification> upcoming = upcomingFor(index, now(), m_target);
    m_result.insert(u"ok"_s, true);
    m_result.insert(u"notes"_s, int(index.paths().size()));
    m_result.insert(u"reminders"_s, notify::androidReminderPayload(upcoming));
    finish();
}

void Check::fail(const QString &error, const QString &message)
{
    m_result.insert(u"ok"_s, false);
    m_result.insert(u"error"_s, error);
    if (!message.isEmpty())
        m_result.insert(u"message"_s, logging::redact(message));
    // Never from inside start(): the caller connects first.
    QMetaObject::invokeMethod(this, &Check::finish, Qt::QueuedConnection);
}

void Check::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_result.insert(u"elapsedMs"_s, qint64(m_elapsed.elapsed()));
    if (m_result.value(u"ok"_s).toBool()) {
        qCInfo(lcBackground).noquote() << u"Checked %1 note(s): %2 reminder(s), sync %3 (%4 ms)"_s.arg(
                QString::number(m_result.value(u"notes"_s).toInt()),
                QString::number(m_result.value(u"reminders"_s).toArray().size()), m_result.value(u"sync"_s).toString(),
                QString::number(m_result.value(u"elapsedMs"_s).toInteger()));
    } else {
        qCWarning(lcBackground).noquote() << u"Check failed (%1): %2"_s.arg(m_result.value(u"error"_s).toString(),
                                                                            m_result.value(u"message"_s).toString());
    }
    // The notebook closes now: its index cache is written when this load parsed many notes.
    m_notebook.reset();
    Q_EMIT finished(m_result);
}

} // namespace brulion::background
