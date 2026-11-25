#include "hookserver.h"

#include "agentshare.h"
#include "serverprocess.h"
#include "workspaceagents.h"

#include "log/categories.h"
#include "tailscale/localapi.h"
#include "tailscale/tailscale.h"

#include <QJsonObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The variable that carries the grant (src/cli/hooks.cpp).
constexpr auto HookTokenVariable = "BRULION_APP_HOOK_TOKEN";

} // namespace

HookServer::HookServer(QObject *parent) : QObject(parent), m_command(AgentShare::defaultCommand())
{
    m_server = new ServerProcess(u"Webhooks of scheduled conversations"_s, this);
    connect(m_server, &ServerProcess::stateChanged, this, [this] {
        if (m_server->state() != ServerProcess::State::Running)
            m_url.clear();
        Q_EMIT changed();
    });
    connect(m_server, &ServerProcess::announced, this, &HookServer::announced);
    connect(m_server, &ServerProcess::logLine, this, [](const QString &line) {
        // (The server masks every token in its lines; one line a request.)
        qCInfo(logging::lcNet).noquote() << QString(u"webhooks: "_s + line);
    });
}

HookServer::~HookServer()
{
    stop();
}

bool HookServer::available() const
{
    return WorkspaceAgents::available() && !m_command.isEmpty() && bool(m_grant);
}

void HookServer::setGrants(GrantFunction grant, RevokeFunction revoke)
{
    m_grant = std::move(grant);
    m_revoke = std::move(revoke);
    apply();
}

void HookServer::setConfig(const Config &config)
{
    Config c = config;
    c.address = c.address.trimmed() == u"tailnet" ? u"tailnet"_s : QString();
    if (c.port < 1024 || c.port > 65535)
        c.port = DefaultPort;
    c.publicUrl = c.publicUrl.trimmed();
    while (c.publicUrl.endsWith(u'/'))
        c.publicUrl.chop(1);
    if (c == m_config && (m_server->active() || !c.enabled))
        return;
    m_config = c;
    apply();
}

QString HookServer::state() const
{
    if (!m_config.enabled || !available())
        return u"off"_s;
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

QString HookServer::status() const
{
    if (!available())
        return tr("Webhooks need the desktop app with brulion-cli.");
    const QString s = state();
    if (s == u"off")
        return tr("Off: no scheduled conversation is started from elsewhere.");
    if (s == u"starting")
        return tr("Starting…");
    if (s == u"running") {
        const QString host = QUrl(baseUrl()).host();
        if (host == u"127.0.0.1" || host == u"localhost" || host == u"::1")
            return tr("On at %1 · this computer only: put your own reverse proxy or Tailscale Funnel in front to "
                      "reach it from elsewhere")
                    .arg(baseUrl());
        return tr("On at %1").arg(baseUrl());
    }
    const QString why = !m_launchError.isEmpty() ? m_launchError : m_server->error();
    if (s == u"waiting")
        return tr("Stopped: %1 — trying again soon").arg(why);
    return tr("Stopped: %1").arg(why);
}

QString HookServer::baseUrl() const
{
    if (!m_config.publicUrl.isEmpty())
        return m_config.publicUrl;
    if (!m_url.isEmpty())
        return m_url;
    if (m_config.address == u"tailnet" && !m_tailnetName.isEmpty())
        return u"http://"_s + m_tailnetName + u':' + QString::number(m_config.port);
    return u"http://127.0.0.1:"_s + QString::number(m_config.port);
}

QStringList HookServer::arguments(const Launch &l)
{
    QStringList args{u"app"_s, u"hooks"_s, u"serve"_s, u"--listen"_s, l.listen, u"--port"_s, QString::number(l.port)};
    if (!l.publicUrl.isEmpty())
        args << u"--public-url"_s << l.publicUrl;
    args << u"--stop-on-eof"_s;
    return args;
}

void HookServer::stop()
{
    ++m_detecting;
    m_server->stop();
    if (!m_token.isEmpty() && m_revoke)
        m_revoke(m_token);
    m_token.clear();
    m_url.clear();
}

void HookServer::apply()
{
    m_launchError.clear();
    stop();
    if (!m_config.enabled || !available()) {
        Q_EMIT changed();
        return;
    }
    // (Tailscale is looked up each time: the settings offer the tailnet's address when it runs.)
    if (!m_tailscale) {
        if (!m_ownTailscale)
            m_ownTailscale = std::make_unique<tailscale::LocalApi>();
        m_tailscale = m_ownTailscale.get();
    }
    const int serial = m_detecting;
    const Config config = m_config;
    QPointer<HookServer> self(this);
    m_tailscale->status([self, serial, config](const std::optional<tailscale::Status> &s, const QString &why) {
        if (!self || serial != self->m_detecting)
            return;
        const bool tailnet = s && s->running() && !s->dnsName.isEmpty() && !s->addresses.isEmpty();
        if (self->m_tailnetName != (tailnet ? s->dnsName : QString())) {
            self->m_tailnetName = tailnet ? s->dnsName : QString();
            Q_EMIT self->changed();
        }
        Launch l;
        l.port = config.port;
        l.publicUrl = config.publicUrl;
        if (config.address == u"tailnet") {
            if (!tailnet) {
                self->m_launchError = tr("Tailscale does not run here (%1): choose “This computer only”, or start it.")
                                              .arg(s ? s->backendState : why);
                qCWarning(logging::lcApp).noquote() << QString(u"Webhooks: "_s + self->m_launchError);
                Q_EMIT self->changed();
                return;
            }
            l.listen = (s->ipv4().isNull() ? s->addresses.first() : s->ipv4()).toString();
            if (l.publicUrl.isEmpty())
                l.publicUrl = u"http://"_s + s->dnsName + u':' + QString::number(config.port);
        }
        self->launch(l);
    });
    Q_EMIT changed();
}

void HookServer::launch(const Launch &l)
{
    m_token = m_grant ? m_grant() : QString();
    if (m_token.isEmpty()) {
        m_launchError = tr("the app's remote control could not give it a grant");
        Q_EMIT changed();
        return;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QString::fromLatin1(HookTokenVariable), m_token);
    m_server->setEnvironment(env);
    m_server->setProgram(m_command.first(), m_command.mid(1) + arguments(l));
    qCInfo(logging::lcApp).noquote() << u"Webhooks: starting on %1 port %2"_s.arg(l.listen, QString::number(l.port));
    m_server->start();
    Q_EMIT changed();
}

void HookServer::announced()
{
    m_url = m_server->announcement().value(u"url"_s).toString();
    qCInfo(logging::lcApp).noquote() << u"Webhooks: on at %1"_s.arg(m_url);
    Q_EMIT changed();
}

} // namespace brulion::shell
