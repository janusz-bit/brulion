// McpServerSupervisor — Settings → Extensions → "claude.ai and ChatGPT" (docs/UI.md "AI"): the
// remote MCP server (`brulion-cli mcp serve`, docs/CLI.md "Remote MCP server") run in the
// background while the desktop app runs, for the notebook open now, so that claude.ai, the Claude
// apps and ChatGPT reach the notes through a connector. QML: AppController.mcpServer.
//
// The server is the command line's, as a child process — brulion-cli next to the app, else the
// app's own binary as `brulion cli …` (defaultProgram()) — started with the settings (port, public
// address, read only: Config) and the sign-in password the app keeps in its secrets
// (`<app data>/secrets/mcp-password`, passed with --password-file; never in the notebook). Its
// state follows the process:
//
//   off       not on, or no program here (available() false: Android, the browser)
//   starting  started, its first line not there yet
//   running   it listens: its first line's `endpoint` is the address to paste into the apps
//   failed    no notebook open, no password set, or it refused to start (error(): its message —
//             the port in use, a public address that is no https …), shown until the settings
//             change or restart()
//   waiting   it ended by itself after it ran (error(): its last words): started again after
//             1, 2, 4 … 60 s (restartDelay()); one that ran a minute starts the count again
//
// Its standard error's lines (one per request: method, path, status, time — never a token) go
// to the event log (brulion.net) and the last ones to log(). A change of the configuration starts
// it again; the app quitting stops it.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace brulion::shell {

class McpServerSupervisor : public QObject {
    Q_OBJECT
    /// A server can run here (the desktop app).
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    /// "off" | "starting" | "running" | "failed" | "waiting" (above).
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    /// The MCP endpoint to paste into claude.ai or ChatGPT ("<public address>/mcp"), "" while it
    /// does not run.
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY changed FINAL)
    /// The endpoint is https: the providers' apps can reach it (a plain http one is this
    /// computer's only — a tunnel or a proxy has to be named as the public address).
    Q_PROPERTY(bool reachable READ reachable NOTIFY changed FINAL)
    /// Why it does not run ("" none), for the user.
    Q_PROPERTY(QString error READ error NOTIFY changed FINAL)
    /// Its last lines (standard error), the oldest first.
    Q_PROPERTY(QStringList log READ log NOTIFY logChanged FINAL)

public:
    struct Config {
        bool enabled = false;
        QString vault;        ///< the notebook's folder ("" none: off)
        int port = 8780;
        QString publicUrl;    ///< "" this computer's address (http://127.0.0.1:<port>)
        bool readOnly = false;
        QString passwordFile; ///< the secret's file; "" none set (it is not started)
        friend bool operator==(const Config &, const Config &) = default;
    };

    /// Lines of log() kept.
    static constexpr int MaxLogLines = 50;

    explicit McpServerSupervisor(QObject *parent = nullptr);
    ~McpServerSupervisor() override;

    /// Can a server run here at all (a desktop system that starts programs)?
    static bool available();
    /// What runs the server: brulion-cli next to the app, else {the app, "cli"}; empty where
    /// none runs.
    static QStringList defaultProgram();
    /// The program and its first arguments (defaultProgram(); tests name brulion-cli).
    void setProgram(const QStringList &program);
    /// The arguments after the program for `config` (pure).
    static QStringList arguments(const Config &config);

    /// Starts, stops or restarts the server as `config` says (nothing when it is the same).
    void setConfig(const Config &config);
    Config config() const { return m_config; }
    /// Starts it again now (after a failure, or while it waits to).
    Q_INVOKABLE void restart();
    /// Puts `text` on the clipboard (the endpoint); false without one.
    Q_INVOKABLE bool copy(const QString &text) const;

    QString state() const { return m_state; }
    QString endpoint() const { return m_endpoint; }
    bool reachable() const;
    QString error() const { return m_error; }
    QStringList log() const { return m_log; }
    /// The server's process id while it runs (0 none: tests).
    qint64 processId() const;

    /// The delay before the next start after `failures` ends in a row (1 s, 2 s, 4 s … 60 s).
    static int restartDelayMs(int failures);
    /// Tests: every delay this short instead.
    void setRestartDelayForTests(int ms) { m_testDelayMs = ms; }

Q_SIGNALS:
    void changed();
    void logChanged();

private:
    void apply();
    void start();
    void stop();
    void readOutput();
    void readErrors();
    void ended(int exitCode, QProcess::ExitStatus status);
    void setState(const QString &state, const QString &error = QString());
    void addLog(const QString &line);
    bool wanted() const;

    Config m_config;
    QStringList m_program;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_out;
    QByteArray m_err;
    bool m_sawFirstLine = false;
    bool m_refused = false; // its first line said why it does not start
    QString m_state = QStringLiteral("off");
    QString m_endpoint;
    QString m_error;
    QString m_lastWords; // its last line of standard error
    QStringList m_log;
    QDateTime m_runningSince;
    int m_failures = 0;
    QTimer m_restart;
    int m_testDelayMs = 0;
};

} // namespace brulion::shell
