#pragma once
// HookServer — the webhooks of scheduled conversations (docs/UI.md "Scheduled conversations" →
// "Webhooks"; Settings → Extensions → Scheduled conversations → Webhooks; QML:
// Workspace.chats.hooks): a request from elsewhere — a GitHub webhook, Home Assistant, a cron job on
// another machine, iOS Shortcuts — to a schedule's own address `<address>/hooks/<token>` starts a run
// of it (ChatSchedules::runHook()).
//
// On (AppSettings `ai/hooks`, off by default), the desktop app runs `brulion-cli app hooks serve
// --listen … --port … [--public-url …] --stop-on-eof` while it runs (ServerProcess: started again
// after a crash, stopped on quit; every line of its log in the event log, tokens masked by the
// server). The server is the command line's (its hardened HTTP server; src/cli/hooks.cpp); it passes
// each hook on to this app over the remote control socket with a grant of its own (grant(): the
// socket's GrantScope::Hook — hello and chatHook only — in BRULION_APP_HOOK_TOKEN of its environment,
// never on its command line), which ends when it stops. Where it listens: 127.0.0.1 by default (for
// a reverse proxy of the user's own or Tailscale Funnel in front: the public address the hooks'
// addresses start with), or this computer's tailnet address when tailscaled runs here ("tailnet":
// http://<its MagicDNS name>:<port>, reached by the user's own devices in the tailnet). The port: 8791.
//
// Desktop only (available(): programs run here, a command line is found, and a grant can be made —
// the remote control exists). Single-threaded.

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace brulion::tailscale {
class LocalApi;
}

namespace brulion::shell {

class ServerProcess;

class HookServer : public QObject {
    Q_OBJECT
    /// It can run here (the desktop app with brulion-cli and its remote control).
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    /// The switch (AppSettings::chatHooks).
    Q_PROPERTY(bool on READ on NOTIFY changed FINAL)
    /// "off" | "starting" | "running" | "waiting" (it ended: started again soon) | "failed".
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    /// One line for the settings: what it does or why not.
    Q_PROPERTY(QString status READ status NOTIFY changed FINAL)
    /// What a hook's address starts with ("http://127.0.0.1:8791", the public address …).
    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY changed FINAL)
    /// This computer's tailnet name when tailscaled runs here ("" none): the address choice offers it.
    Q_PROPERTY(QString tailnetName READ tailnetName NOTIFY changed FINAL)

public:
    explicit HookServer(QObject *parent = nullptr);
    ~HookServer() override;

    static constexpr int DefaultPort = 8791;

    struct Config {
        bool enabled = false;
        QString address; ///< "" this computer only (127.0.0.1) | "tailnet"
        int port = DefaultPort;
        QString publicUrl; ///< the address callers reach it at ("" none: the one it listens on)
        friend bool operator==(const Config &, const Config &) = default;
    };
    void setConfig(const Config &config);
    Config config() const { return m_config; }

    /// The remote control's grants of the webhooks (RemoteControl::hookGrant / revokeGrant in the
    /// desktop app); unset: it does not run.
    using GrantFunction = std::function<QString()>;
    using RevokeFunction = std::function<void(const QString &token)>;
    void setGrants(GrantFunction grant, RevokeFunction revoke);

    bool available() const;
    bool on() const { return m_config.enabled; }
    QString state() const;
    QString status() const;
    QString baseUrl() const;
    QString tailnetName() const { return m_tailnetName; }

    /// How it is started (pure).
    struct Launch {
        QString listen = QStringLiteral("127.0.0.1");
        int port = DefaultPort;
        QString publicUrl;
    };
    /// brulion-cli's arguments after the program's own for `launch`.
    static QStringList arguments(const Launch &launch);
    /// Tests: the command that runs brulion-cli (default AgentShare::defaultCommand()) and
    /// tailscaled (default this computer's).
    void setCommand(const QStringList &command) { m_command = command; }
    void setTailscale(tailscale::LocalApi *api) { m_tailscale = api; }
    ServerProcess *server() const { return m_server; }

Q_SIGNALS:
    void changed();

private:
    void apply();
    void launch(const Launch &launch);
    void stop();
    void announced();

    Config m_config;
    QStringList m_command;
    ServerProcess *m_server = nullptr;
    GrantFunction m_grant;
    RevokeFunction m_revoke;
    QString m_token; ///< the grant of the server running (or starting)
    tailscale::LocalApi *m_tailscale = nullptr;
    std::unique_ptr<tailscale::LocalApi> m_ownTailscale;
    QString m_tailnetName;
    QString m_url;         ///< what the running server said (its first line's url)
    QString m_launchError; ///< it could not be started so (no Tailscale …)
    int m_detecting = 0;   ///< a Tailscale lookup on its way (its serial)
};

} // namespace brulion::shell
