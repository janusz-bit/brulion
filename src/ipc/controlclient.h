// ControlClient — the other end of the remote control (controlprotocol.h, controlserver.h): one
// connection to the running app's control socket, requests answered in turn (blocking: for
// brulion-cli and tests, never inside the app).
//
//   ControlClient client(controlKeyFor(SingleInstance::keyFor(appData)));
//   if (!client.connect())                      // no app, or its remote control is off
//       …
//   const ControlClient::Response r = client.call(u"state"_s);
//   if (r.ok) use(r.result); else report(r.code, r.message);
//
// Who it is (docs/CLI.md "Live changes"): BRULION_APP_TOKEN (a grant's token the app gave the
// agent that runs this program) and BRULION_APP_CLIENT (a name for the app's list of live
// changes) from the environment, or setIdentity(); connect() then sends them in a `hello` first.
// When the app refuses the token, every call() answers that refusal (`unauthorized`).
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <memory>

class QLocalSocket;

namespace brulion::ipc {

class ControlClient {
public:
    explicit ControlClient(const QString &key);
    ~ControlClient();

    /// The environment variables of a grant's token and of the client's name.
    static constexpr auto TokenVariable = "BRULION_APP_TOKEN";
    static constexpr auto ClientVariable = "BRULION_APP_CLIENT";
    /// Who connect() says it is (default: the environment's); "" sends nothing of it.
    void setIdentity(const QString &token, const QString &client);
    QString token() const { return m_token; }
    QString client() const { return m_client; }

    /// Connect to the app. False when nothing listens (no app, or its remote control is off), or
    /// with errorString() when another user's process listens there (never talked to: it could
    /// answer in the app's place and read what is sent).
    bool connect(int timeoutMs = 1000);
    bool isConnected() const;
    /// Why connect() refused the socket ("" when nothing listened or it connected).
    QString errorString() const { return m_error; }

    struct Response {
        bool ok = false;
        /// The request could not be delivered or no answer came: code "disconnected" or "no_answer".
        bool transportError = false;
        QJsonValue result;
        QString code;
        QString message;
        QJsonObject details;
    };
    /// Send one request and wait for its response (other responses are skipped). `timeoutMs`:
    /// how long to wait for it (a waitFor request should allow its own time and a margin).
    Response call(const QString &method, const QJsonObject &params = {}, int timeoutMs = 15000);

private:
    Response send(const QString &method, const QJsonObject &params, int timeoutMs);

    QString m_key;
    QString m_error;
    QString m_token;
    QString m_client;
    std::unique_ptr<QLocalSocket> m_socket;
    qint64 m_nextId = 1;
    /// The app refused the hello of connect(): every call() answers it.
    std::unique_ptr<Response> m_refused;
};

} // namespace brulion::ipc
