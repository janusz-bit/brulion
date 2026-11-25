#include "chatschedules.h"

#include "hookserver.h"
#include "workspace.h"
#include "workspacechats.h"
#include "workspaceencryption.h"
#include "workspaceextensions.h"

#include "dates/format.h"
#include "log/categories.h"

#include <QRandomGenerator>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// How often the schedules are looked at (a run is at most this late).
constexpr int CheckIntervalMs = 30 * 1000;

QString newId()
{
    return u"s%1"_s.arg(QRandomGenerator::global()->generate64() & 0xffffffffffffULL, 12, 16, u'0');
}

} // namespace

ChatSchedules::ChatSchedules(Workspace *workspace, WorkspaceChats *chats, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_chats(chats)
{
    // (The notebook's clock: --now of a test run, the real one otherwise.)
    setClock({});
    m_timer.setInterval(CheckIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ChatSchedules::runDue);
    // A run missed while the app was closed: once the notebook is ready.
    connect(m_workspace, &Workspace::readyChanged, this, [this] {
        if (m_workspace->ready())
            runDue();
    });
    // The words of the rules follow the interface's language; partners come and go.
    connect(m_chats, &WorkspaceChats::partnersChanged, this, &ChatSchedules::changed);
}

void ChatSchedules::setStorage(Load loader, Save saver)
{
    m_load = std::move(loader);
    m_save = std::move(saver);
    load();
    if (m_load)
        m_timer.start();
    else
        m_timer.stop();
    Q_EMIT changed();
    if (m_workspace->ready())
        runDue();
}

void ChatSchedules::setClock(std::function<QDateTime()> clock)
{
    m_clock = clock ? std::move(clock) : std::function<QDateTime()>([this] { return m_workspace->now(); });
}

void ChatSchedules::load()
{
    m_schedules = m_load ? schedule::parseSchedules(m_load()) : QList<schedule::Schedule>();
    maskHooks();
}

void ChatSchedules::save()
{
    if (m_save)
        m_save(schedule::serializeSchedules(m_schedules));
    maskHooks();
    Q_EMIT changed();
}

void ChatSchedules::maskHooks()
{
    // (A token is a secret: never in a line of the event log, wherever it would come from.)
    std::vector<logging::ScopedSecret> secrets;
    for (const schedule::Schedule &s : std::as_const(m_schedules)) {
        if (!s.hook.isEmpty())
            secrets.emplace_back(s.hook);
    }
    m_hookSecrets = std::move(secrets);
}

dates::UiLocale ChatSchedules::locale() const
{
    return m_workspace->extensions()->language() == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
}

QString ChatSchedules::errorText(schedule::WhenError error) const
{
    using E = schedule::WhenError;
    switch (error) {
    case E::None: return {};
    case E::Empty: return tr("Say when it runs: “every day at 7:30”, “every 3 hours”, “tomorrow at 8”.");
    case E::NotUnderstood:
        return tr("Not understood. Write it as “every day at 7:30”, “on mondays at 9:00”, “every 3 hours” or “tomorrow "
                  "at 8” (in Polish: “codziennie o 7:30”, “w poniedziałki o 9:00”, “co 3 godziny”, “jutro o 8”).");
    case E::TooOften: return tr("At most every %1 minutes, at least once a month.").arg(schedule::MinIntervalMinutes);
    case E::Past: return tr("That time has passed.");
    case E::WhenDone: return tr("A conversation is never done: leave out “when done”.");
    }
    return {};
}

QString ChatSchedules::describe(const schedule::Schedule &s) const
{
    return schedule::describeWhen(s.when, locale(), m_clock().date());
}

QString ChatSchedules::nextText(const schedule::Schedule &s) const
{
    const std::optional<DateSpec> next = s.next();
    return next ? dates::formatHuman(*next, locale(), m_clock().date()) : QString();
}

const schedule::Schedule *ChatSchedules::find(const QString &id) const
{
    for (const schedule::Schedule &s : m_schedules) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

QVariantList ChatSchedules::rows() const
{
    QVariantList out;
    for (const schedule::Schedule &s : m_schedules) {
        const QString partnerName = m_chats->partnerLabel(s.partner);
        out.append(QVariantMap{
                {u"id"_s, s.id},
                {u"title"_s, s.title},
                {u"prompt"_s, s.prompt},
                {u"partner"_s, s.partner},
                {u"partnerName"_s, partnerName.isEmpty() ? s.partner : partnerName},
                {u"when"_s, s.whenText},
                {u"rule"_s, describe(s)},
                {u"next"_s, s.enabled ? nextText(s) : QString()},
                {u"continues"_s, s.continues},
                {u"enabled"_s, s.enabled},
                {u"by"_s, s.by},
                {u"lastRun"_s,
                 s.lastRun.isValid() ? dates::formatHuman(s.lastRun, locale(), m_clock().date()) : QString()},
                {u"lastConversation"_s, s.lastConversation},
                {u"lastError"_s, s.lastError},
                {u"webhook"_s, !s.hook.isEmpty()},
                {u"hookToken"_s, s.hook},
                {u"lastHook"_s,
                 s.lastHook.isValid() ? dates::formatHuman(s.lastHook, locale(), m_clock().date()) : QString()}});
    }
    return out;
}

QJsonObject ChatSchedules::toJson(const schedule::Schedule &s) const
{
    const std::optional<DateSpec> next = s.enabled ? s.next() : std::nullopt;
    QJsonObject out{{u"id"_s, s.id},
                    {u"title"_s, s.title},
                    {u"prompt"_s, s.prompt},
                    {u"partner"_s, s.partner},
                    {u"when"_s, s.whenText},
                    {u"rule"_s, describe(s)},
                    {u"next"_s, next ? QJsonValue(next->toString()) : QJsonValue()},
                    {u"destination"_s, s.continues ? u"continue"_s : u"new"_s},
                    {u"enabled"_s, s.enabled}};
    const auto put = [&out](const QString &key, const QString &value) {
        if (!value.isEmpty())
            out.insert(key, value);
    };
    put(u"conversation"_s, s.conversation);
    put(u"by"_s, s.by);
    put(u"parent"_s, s.parent);
    put(u"lastRun"_s, s.lastRun.isValid() ? s.lastRun.toString() : QString());
    put(u"lastConversation"_s, s.lastConversation);
    put(u"lastError"_s, s.lastError);
    // Whether it has a webhook — never its token (only the user sees and sets it, in Settings).
    out.insert(u"webhook"_s, !s.hook.isEmpty());
    put(u"lastHook"_s, s.lastHook.isValid() ? s.lastHook.toString() : QString());
    return out;
}

QVariantMap ChatSchedules::check(const QString &when) const
{
    schedule::WhenError error = schedule::WhenError::None;
    const std::optional<schedule::When> w = schedule::parseWhen(when, m_clock(), locale(), &error);
    if (!w)
        return {{u"ok"_s, false}, {u"error"_s, errorText(error)}};
    schedule::Schedule s;
    s.when = *w;
    s.created = schedule::wallClock(m_clock());
    return {{u"ok"_s, true}, {u"text"_s, describe(s)}, {u"next"_s, nextText(s)}, {u"error"_s, QString()}};
}

QString ChatSchedules::whenText(const QString &when) const
{
    return check(when).value(u"text"_s).toString();
}

QString ChatSchedules::whenNext(const QString &when) const
{
    return check(when).value(u"next"_s).toString();
}

QString ChatSchedules::whenError(const QString &when) const
{
    return check(when).value(u"error"_s).toString();
}

QString ChatSchedules::add(const Fields &fields, const QString &by, const QString &parent, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QString();
    };
    if (!m_load)
        return fail(tr("Open a notebook of yours first: scheduled conversations are kept for it."));
    if (m_schedules.size() >= schedule::MaxSchedules)
        return fail(tr("At most %1 scheduled conversations: delete one first.").arg(schedule::MaxSchedules));
    schedule::Schedule s;
    s.id = newId();
    s.enabled = true;
    s.by = by.simplified().left(64);
    s.parent = parent;
    s.created = schedule::wallClock(m_clock());
    m_schedules.append(s);
    if (!update(s.id, fields, error)) {
        m_schedules.removeLast();
        return {};
    }
    return s.id;
}

bool ChatSchedules::update(const QString &id, const Fields &fields, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    auto it = std::ranges::find(m_schedules, id, &schedule::Schedule::id);
    if (it == m_schedules.end())
        return fail(tr("There is no such scheduled conversation."));
    schedule::Schedule s = *it;
    if (fields.prompt)
        s.prompt = fields.prompt->trimmed().left(schedule::MaxPromptChars);
    if (s.prompt.isEmpty())
        return fail(tr("Write what to send each time."));
    if (fields.title)
        s.title = fields.title->simplified().left(schedule::MaxTitleChars);
    if (s.title.isEmpty())
        s.title = s.prompt.simplified().left(60);
    if (fields.partner)
        s.partner = fields.partner->trimmed();
    if (fields.hook) {
        const QString hook = fields.hook->trimmed();
        if (!hook.isEmpty() && !webhook::isToken(hook))
            return fail(tr("That is no webhook's address: make a new one."));
        if (hook != s.hook)
            m_hookLimit.forget(s.id);
        s.hook = hook;
    }
    if (fields.when && fields.when->trimmed().isEmpty() && !s.hook.isEmpty()) {
        // No time of its own: only its webhook (and Run now) start it.
        if (s.when.kind != schedule::When::Kind::None) {
            s.when = schedule::When{};
            s.when.kind = schedule::When::Kind::None;
            s.whenText.clear();
            s.created = schedule::wallClock(m_clock());
            s.lastRun = {};
        }
    } else if (fields.when) {
        schedule::WhenError why = schedule::WhenError::None;
        const std::optional<schedule::When> w = schedule::parseWhen(*fields.when, m_clock(), locale(), &why);
        if (!w)
            return fail(errorText(why));
        // A new rule counts from now (a time already passed today waits for tomorrow).
        if (!(*w == s.when) || s.whenText != fields.when->simplified()) {
            s.when = *w;
            s.whenText = fields.when->simplified();
            s.created = schedule::wallClock(m_clock());
            s.lastRun = {};
        }
    }
    if (!s.when.isValid())
        return fail(errorText(schedule::WhenError::Empty));
    if (s.when.kind == schedule::When::Kind::None && s.hook.isEmpty())
        return fail(tr("Say when it runs, or keep its webhook: without either it never runs."));
    if (fields.continues) {
        if (s.continues != *fields.continues)
            s.conversation.clear();
        s.continues = *fields.continues;
    }
    if (fields.enabled)
        s.enabled = *fields.enabled;
    *it = s;
    if (error)
        error->clear();
    save();
    return true;
}

QString ChatSchedules::add(const QString &title, const QString &prompt, const QString &partner, const QString &when,
                           bool continues, bool enabled)
{
    m_lastError.clear();
    return add(Fields{title, prompt, partner, when, continues, enabled}, QString(), QString(), &m_lastError);
}

bool ChatSchedules::update(const QString &id, const QString &title, const QString &prompt, const QString &partner,
                           const QString &when, bool continues, bool enabled)
{
    m_lastError.clear();
    return update(id, Fields{title, prompt, partner, when, continues, enabled}, &m_lastError);
}

QString ChatSchedules::save(const QString &id, const QString &title, const QString &prompt, const QString &partner,
                            const QString &when, bool continues, bool enabled, const QString &hook)
{
    m_lastError.clear();
    const Fields f{title, prompt, partner, when, continues, enabled, hook};
    if (id.isEmpty())
        return add(f, QString(), QString(), &m_lastError);
    return update(id, f, &m_lastError) ? id : QString();
}

QString ChatSchedules::hookUrl(const QString &token) const
{
    if (!webhook::isToken(token))
        return {};
    return m_chats->hooks()->baseUrl() + u'/' + webhook::Segment + u'/' + token;
}

QString ChatSchedules::maskedHookUrl(const QString &token) const
{
    if (!webhook::isToken(token))
        return {};
    return m_chats->hooks()->baseUrl() + u'/' + webhook::Segment + u'/' + webhook::masked(token);
}

bool ChatSchedules::copyHookUrl(const QString &token) const
{
    const QString url = hookUrl(token);
    return !url.isEmpty() && m_workspace->encryption()->copySecret(url);
}

QString ChatSchedules::hookOnlyText() const
{
    schedule::When none;
    none.kind = schedule::When::Kind::None;
    return schedule::describeWhen(none, locale());
}

bool ChatSchedules::setEnabled(const QString &id, bool enabled)
{
    Fields f;
    f.enabled = enabled;
    return update(id, f, nullptr);
}

bool ChatSchedules::remove(const QString &id)
{
    const qsizetype removed = m_schedules.removeIf([&id](const schedule::Schedule &s) { return s.id == id; });
    if (removed == 0)
        return false;
    save();
    return true;
}

QString ChatSchedules::runNow(const QString &id)
{
    auto it = std::ranges::find(m_schedules, id, &schedule::Schedule::id);
    if (it == m_schedules.end())
        return {};
    const QString path = run(*it);
    save();
    return path;
}

void ChatSchedules::runDue()
{
    if (m_running || !m_load || !m_workspace->ready())
        return;
    m_running = true;
    const QDateTime now = m_clock();
    bool ran = false;
    for (schedule::Schedule &s : m_schedules) {
        if (!s.isDue(now))
            continue;
        run(s);
        ran = true;
    }
    m_running = false;
    if (ran)
        save();
}

QString ChatSchedules::run(schedule::Schedule &s)
{
    // Noted before the turn starts: whatever happens, the next run counts from this one.
    s.lastRun = schedule::wallClock(m_clock());
    return start(s, s.prompt);
}

QString ChatSchedules::start(schedule::Schedule &s, const QString &message)
{
    s.lastError.clear();
    const auto skip = [&s](const QString &why) {
        s.lastError = why;
        qCInfo(logging::lcApp).noquote() << u"Scheduled conversation %1 skipped: %2"_s.arg(s.id, why);
        return QString();
    };
    if (!m_workspace->ready())
        return skip(tr("No notebook was open."));
    // Only a partner that answers on this device (an agent of a computer's own: not on the phone).
    bool known = false;
    for (const QVariant &row : m_chats->partners()) {
        if (row.toMap().value(u"id"_s).toString() == s.partner)
            known = true;
    }
    if (!known)
        return skip(tr("Its partner does not answer on this device."));
    QString path;
    const bool goesOn = s.continues && !s.conversation.isEmpty() && m_workspace->exists(s.conversation);
    if (goesOn) {
        if (m_chats->isRunning(s.conversation))
            return skip(tr("Its conversation was still answering the last run."));
        path = m_chats->send(s.conversation, message);
    } else {
        path = m_chats->send(QString(), message, QString(), {}, s.partner, s.parent);
        if (s.continues && !path.isEmpty())
            s.conversation = path;
    }
    if (path.isEmpty()) {
        const QString failure = m_chats->turn(goesOn ? s.conversation : QString())->failure();
        return skip(failure.isEmpty() ? tr("The conversation did not start.") : failure);
    }
    s.lastConversation = path;
    qCInfo(logging::lcApp).noquote() << u"Scheduled conversation %1 ran: %2"_s.arg(s.id, path);
    Q_EMIT ran(s.id, s.title, path);
    return path;
}

ChatSchedules::HookOutcome ChatSchedules::runHook(const HookRequest &request)
{
    // Every schedule's token compared, in constant time; one that is off answers as none does.
    QStringList tokens;
    for (const schedule::Schedule &s : std::as_const(m_schedules))
        tokens.append(s.hook);
    const int found = m_load ? webhook::findToken(tokens, request.token) : -1;
    if (found < 0 || !m_schedules.at(found).enabled) {
        qCInfo(logging::lcApp) << "Webhook: no scheduled conversation of this notebook has that address (or it is off)";
        return {u"unknown"_s};
    }
    schedule::Schedule &s = m_schedules[found];
    const qint64 now = m_clock().toMSecsSinceEpoch();
    if (const qint64 wait = m_hookLimit.wait(s.id, now); wait > 0) {
        qCInfo(logging::lcApp).noquote() << u"Webhook of scheduled conversation %1: too soon after the last one"_s.arg(
                s.id);
        return {u"busy"_s, QString(), wait};
    }
    if (!m_workspace->ready())
        return {u"unavailable"_s};
    if (s.continues && !s.conversation.isEmpty() && m_chats->isRunning(s.conversation)) {
        qCInfo(logging::lcApp).noquote()
                << u"Webhook of scheduled conversation %1: its conversation still answers"_s.arg(s.id);
        return {u"busy"_s, QString(), webhook::MinIntervalMs};
    }
    const QString message = webhook::message(s.prompt, request.body, locale() == dates::UiLocale::Polish);
    const QString path = start(s, message);
    if (path.isEmpty()) {
        save();
        return {u"unavailable"_s};
    }
    m_hookLimit.ran(s.id, now);
    s.lastHook = schedule::wallClock(m_clock());
    qCInfo(logging::lcApp).noquote() << u"Webhook of scheduled conversation %1 started it (%2, %3 bytes%4)"_s.arg(
            s.id, webhook::kindName(request.body.kind), QString::number(request.body.bytes),
            request.body.cut ? u", cut"_s : QString());
    const QString name = m_chats->nameOf(path);
    save();
    return {u"started"_s, name};
}

} // namespace brulion::shell
