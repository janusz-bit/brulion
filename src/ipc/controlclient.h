// ControlClient — the other end of the remote control (controlprotocol.h, controlserver.h): one
// connection to the running app's control socket, requests answered in turn (blocking: for
// brulion-cli and tests, never inside the app).
//
//   ControlClient client(controlKeyFor(SingleInstance::keyFor(appData)));
//   if (!client.connect())                      // no app, or its remote control is off
//       …
//   const ControlClient::Response r = client.call(u"state"_s);
//   if (r.ok) use(r.result); else report(r.code, r.message);
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
    QString m_key;
    QString m_error;
    std::unique_ptr<QLocalSocket> m_socket;
    qint64 m_nextId = 1;
};

} // namespace brulion::ipc
