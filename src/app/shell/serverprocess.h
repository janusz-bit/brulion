#pragma once
// ServerProcess — a brulion-cli server run while the app runs: a program that prints one JSON line
// when it serves ({"ok": true, "result": {…}}, as `acp serve` and `mcp serve` do) and serves until
// it is stopped. The app's "Share agents with the phone" runs `brulion-cli acp serve` with it
// (agentshare.h); it knows nothing of what the server is.
//
// * Starting: Starting until the first line of its standard output says {"ok": true} (Running,
//   announcement() its "result"); {"ok": false, "error": {message}} or an end before that is a
//   failure with the server's own words (error()).
// * Supervised: a server that ends by itself — it crashed, it could not listen yet — is started
//   again after a wait that grows with the failures in a row (retryDelays(), 2 s … 1 min; a run of a
//   minute — stableMs() — forgets them), Waiting meanwhile. One that refuses how it was started
//   (exit 2, a usage error; 7, refused by policy) or cannot be started at all is Failed: it is not
//   started again until start() is called (new arguments).
// * Stopped: its standard input is closed (`--stop-on-eof`: it ends by itself, its agents too),
//   then after stopTimeoutMs() it is terminated, and killed if that is not enough. The destructor
//   does the same, waiting (the app quits). A server whose app crashed sees its standard input end
//   too and never outlives it.
// * Its log: each line of its standard error comes as logLine() (the event log), without the
//   time the server puts in front.
//
//   ServerProcess server(u"AI agents for the phone"_s);
//   server.setProgram(cli, {u"--vault"_s, root, u"acp"_s, u"serve"_s, …, u"--stop-on-eof"_s});
//   server.start();   // → stateChanged(): Starting → Running (announcement()) / Waiting / Failed
//   server.stop();
//
// Single-threaded (the GUI thread). Desktop only: without QProcess (Android, the browser) start()
// fails at once.

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

namespace brulion::shell {

class ServerProcess : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Stopped,  ///< not running, nobody asked for it
        Starting, ///< started, its first line not read yet
        Running,  ///< it said it serves
        Waiting,  ///< it ended by itself: started again after retryIn()
        Failed,   ///< it refused how it was started, or could not be started: not again by itself
    };
    Q_ENUM(State)

    /// `name` for the log ("AI agents for the phone").
    explicit ServerProcess(const QString &name, QObject *parent = nullptr);
    ~ServerProcess() override;

    /// The program and its arguments for the next start().
    void setProgram(const QString &program, const QStringList &arguments);
    QString program() const { return m_program; }
    QStringList arguments() const { return m_arguments; }
    /// Its environment (default: the app's).
    void setEnvironment(const QProcessEnvironment &environment) { m_environment = environment; }
    /// The waits before it is started again, after the first, second … failure in a row (the
    /// last repeats). Default 2, 5, 10, 30, 60 s.
    void setRetryDelays(const QList<int> &ms) { m_delays = ms; }
    /// A run this long forgets the failures before it (default 60 s).
    void setStableMs(qint64 ms) { m_stableMs = ms; }
    /// How long a stop waits for it to end after its standard input closed, and again after it was
    /// terminated (default 3 s).
    void setStopTimeoutMs(int ms) { m_stopMs = ms; }

    /// Starts it with the program set (a running one is stopped first, then started anew).
    void start();
    /// Stops it for good (until start()).
    void stop();

    State state() const { return m_state; }
    bool active() const { return m_state == State::Starting || m_state == State::Running || m_state == State::Waiting; }
    /// "result" of its first line (Running; {} before).
    QJsonObject announcement() const { return m_announcement; }
    /// Why it is not running (Waiting, Failed): the server's message, its last line of standard
    /// error, or how it ended; "" none.
    QString error() const { return m_error; }
    /// Its error code where it gave one ({"error": {"code"}}: "no_tailscale", "cannot_serve" …).
    QString errorCode() const { return m_errorCode; }
    /// Failures in a row.
    int failures() const { return m_failures; }
    /// Waiting: in how many milliseconds it is started again.
    qint64 retryIn() const;
    /// Its process id while it runs (0 none).
    qint64 processId() const;

Q_SIGNALS:
    void stateChanged();
    /// It serves: `result` of its first line.
    void announced(const QJsonObject &result);
    /// A line of its standard error.
    void logLine(const QString &line);

private:
    void launch();
    void readOutput();
    void readErrors();
    void ended(int exitCode, bool crashed);
    void setState(State state);
    void halt(bool wait);

    QString m_name;
    QString m_program;
    QStringList m_arguments;
    QProcessEnvironment m_environment;
    QList<int> m_delays{2000, 5000, 10000, 30000, 60000};
    qint64 m_stableMs = 60000;
    int m_stopMs = 3000;
    QProcess *m_process = nullptr;
    QTimer *m_retry = nullptr;
    State m_state = State::Stopped;
    QJsonObject m_announcement;
    QString m_error;
    QString m_errorCode;
    QString m_lastErrorLine;
    QByteArray m_out;
    QByteArray m_err;
    int m_failures = 0;
    qint64 m_startedAt = 0;
    bool m_announced = false;
    /// A process stopped but not ended yet (its port still taken), and a start() waiting for it.
    QPointer<QProcess> m_stopping;
    bool m_pendingStart = false;
};

} // namespace brulion::shell
