#include "chatsecrets.h"

#include "workspace.h"
#include "workspacechats.h"

#include "log/categories.h"
#include "secretdrop/secretdrop.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

// The remote control's error codes (ipc/controlprotocol.h ControlError; this side is GUI-free).
const QString BadParams = u"bad_params"_s;
const QString NotFound = u"not_found"_s;
const QString Busy = u"busy"_s;
const QString Refused = u"refused"_s;
const QString Timeout = u"timeout"_s;
const QString Failed = u"failed"_s;

/// `text` on one line, cut to `max` characters (never inside a surrogate pair).
QString oneLine(const QString &text, int max)
{
    QString out = text.simplified().left(max);
    if (!out.isEmpty() && out.back().isHighSurrogate())
        out.chop(1);
    return out;
}

/// `text` with its lines, cut to `max` characters.
QString lines(const QString &text, int max)
{
    QString out = text.trimmed().left(max);
    if (!out.isEmpty() && out.back().isHighSurrogate())
        out.chop(1);
    return out;
}

} // namespace

struct ChatSecrets::Pending {
    Request request;
    Reply reply;
    QTimer timer;
};

ChatSecrets::ChatSecrets(Workspace *workspace, WorkspaceChats *chats, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_chats(chats), m_drop(std::make_unique<secretdrop::Drop>(defaultBase()))
{
    // What a run that ended without a word (a crash) left: gone at the next start.
    const int swept = secretdrop::Drop::sweep(m_drop->base());
    if (swept > 0)
        qCInfo(logging::lcApp, "Secrets for AI agents: %d folders left by an earlier run removed", swept);
    watchDrop();
    connect(m_workspace, &Workspace::pathRenamed, this, &ChatSecrets::renamed);
    connect(m_workspace, &Workspace::pathRemoved, this, [this](const QString &path) { turnEnded(path); });
    m_clientCheck.setInterval(1000);
    connect(&m_clientCheck, &QTimer::timeout, this, &ChatSecrets::dropGoneClients);
}

ChatSecrets::~ChatSecrets() = default;

QString ChatSecrets::defaultBase()
{
    // One folder per profile of the app (its data folder names it), as the app's sockets are.
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString id =
            QString::fromLatin1(QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QFileInfo info(runtime);
    constexpr QFileDevice::Permissions others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                                                | QFileDevice::ReadOther | QFileDevice::WriteOther
                                                | QFileDevice::ExeOther;
    // The user's own runtime folder (tmpfs: never on a disk), when it is private.
    if (!runtime.isEmpty() && info.isDir() && !info.isSymLink() && !(info.permissions() & others))
        return runtime + u"/brulion-"_s + id + u"-secrets"_s;
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty())
        return {};
    return cache + u"/agent-secrets"_s;
}

void ChatSecrets::setBase(const QString &base)
{
    m_drop->discardAll();
    m_handed.clear();
    m_drop = std::make_unique<secretdrop::Drop>(base);
    watchDrop();
}

void ChatSecrets::watchDrop()
{
    // A value's time up (15 minutes): its conversation holds nothing more.
    connect(m_drop.get(), &secretdrop::Drop::expired, this, [this](const QString &owner) {
        if (!m_drop->holds(owner)) {
            for (auto it = m_handed.begin(); it != m_handed.end();)
                it = it.value() == owner ? m_handed.erase(it) : std::next(it);
        }
        qCInfo(logging::lcApp) << "Secrets for AI agents: a secret's time was up: deleted";
    });
}

void ChatSecrets::ask(const Request &request, const Reply &reply)
{
    Request r = request;
    r.label = oneLine(r.label, MaxLabelChars);
    r.reason = lines(r.reason, MaxReasonChars);
    r.placeholder = oneLine(r.placeholder, MaxPlaceholderChars);
    r.agent = oneLine(r.agent, 64);
    r.url = r.url.trimmed();
    if (r.label.isEmpty())
        return reply.fail(BadParams, u"Say what you ask for (label): \"GitHub token\", \"the router's PIN\""_s,
                          {{u"param"_s, u"label"_s}});
    if (!r.url.isEmpty()) {
        const QUrl url(r.url, QUrl::StrictMode);
        if (r.url.size() > MaxUrlChars || !url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http")
            || url.host().isEmpty() || !url.userInfo().isEmpty())
            return reply.fail(BadParams, u"url is the address of a sign-in or setup page: http or https"_s,
                              {{u"param"_s, u"url"_s}});
        r.url = url.toString(QUrl::FullyEncoded);
    }
    if (r.path.isEmpty() || !m_workspace->exists(r.path) || !m_chats->isConversation(r.path))
        return reply.fail(NotFound, u"No such conversation"_s, {{u"param"_s, u"path"_s}});
    const QString key = r.path.toLower();
    if (m_pending.contains(key))
        return reply.fail(Busy, u"The user is already asked for a secret in this conversation: wait for that answer"_s,
                          {{u"label"_s, m_pending.value(key)->request.label}});
    r.waitMs = std::clamp(r.waitMs, 1000, MaxWaitMs);
    auto p = std::make_shared<Pending>();
    p->request = r;
    p->reply = reply;
    p->timer.setSingleShot(true);
    p->timer.setInterval(r.waitMs);
    const std::weak_ptr<Pending> weak = p;
    connect(&p->timer, &QTimer::timeout, this, [this, weak] {
        if (const std::shared_ptr<Pending> live = weak.lock())
            finish(live->request.path.toLower(), Timeout,
                   u"The user did not give it in time: ask again only if it is still needed"_s);
    });
    p->timer.start();
    m_pending.insert(key, p);
    if (!m_clientCheck.isActive())
        m_clientCheck.start();
    ++m_serial;
    qCInfo(logging::lcApp).noquote() << u"Secrets for AI agents: %1 asks in %2 for “%3”"_s.arg(
            r.agent.isEmpty() ? u"an agent"_s : r.agent, r.path, r.label);
    Q_EMIT changed();
    Q_EMIT asked(r.path, questionText(r.path));
}

bool ChatSecrets::pending(const QString &path, int) const
{
    return !path.isEmpty() && m_pending.contains(path.toLower());
}

QVariantMap ChatSecrets::card(const QString &path, int) const
{
    const std::shared_ptr<Pending> p = path.isEmpty() ? nullptr : m_pending.value(path.toLower());
    if (!p)
        return {};
    const Request &r = p->request;
    return {{u"agent"_s, r.agent.isEmpty() ? tr("An AI agent") : r.agent},
            {u"label"_s, r.label},
            {u"reason"_s, r.reason},
            {u"placeholder"_s, r.placeholder},
            {u"url"_s, r.url},
            {u"host"_s, r.url.isEmpty() ? QString() : QUrl(r.url).host()},
            {u"inline"_s, r.inlineValue}};
}

QString ChatSecrets::questionText(const QString &path) const
{
    const std::shared_ptr<Pending> p = path.isEmpty() ? nullptr : m_pending.value(path.toLower());
    if (!p)
        return {};
    return tr("%1 asks for a secret: %2")
            .arg(p->request.agent.isEmpty() ? tr("An AI agent") : p->request.agent, p->request.label);
}

bool ChatSecrets::send(const QString &path, const QString &value)
{
    const QString key = path.toLower();
    const std::shared_ptr<Pending> p = m_pending.value(key);
    if (!p || value.isEmpty())
        return false;
    // (The drop's owner: the conversation as it is named now; a rename moves m_handed.)
    const QString owner = m_handed.value(key, key);
    QJsonObject result{{u"expiresInMs"_s, double(secretdrop::MaxLifetimeMs)}};
    if (p->request.inlineValue) {
        m_drop->hold(owner, value);
        result.insert(u"delivery"_s, u"value"_s);
        result.insert(u"value"_s, value);
        result.insert(u"note"_s,
                      u"The user's secret itself: use it only where it is needed, never repeat it in the chat, "
                      u"a note or a file."_s);
    } else {
        QString error;
        const QString file = m_drop->write(owner, value, secretdrop::MaxLifetimeMs, &error);
        if (file.isEmpty()) {
            qCWarning(logging::lcApp).noquote() << u"Secrets for AI agents: not handed over: %1"_s.arg(error);
            Q_EMIT message(tr("The secret could not be handed over: %1").arg(error), u"error"_s);
            finish(key, Failed, u"The app could not hand the secret over: %1"_s.arg(error));
            return false;
        }
        result.insert(u"delivery"_s, u"file"_s);
        result.insert(u"file"_s, file);
        result.insert(u"note"_s,
                      u"The secret is in that file, which only this user can read. Read it only inside the command "
                      u"that uses it — --token \"$(cat '%1')\", -H \"Authorization: Bearer $(cat '%1')\" — and never "
                      u"print it, echo it or write it into a note, a file or the chat. The file is deleted when this "
                      u"turn ends, or in 15 minutes."_s.arg(file));
    }
    m_handed.insert(key, owner);
    p->timer.stop();
    m_pending.remove(key);
    ++m_serial;
    qCInfo(logging::lcApp).noquote() << u"Secrets for AI agents: “%1” handed to %2 in %3 (%4)"_s.arg(
            p->request.label, p->request.agent.isEmpty() ? u"an agent"_s : p->request.agent, p->request.path,
            p->request.inlineValue ? u"the value itself"_s : u"an owner-only file"_s);
    Q_EMIT changed();
    if (!p->reply.connected || p->reply.connected())
        p->reply.ok(result);
    if (m_pending.isEmpty())
        m_clientCheck.stop();
    return true;
}

void ChatSecrets::cancel(const QString &path)
{
    finish(path.toLower(), Refused,
           u"The user did not give it (they cancelled): go on without it, or ask them in words what to do"_s,
           {{u"reason"_s, u"cancelled"_s}});
}

void ChatSecrets::finish(const QString &key, const QString &code, const QString &message, const QJsonObject &details)
{
    const std::shared_ptr<Pending> p = m_pending.take(key);
    if (!p)
        return;
    p->timer.stop();
    ++m_serial;
    qCInfo(logging::lcApp).noquote() << u"Secrets for AI agents: the request for “%1” in %2 ended: %3"_s.arg(
            p->request.label, p->request.path, code);
    Q_EMIT changed();
    p->reply.fail(code, message, details);
    if (m_pending.isEmpty())
        m_clientCheck.stop();
}

void ChatSecrets::turnEnded(const QString &path, bool stopped)
{
    const QString key = path.toLower();
    if (m_pending.contains(key))
        finish(key, Refused,
               stopped ? u"The user stopped the turn: the secret was not given"_s
                       : u"The conversation's turn ended: the secret was not given"_s,
               {{u"reason"_s, stopped ? u"stopped"_s : u"ended"_s}});
    if (const QString owner = m_handed.take(key); !owner.isEmpty() && m_drop->discard(owner) > 0)
        qCInfo(logging::lcApp).noquote()
                << u"Secrets for AI agents: the secrets handed over in %1 deleted (its turn ended)"_s.arg(path);
}

void ChatSecrets::reset()
{
    const QStringList keys = m_pending.keys();
    for (const QString &key : keys)
        finish(key, Refused, u"The notebook was closed"_s, {{u"reason"_s, u"closed"_s}});
    m_drop->discardAll();
    m_handed.clear();
}

void ChatSecrets::renamed(const QString &from, const QString &to)
{
    const QString key = from.toLower();
    if (const std::shared_ptr<Pending> p = m_pending.take(key)) {
        p->request.path = to;
        m_pending.insert(to.toLower(), p);
        ++m_serial;
        Q_EMIT changed();
    }
    if (m_handed.contains(key))
        m_handed.insert(to.toLower(), m_handed.take(key));
}

void ChatSecrets::dropGoneClients()
{
    QStringList gone;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it.value()->reply.connected && !it.value()->reply.connected())
            gone.append(it.key());
    }
    for (const QString &key : std::as_const(gone)) {
        const std::shared_ptr<Pending> p = m_pending.take(key);
        p->timer.stop();
        qCInfo(logging::lcApp).noquote() << u"Secrets for AI agents: the agent stopped waiting for “%1”"_s.arg(
                p->request.label);
    }
    if (!gone.isEmpty()) {
        ++m_serial;
        Q_EMIT changed();
    }
    if (m_pending.isEmpty())
        m_clientCheck.stop();
}

} // namespace brulion::shell
