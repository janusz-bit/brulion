#include "agentshare.h"

#include "acp/hostoffer.h"
#include "acp/remote.h"
#include "log/categories.h"
#include "serverprocess.h"
#include "tailscale/localapi.h"
#include "tailscale/tailscale.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"
#include "workspace.h"
#include "workspaceagents.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

#if QT_CONFIG(process)
#include <QProcess>
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// How long a command of brulion-cli (`acp pair`, `acp clients`) may take.
constexpr int kCliTimeoutMs = 20000;

QList<AgentConfig> localAgents(const QList<AgentConfig> &agents)
{
    QList<AgentConfig> out;
    for (const AgentConfig &a : agents) {
        if (!a.isRemote() && !a.command.trimmed().isEmpty())
            out.append(a);
    }
    return out;
}

bool privateIpv4(const QHostAddress &a)
{
    return a.isInSubnet(QHostAddress(u"10.0.0.0"_s), 8) || a.isInSubnet(QHostAddress(u"172.16.0.0"_s), 12)
           || a.isInSubnet(QHostAddress(u"192.168.0.0"_s), 16);
}

} // namespace

AgentShare::AgentShare(Workspace *workspace, WorkspaceAgents *agents, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_agents(agents), m_command(defaultCommand())
{
    m_server = new ServerProcess(u"AI agents for the phone"_s, this);
    connect(m_server, &ServerProcess::stateChanged, this, &AgentShare::serverChanged);
    connect(m_server, &ServerProcess::announced, this, &AgentShare::announced);
    // (The server says when a device paired: the count follows.)
    connect(m_server, &ServerProcess::logLine, this, [this](const QString &line) {
        if (line.startsWith(u"paired "_s))
            countDevices();
    });
    m_applyTimer = new QTimer(this);
    m_applyTimer->setSingleShot(true);
    m_applyTimer->setInterval(300);
    connect(m_applyTimer, &QTimer::timeout, this, &AgentShare::apply);
    if (m_workspace) {
        connect(m_workspace, &Workspace::openChanged, this, [this] {
            watchVault();
            readOffers();
        });
        watchVault();
    }
}

AgentShare::~AgentShare() = default;

bool AgentShare::available() const
{
    return WorkspaceAgents::available() && !m_command.isEmpty();
}

QString AgentShare::state() const
{
    if (!m_on)
        return u"off"_s;
    if (!m_waiting.isEmpty())
        return m_waiting;
    if (!m_launchError.isEmpty())
        return u"failed"_s;
    switch (m_server->state()) {
    case ServerProcess::State::Stopped: return u"starting"_s; // (looking for its address)
    case ServerProcess::State::Starting: return u"starting"_s;
    case ServerProcess::State::Running: return u"running"_s;
    case ServerProcess::State::Waiting: return u"waiting"_s;
    case ServerProcess::State::Failed: return u"failed"_s;
    }
    return u"off"_s;
}

bool AgentShare::unencrypted() const
{
    const QUrl url(m_url);
    return url.scheme() == u"http" && !tailscale::isTailnetHost(url.host()) && !QHostAddress(url.host()).isLoopback();
}

QString AgentShare::status() const
{
    if (!available())
        return tr("brulion-cli was not found: it comes with Brulion's package.");
    const QString s = state();
    if (s == u"off")
        return tr("Your phone can then use the AI agents of this computer.");
    if (s == u"notebook")
        return tr("Open a notebook of yours: the open one is shared.");
    if (s == u"agents")
        return tr("Add an agent of this computer first.");
    if (s == u"starting")
        return tr("Starting…");
    if (s == u"running") {
        if (m_tailnet && !m_owner.isEmpty())
            return tr("On at %1 · your devices in the tailnet pair by themselves").arg(m_url);
        if (unencrypted())
            return tr("On at %1 · not encrypted on your network").arg(m_url);
        return tr("On at %1").arg(m_url);
    }
    const QString why = !m_launchError.isEmpty() ? m_launchError : m_server->error();
    if (s == u"waiting")
        return tr("Stopped: %1 — trying again soon").arg(why);
    return tr("Stopped: %1").arg(why);
}

void AgentShare::setSettings(bool on, const QString &address, bool trust)
{
    const bool wasOn = m_on;
    if (m_on == on && m_address == address.trimmed() && m_trust == trust)
        return;
    m_on = on;
    m_address = address.trimmed();
    m_trust = trust;
    if (wasOn && !on)
        removeOffer();
    Q_EMIT changed();
    applySoon();
}

void AgentShare::setNotebook(const QString &root, bool shareable)
{
    const QString clean = root.isEmpty() ? QString() : QDir::cleanPath(root);
    if (m_root == clean && m_shareable == shareable)
        return;
    m_root = clean;
    m_shareable = shareable;
    applySoon();
}

void AgentShare::agentsChanged()
{
    readOffers();
    applySoon();
}

void AgentShare::applySoon()
{
    m_applyTimer->start();
}

void AgentShare::apply()
{
    m_launchError.clear();
    const auto halt = [this](const QString &waiting) {
        m_waiting = waiting;
        m_launchKey.clear();
        ++m_detecting;
        m_server->stop();
        Q_EMIT changed();
    };
    if (!m_on || !available()) {
        halt(QString());
        return;
    }
    if (m_root.isEmpty() || !m_shareable) {
        halt(u"notebook"_s);
        return;
    }
    const QList<AgentConfig> agents = localAgents(m_agents ? m_agents->agents() : QList<AgentConfig>());
    if (agents.isEmpty()) {
        halt(u"agents"_s);
        return;
    }
    m_waiting.clear();
    QString key = m_root + u'\n' + m_address + u'\n' + (m_trust ? u'1' : u'0');
    for (const AgentConfig &a : agents)
        key += u'\n' + a.id + u'\t' + a.name + u'\t' + a.command + u'\t' + (a.tools ? u'1' : u'0');
    if (key == m_launchKey && m_server->state() != ServerProcess::State::Stopped
        && m_server->state() != ServerProcess::State::Failed) {
        Q_EMIT changed();
        return;
    }
    m_launchKey = key;
    QString error;
    const QString file = writeAgentsFile(&error);
    if (file.isEmpty()) {
        m_launchError = error;
        m_server->stop();
        Q_EMIT changed();
        return;
    }
    Launch l;
    l.root = m_root;
    l.agentsFile = file;
    l.address = m_address;
    l.trust = m_trust;
    l.port = m_port;
    if (!l.address.isEmpty()) {
        launch(l);
        return;
    }
    // By itself: Tailscale when tailscaled runs here, else this computer's address in its network.
    if (!m_tailscale) {
        if (!m_ownTailscale)
            m_ownTailscale = std::make_unique<tailscale::LocalApi>();
        m_tailscale = m_ownTailscale.get();
    }
    const int serial = ++m_detecting;
    m_server->stop();
    Q_EMIT changed();
    QPointer<AgentShare> self(this);
    m_tailscale->status([self, serial, l](const std::optional<tailscale::Status> &s, const QString &why) mutable {
        if (!self || serial != self->m_detecting)
            return;
        l.tailscale = s && s->running() && !s->dnsName.isEmpty() && !s->addresses.isEmpty();
        if (!l.tailscale) {
            qCInfo(logging::lcApp, "AI agents for the phone: no Tailscale here (%s)",
                   qUtf8Printable(s ? s->backendState : why));
            l.lan = lanAddress(QNetworkInterface::allAddresses());
        }
        const QString port = QString::number(l.port);
        self->m_automatic = l.tailscale      ? u"http://"_s + s->dnsName + u':' + port
                            : l.lan.isEmpty() ? QString()
                                              : u"http://"_s + l.lan + u':' + port;
        self->launch(l);
    });
}

void AgentShare::launch(const Launch &l)
{
    QString error;
    QStringList args = serveArguments(l, &error);
    if (args.isEmpty()) {
        m_launchError = error;
        qCWarning(logging::lcApp, "AI agents for the phone: %s", qUtf8Printable(error));
        m_server->stop();
        Q_EMIT changed();
        return;
    }
    const QList<AgentConfig> agents = localAgents(m_agents ? m_agents->agents() : QList<AgentConfig>());
    if (std::ranges::none_of(agents, [](const AgentConfig &a) { return a.tools; }))
        args << u"--no-tools"_s;
    m_server->setProgram(m_command.first(), m_command.mid(1) + args);
    m_server->start();
    Q_EMIT changed();
}

void AgentShare::serverChanged()
{
    if (m_server->state() != ServerProcess::State::Running) {
        m_url.clear();
        m_tailnet = false;
        m_owner.clear();
    }
    Q_EMIT changed();
}

void AgentShare::announced(const QJsonObject &result)
{
    m_url = result.value(u"url"_s).toString();
    const QJsonObject tailnet = result.value(u"tailnet"_s).toObject();
    m_tailnet = !tailnet.isEmpty();
    m_owner = tailnet.value(u"trust"_s).toBool() ? tailnet.value(u"login"_s).toString() : QString();
    Q_EMIT changed();
    countDevices();
    if (m_tailnet && tailscale::isTailnetHost(QUrl(m_url).host()))
        writeOffer();
    else
        removeOffer();
    readOffers();
}

QString AgentShare::writeAgentsFile(QString *error) const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/agent-share"_s;
    if (!QDir().mkpath(dir)) {
        *error = tr("Cannot write the agents' list (%1)").arg(dir);
        return {};
    }
    QJsonArray list;
    for (const AgentConfig &a : localAgents(m_agents ? m_agents->agents() : QList<AgentConfig>()))
        list.append(QJsonObject{{u"id"_s, a.id}, {u"name"_s, a.name}, {u"command"_s, a.command}});
    const QString path = dir + u"/agents.json"_s;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = tr("Cannot write the agents' list: %1").arg(file.errorString());
        return {};
    }
    file.write(QJsonDocument(list).toJson());
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        *error = tr("Cannot write the agents' list: %1").arg(file.errorString());
        return {};
    }
    return path;
}

// ---- brulion-cli's commands for the notebook: pairing codes, the devices --------------------------

void AgentShare::runCli(const QStringList &arguments, std::function<void(bool, const QJsonObject &)> done)
{
#if QT_CONFIG(process)
    if (m_command.isEmpty() || m_root.isEmpty()) {
        done(false, {});
        return;
    }
    auto *p = new QProcess(this);
    p->setProgram(m_command.first());
    p->setArguments(m_command.mid(1) + QStringList{u"--vault"_s, m_root, u"--compact"_s} + arguments);
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [p, done](int, QProcess::ExitStatus) {
        const QJsonObject o = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
        p->deleteLater();
        done(o.value(u"ok"_s).toBool(), o);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        done(false, QJsonObject{{u"error"_s, QJsonObject{{u"message"_s, p->errorString()}}}});
    });
    timer->start(kCliTimeoutMs);
    p->start(QIODevice::ReadOnly);
#else
    Q_UNUSED(arguments)
    done(false, {});
#endif
}

void AgentShare::newPairing()
{
    if (m_pairing)
        return;
    if (state() != u"running") {
        Q_EMIT message(tr("Turn on sharing first: the code is for this computer's agents."), u"info"_s);
        return;
    }
    m_pairing = true;
    Q_EMIT pairingChanged();
    QPointer<AgentShare> self(this);
    runCli({u"acp"_s, u"pair"_s}, [self](bool ok, const QJsonObject &json) {
        if (!self)
            return;
        self->m_pairing = false;
        if (!ok) {
            const QString why = json.value(u"error"_s).toObject().value(u"message"_s).toString();
            Q_EMIT self->message(tr("No pairing code: %1").arg(why.isEmpty() ? tr("brulion-cli did not answer") : why),
                                 u"error"_s);
            Q_EMIT self->pairingChanged();
            return;
        }
        const QJsonObject r = json.value(u"result"_s).toObject();
        self->m_pairCode = r.value(u"code"_s).toString();
        self->m_pairLink = r.value(u"link"_s).toString();
        const QDateTime expires = QDateTime::fromString(r.value(u"expires"_s).toString(), Qt::ISODate).toLocalTime();
        self->m_pairExpires = expires.isValid() ? QLocale().toString(expires.time(), QLocale::ShortFormat) : QString();
        qCInfo(logging::lcApp, "AI agents for the phone: a pairing code until %s", qUtf8Printable(self->m_pairExpires));
        Q_EMIT self->pairingChanged();
    });
}

void AgentShare::closePairing()
{
    if (m_pairCode.isEmpty() && m_pairLink.isEmpty())
        return;
    m_pairCode.clear();
    m_pairLink.clear();
    m_pairExpires.clear();
    Q_EMIT pairingChanged();
    countDevices(); // (a device may have paired with it)
}

void AgentShare::unpairAll()
{
    QPointer<AgentShare> self(this);
    runCli({u"acp"_s, u"revoke"_s, u"--all"_s}, [self](bool ok, const QJsonObject &json) {
        if (!self)
            return;
        if (!ok) {
            Q_EMIT self->message(tr("The devices were not unpaired: %1")
                                         .arg(json.value(u"error"_s).toObject().value(u"message"_s).toString()),
                                 u"error"_s);
            return;
        }
        qCInfo(logging::lcApp, "AI agents for the phone: every device unpaired");
        Q_EMIT self->message(tr("Every device was unpaired: each pairs again to use the agents."), u"success"_s);
        self->countDevices();
    });
}

void AgentShare::countDevices()
{
    QPointer<AgentShare> self(this);
    runCli({u"acp"_s, u"clients"_s}, [self](bool ok, const QJsonObject &json) {
        if (!self)
            return;
        const int n = ok ? int(json.value(u"result"_s).toObject().value(u"devices"_s).toArray().size()) : -1;
        if (n != self->m_devices) {
            self->m_devices = n;
            Q_EMIT self->devicesChanged();
        }
    });
}

// ---- the offer in the notebook ----------------------------------------------------------------------

void AgentShare::watchVault()
{
    Vault *v = m_workspace ? m_workspace->vault() : nullptr;
    if (m_watched.data() == static_cast<QObject *>(v))
        return;
    if (m_watched)
        m_watched->disconnect(this);
    m_watched = v;
    if (v)
        connect(v, &Vault::changed, this, &AgentShare::onVaultChanged);
}

void AgentShare::onVaultChanged(const QList<VaultChange> &changes)
{
    const VaultPath config = vaultConfigPath();
    if (std::ranges::any_of(changes, [&config](const VaultChange &c) { return c.path == config || c.oldPath == config; }))
        readOffers();
}

void AgentShare::readOffers()
{
    QJsonObject raw;
    Vault *v = m_workspace ? m_workspace->vault() : nullptr;
    if (v && v->isOpen() && v->exists(vaultConfigPath())) {
        bool ok = false;
        const QString text = v->readText(vaultConfigPath(), &ok);
        if (ok)
            raw = QJsonDocument::fromJson(text.toUtf8()).object();
    }
    QList<std::pair<QString, QString>> added;
    if (m_agents) {
        for (const AgentConfig &a : m_agents->agents()) {
            if (a.isRemote())
                added.append({a.url, a.remote});
        }
    }
    const QVariantList offers = offersFrom(raw, m_url, WorkspaceAgents::deviceName(), added);
    if (offers == m_offers)
        return;
    m_offers = offers;
    Q_EMIT offersChanged();
}

void AgentShare::writeOffer()
{
    Vault *v = m_workspace ? m_workspace->vault() : nullptr;
    if (!v || !v->isOpen() || QDir::cleanPath(v->rootPath()) != m_root)
        return;
    const VaultPath path = vaultConfigPath();
    QJsonObject raw;
    if (v->exists(path)) {
        bool ok = false;
        const QString text = v->readText(path, &ok);
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (!ok || !doc.isObject()) {
            // (A file the user broke is theirs to mend: never written over.)
            qCWarning(logging::lcApp, "AI agents for the phone: not offered in the notebook: %s is no JSON object",
                      qUtf8Printable(path));
            return;
        }
        raw = doc.object();
    }
    acp::HostOffer offer;
    offer.url = acp::normalizeRemoteBase(m_url).toString();
    offer.host = WorkspaceAgents::deviceName();
    for (const AgentConfig &a : localAgents(m_agents ? m_agents->agents() : QList<AgentConfig>()))
        offer.agents.append({a.id, a.name, QString()});
    m_offeredUrl = offer.url;
    m_offeredRoot = m_root;
    const QList<acp::HostOffer> there = acp::hostOffers(raw);
    if (std::ranges::any_of(there, [&offer](const acp::HostOffer &o) { return o == offer; }))
        return;
    const QJsonObject next = acp::withHostOffer(raw, offer);
    if (!v->writeText(path, QString::fromUtf8(serializeVaultConfig(v->config(), next))))
        qCWarning(logging::lcApp, "AI agents for the phone: not offered in the notebook: %s",
                  qUtf8Printable(v->errorString()));
    else
        qCInfo(logging::lcApp, "AI agents for the phone: offered in the notebook's settings at %s",
               qUtf8Printable(offer.url));
}

void AgentShare::removeOffer()
{
    Vault *v = m_workspace ? m_workspace->vault() : nullptr;
    if (!v || !v->isOpen() || !v->exists(vaultConfigPath()))
        return;
    if (!m_offeredRoot.isEmpty() && QDir::cleanPath(v->rootPath()) != m_offeredRoot)
        return;
    bool ok = false;
    const QString text = v->readText(vaultConfigPath(), &ok);
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!ok || !doc.isObject())
        return;
    QJsonObject raw = doc.object();
    const QString own = WorkspaceAgents::deviceName();
    QJsonObject next = raw;
    for (const acp::HostOffer &o : acp::hostOffers(raw)) {
        if ((!m_offeredUrl.isEmpty() && acp::sameHost(o.url, m_offeredUrl)) || o.host.compare(own, Qt::CaseInsensitive) == 0)
            next = acp::withoutHostOffer(next, o.url);
    }
    m_offeredUrl.clear();
    m_offeredRoot.clear();
    if (next == raw)
        return;
    if (v->writeText(vaultConfigPath(), QString::fromUtf8(serializeVaultConfig(v->config(), next))))
        qCInfo(logging::lcApp, "AI agents for the phone: no longer offered in the notebook");
}

// ---- pure -----------------------------------------------------------------------------------------

QStringList AgentShare::serveArguments(const Launch &l, QString *error)
{
    QStringList args{u"--vault"_s, l.root, u"acp"_s, u"serve"_s, u"--agents"_s, l.agentsFile, u"--stop-on-eof"_s};
    const QString port = QString::number(l.port);
    const auto trust = [&args, &l] {
        if (!l.trust)
            args << u"--no-tailnet-trust"_s;
    };
    const QString typed = l.address.trimmed();
    if (typed.isEmpty()) {
        if (l.tailscale) {
            args << u"--tailscale"_s << u"--port"_s << port;
            trust();
            return args;
        }
        if (l.lan.isEmpty()) {
            *error = tr("this computer has no address in a network: connect it, or type its address");
            return {};
        }
        args << u"--listen"_s << l.lan << u"--port"_s << port << u"--public-url"_s
             << u"http://%1:%2"_s.arg(l.lan, port) << u"--allow-http"_s;
        return args;
    }
    // (Typed without a scheme it is an address of this network: http.)
    const QUrl url = acp::normalizeRemoteBase(typed.contains(u"://"_s) ? typed : u"http://"_s + typed);
    if (!url.isValid() || url.isEmpty()) {
        *error = tr("“%1” is no address: type it as http://192.168.1.10:8790 or https://agents.example.com").arg(typed);
        return {};
    }
    const bool https = url.scheme() == u"https";
    const QString host = url.host();
    const QHostAddress ip(host);
    const QString given = QString::number(url.port(https ? 443 : l.port));
    if (tailscale::isTailnetHost(host)) {
        // https: the user's own Tailscale Serve in front (`tailscale serve --bg <port>`).
        if (https)
            args << u"--tailscale-serve"_s << u"--port"_s << port;
        else
            args << u"--tailscale"_s << u"--port"_s << given;
        args << u"--public-url"_s << url.toString();
        trust();
        return args;
    }
    if (https) {
        // A proxy of theirs adds HTTPS in front of this computer.
        args << u"--listen"_s << u"127.0.0.1"_s << u"--port"_s << port << u"--public-url"_s << url.toString();
        return args;
    }
    if (!ip.isNull()) {
        args << u"--listen"_s << ip.toString() << u"--port"_s << given << u"--public-url"_s << url.toString();
        if (!ip.isLoopback())
            args << u"--allow-http"_s;
        return args;
    }
    *error = tr("type this computer's address in your network (http://192.168.1.10:8790), its tailnet name, or an "
                "https:// address");
    return {};
}

QString AgentShare::lanAddress(const QList<QHostAddress> &addresses)
{
    QString other;
    for (const QHostAddress &a : addresses) {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback() || a.isLinkLocal()
            || tailscale::isTailnetAddress(a))
            continue;
        if (privateIpv4(a))
            return a.toString();
        if (other.isEmpty())
            other = a.toString();
    }
    return other;
}

QStringList AgentShare::defaultCommand()
{
    if (const QString cli = WorkspaceAgents::toolsProgram(); !cli.isEmpty())
        return {cli};
    // The AppImage has one entry point: `Brulion.AppImage cli …`.
    if (const QString image = qEnvironmentVariable("APPIMAGE"); !image.isEmpty() && QFileInfo(image).isExecutable())
        return {image, u"cli"_s};
    const QFileInfo self(QCoreApplication::applicationFilePath());
    if (self.completeBaseName() == u"brulion")
        return {self.absoluteFilePath(), u"cli"_s};
    return {};
}

QVariantList AgentShare::offersFrom(const QJsonObject &config, const QString &ownUrl, const QString &ownHost,
                                    const QList<std::pair<QString, QString>> &added)
{
    QVariantList out;
    for (const acp::HostOffer &o : acp::hostOffers(config)) {
        if ((!ownUrl.isEmpty() && acp::sameHost(o.url, ownUrl))
            || (!ownHost.isEmpty() && o.host.compare(ownHost, Qt::CaseInsensitive) == 0))
            continue;
        for (const acp::RemoteAgent &a : o.agents) {
            const bool here = std::ranges::any_of(added, [&](const std::pair<QString, QString> &x) {
                return acp::sameHost(x.first, o.url) && x.second == a.id;
            });
            if (here)
                continue;
            out.append(QVariantMap{{u"url"_s, o.url}, {u"host"_s, o.host}, {u"agent"_s, a.id}, {u"name"_s, a.name}});
        }
    }
    return out;
}

} // namespace brulion::shell
