// AsyncControlClient — a lasting connection to the running app's control socket (controlprotocol.h,
// controlserver.h) for a server of brulion-cli that the app starts and that must never block on the
// app: `acp serve` of the app's agent sharing (a grant of its own, GrantScope::Share), which follows
// and steers the app's conversations for the user's phone (docs/CLI.md "AI agents for other
// devices" → "The app's own conversations"). ControlClient (controlclient.h) is the blocking one of
// the command line; this one runs on the event loop.
//
//   AsyncControlClient app(socketPath, token, u"agent sharing"_s);
//   connect(&app, &AsyncControlClient::readyChanged, …);   // the link came or went
//   app.start();                                           // connects, says hello with the token
//   app.call(u"chatLive"_s, {{u"since"_s, 12}}, 40000, [](const AsyncControlClient::Response &r) { … });
//
// * Ready once the app accepted its `hello {token, client}`: only then do calls go out. Requests are
//   multiplexed on the one connection by their ids; their answers come in any order.
// * Every call is answered once, and never inside call() itself (from the event loop): the app's
//   answer; or a transport failure (Response::transportError) — "disconnected" when the link is down
//   or drops before the answer (every call waiting fails then), "no_answer" after the call's time.
// * Gone, not listening yet, or refusing the token: connected again and again after a wait that
//   grows with the failures in a row (setRetryDelays(), 0.25 s … 5 s) until stop(). Only this
//   user's process is talked to (peerUid() / trustedPeer(), as ControlClient's connectToTrusted()).
//
// Single-threaded. Library brulion_ipc (desktop).
#pragma once

#include "controlclient.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QLocalSocket;
class QTimer;

namespace brulion::ipc {

class AsyncControlClient : public QObject {
    Q_OBJECT
public:
    using Response = ControlClient::Response;
    using Done = std::function<void(const Response &response)>;

    /// `key` the app's control socket, `token` the grant it gave, `client` the name its log says.
    AsyncControlClient(const QString &key, const QString &token, const QString &client, QObject *parent = nullptr);
    ~AsyncControlClient() override;

    /// Connects (and again whenever the link drops) until stop().
    void start();
    /// Ends the link: the calls waiting fail ("disconnected").
    void stop();
    /// The app accepted the hello: calls go through.
    bool isReady() const { return m_ready; }
    /// The app's answer to the hello ({protocol, version, methods …}; {} while not ready).
    QJsonObject hello() const { return m_hello; }
    /// Why the link is down: the app's refusal, why connecting failed ("" while ready, or before
    /// the first try).
    QString lastError() const { return m_error; }
    /// Calls waiting for their answers.
    int pending() const { return int(m_pending.size()); }
    /// The waits before connecting again after the first, second … failure in a row (the last
    /// repeats). Default 250, 500, 1000, 2000, 5000 ms.
    void setRetryDelays(const QList<int> &ms) { m_delays = ms; }

    /// Sends `method` with `params`; `done` gets the answer (or a transport failure), at most
    /// `timeoutMs` later — never before call() returns.
    void call(const QString &method, const QJsonObject &params, int timeoutMs, Done done);

Q_SIGNALS:
    /// The link came up (hello accepted) or went down.
    void readyChanged(bool ready);

private:
    struct Pending {
        Done done;
        QTimer *timer = nullptr;
    };
    void connectNow();
    void connected();
    void readLines();
    /// The link is gone (`why`): every call waiting fails, and it is connected again later.
    void dropped(const QString &why);
    void setReady(bool ready);
    void answer(qint64 id, const Response &response);
    /// `done` with a transport failure, from the event loop.
    void failLater(Done done, const QString &code, const QString &message);

    QString m_key;
    QString m_token;
    QString m_client;
    QPointer<QLocalSocket> m_socket;
    QTimer *m_retry = nullptr;
    QList<int> m_delays{250, 500, 1000, 2000, 5000};
    int m_failures = 0;
    bool m_running = false;
    bool m_ready = false;
    qint64 m_helloId = 0;
    qint64 m_nextId = 1;
    QJsonObject m_hello;
    QString m_error;
    QHash<qint64, Pending> m_pending;
};

} // namespace brulion::ipc
