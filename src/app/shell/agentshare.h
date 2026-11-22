#pragma once
// AgentShare — "Share agents with the phone" (Settings → Extensions → AI agents; docs/UI.md "AI
// agents (ACP)" → "Sharing them with the phone"; Workspace.chats.share): the AI agents set up on
// this computer (Claude Code, Codex …) for the app on the user's other devices, with one switch.
//
// * On, it runs `brulion-cli --vault <the open notebook> acp serve --agents <file> --stop-on-eof …`
//   while the app runs (ServerProcess: started again after a crash, stopped on quit, its state and
//   errors in the row and in the event log). It serves the notebook the app has open — no copy of
//   its own, no sync of its own: the agents' changes are seen here at once — with the agents of
//   this device (AppSettings::acpAgents, programs only), written to `<app data>/agent-share/
//   agents.json`. Not for the documentation notebook. Changing the notebook, the agents or the
//   settings starts it again.
// * Where it is reached: by itself, Tailscale when tailscaled runs here — `--tailscale`, this
//   computer's tailnet name (http://nixos.tail1ea20e.ts.net:8790), and the user's own devices
//   there pair without a code (trust, the default; tailscaled vouches for them, docs/CLI.md "In a
//   tailnet") —, else this computer's address in its network (http://192.168.1.10:8790,
//   `--allow-http`: not encrypted on the way, the row says so). An address the user types
//   (AppSettings::acpShareAddress) wins: an address of this computer, a tailnet name (https: behind
//   their own Tailscale Serve), or an https:// address of their own proxy.
// * Pairing: newPairing() runs `acp pair` (a code for 10 minutes, once) — the row shows its QR code
//   and the brulion://pair link; unpairAll() runs `acp revoke --all`; devices() counts them.
// * A phone with the same notebook finds the computer by itself: in a tailnet the address and the
//   agents' names go into the notebook's `.brulion/config.json` (core acp/hostoffer.h; never a
//   token or a code), and every device lists the offers of the other computers there (offers():
//   those not added yet). Turned off, the computer's offer goes.
//
// Desktop only (available(): programs run here, and brulion-cli — or the app's own `brulion cli`
// — is found). Single-threaded.

#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>
#include <memory>
#include <utility>

class QTimer;

namespace brulion {
struct VaultChange;
namespace tailscale {
class LocalApi;
}
} // namespace brulion

namespace brulion::shell {

class ServerProcess;
class Workspace;
class WorkspaceAgents;

class AgentShare : public QObject
{
    Q_OBJECT
    /// It can run here (the desktop app with brulion-cli).
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
    /// The switch (AppSettings::acpShare).
    Q_PROPERTY(bool on READ on NOTIFY changed FINAL)
    /// "off" | "notebook" (none to serve) | "agents" (none set up) | "starting" | "running" |
    /// "waiting" (it ended: started again soon) | "failed".
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    /// One line for the row: what it does or why not.
    Q_PROPERTY(QString status READ status NOTIFY changed FINAL)
    /// The address the devices are given ("" while it does not run).
    Q_PROPERTY(QString url READ url NOTIFY changed FINAL)
    /// It runs in the tailnet; the login whose devices pair without a code ("" none).
    Q_PROPERTY(bool tailnet READ tailnet NOTIFY changed FINAL)
    Q_PROPERTY(QString owner READ owner NOTIFY changed FINAL)
    /// It runs on a plain http address of the network (not encrypted on the way).
    Q_PROPERTY(bool unencrypted READ unencrypted NOTIFY changed FINAL)
    /// The address it finds by itself (the address field's placeholder; "" not known yet).
    Q_PROPERTY(QString automaticAddress READ automaticAddress NOTIFY changed FINAL)
    /// Devices paired with it (`acp clients`; -1 not known).
    Q_PROPERTY(int devices READ devices NOTIFY devicesChanged FINAL)
    /// `acp pair` runs; its code, its link (brulion://pair?…) and until when it is valid.
    Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairCode READ pairCode NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairLink READ pairLink NOTIFY pairingChanged FINAL)
    Q_PROPERTY(QString pairExpires READ pairExpires NOTIFY pairingChanged FINAL)
    /// The agents other computers offer in the notebook and this device does not have yet:
    /// {url, host, agent (its id there), name}.
    Q_PROPERTY(QVariantList offers READ offers NOTIFY offersChanged FINAL)

public:
    AgentShare(Workspace *workspace, WorkspaceAgents *agents, QObject *parent = nullptr);
    ~AgentShare() override;

    /// The port it listens on by default.
    static constexpr quint16 DefaultPort = 8790;

    bool available() const;
    bool on() const { return m_on; }
    QString state() const;
    QString status() const;
    QString url() const { return m_url; }
    bool tailnet() const { return m_tailnet; }
    QString owner() const { return m_owner; }
    bool unencrypted() const;
    QString automaticAddress() const { return m_automatic; }
    int devices() const { return m_devices; }
    bool pairing() const { return m_pairing; }
    QString pairCode() const { return m_pairCode; }
    QString pairLink() const { return m_pairLink; }
    QString pairExpires() const { return m_pairExpires; }
    QVariantList offers() const { return m_offers; }

    /// AppSettings: the switch, the address typed ("" by itself), whether the user's tailnet
    /// devices pair without a code.
    void setSettings(bool on, const QString &address, bool trust);
    /// The notebook it serves: the open one (`shareable` false: the documentation notebook).
    void setNotebook(const QString &root, bool shareable);
    /// The agents of this device changed (WorkspaceAgents::setAgents()).
    void agentsChanged();

    /// A pairing code for a device (`acp pair`): pairingChanged() with it, or message() why not.
    Q_INVOKABLE void newPairing();
    /// Forgets the code shown.
    Q_INVOKABLE void closePairing();
    /// Unpairs every device (`acp revoke --all`).
    Q_INVOKABLE void unpairAll();

    /// Tests: the command that runs brulion-cli (its program and the arguments before its own;
    /// default defaultCommand()), tailscaled (default: this computer's), the agents' file.
    void setCommand(const QStringList &command) { m_command = command; }
    void setTailscale(tailscale::LocalApi *api) { m_tailscale = api; }
    /// The port when the address is found by itself (default DefaultPort; 0: any free one).
    void setPort(quint16 port) { m_port = port; }
    ServerProcess *server() const { return m_server; }

    // ---- pure -------------------------------------------------------------------------------
    /// How it is started.
    struct Launch {
        QString root;       ///< the notebook
        QString agentsFile; ///< the agents' JSON ([{id, name, command}])
        QString address;    ///< typed by the user ("": by itself)
        bool tailscale = false; ///< by itself: Tailscale runs here
        QString lan;            ///< by itself without Tailscale: this computer's address in its network
        bool trust = true;      ///< the user's tailnet devices pair without a code
        quint16 port = DefaultPort;
    };
    /// brulion-cli's arguments (after the command's own) for `launch`; {} with `error` (words for
    /// the user) when it cannot be started so.
    static QStringList serveArguments(const Launch &launch, QString *error);
    /// The address of this computer in its network among `addresses`: a private IPv4 address
    /// (10/8, 172.16/12, 192.168/16) first, else another IPv4 that is no loopback, link-local or
    /// tailnet address ("" none).
    static QString lanAddress(const QList<QHostAddress> &addresses);
    /// brulion-cli next to the app or on PATH; else the AppImage itself, or the app's binary,
    /// with `cli` ({} none).
    static QStringList defaultCommand();
    /// The offers of `config` (config.json's object) this device can add: not its own computer's
    /// (`ownUrl`, `ownHost`), not an agent `added` already ({url, remote} of the agents here).
    static QVariantList offersFrom(const QJsonObject &config, const QString &ownUrl, const QString &ownHost,
                                   const QList<std::pair<QString, QString>> &added);

Q_SIGNALS:
    void changed();
    void devicesChanged();
    void pairingChanged();
    void offersChanged();
    /// Words for a toast (kind "success" | "error" | "info").
    void message(const QString &text, const QString &kind);

private:
    void applySoon();
    void apply();
    void launch(const Launch &launch);
    void serverChanged();
    void announced(const QJsonObject &result);
    QString writeAgentsFile(QString *error) const;
    /// Runs a command of brulion-cli for the notebook (`acp pair` …); `done` with its JSON result.
    void runCli(const QStringList &arguments, std::function<void(bool ok, const QJsonObject &json)> done);
    void countDevices();
    void writeOffer();
    void removeOffer();
    void readOffers();
    void watchVault();
    void onVaultChanged(const QList<brulion::VaultChange> &changes);

    Workspace *m_workspace = nullptr;
    WorkspaceAgents *m_agents = nullptr;
    ServerProcess *m_server = nullptr;
    QTimer *m_applyTimer = nullptr;
    QStringList m_command;
    tailscale::LocalApi *m_tailscale = nullptr;
    std::unique_ptr<tailscale::LocalApi> m_ownTailscale;
    QPointer<QObject> m_watched;
    bool m_on = false;
    QString m_address;
    bool m_trust = true;
    QString m_root;
    bool m_shareable = false;
    QString m_waiting; ///< "notebook" | "agents" | "" — why it does not run while on
    QString m_launchKey; ///< what the running server was started with
    QString m_url;
    bool m_tailnet = false;
    QString m_owner;
    QString m_automatic;
    QString m_launchError; ///< it could not be started so (no address …)
    QString m_offeredUrl;  ///< the offer this computer wrote into the notebook
    QString m_offeredRoot;
    int m_devices = -1;
    bool m_pairing = false;
    QString m_pairCode;
    QString m_pairLink;
    QString m_pairExpires;
    QVariantList m_offers;
    int m_detecting = 0; ///< a Tailscale lookup on its way (its serial)
    quint16 m_port = DefaultPort;
};

} // namespace brulion::shell
