#pragma once
// tailscaled's LocalAPI, read only (tailscale.h): GET /localapi/v0/status and GET
// /localapi/v0/whois?addr=<ip:port> over its local socket — on Linux /run/tailscale/tailscaled.sock
// (also /var/run/…; macOS' open-source tailscaled /var/run/tailscaled.socket), on Windows the named
// pipe \\.\pipe\ProtectedPrefix\Administrators\Tailscale\tailscaled; BRULION_TAILSCALE_SOCKET names
// another (a tailscaled started with --socket, tests). Both are open to every user of the computer
// for reading, as `tailscale status` is. Where the socket cannot be reached (the macOS app, which
// talks to its CLI otherwise) the `tailscale` program answers the same: `tailscale status --json`,
// `tailscale whois --json <ip:port>`.
//
// Plain HTTP/1.0 (no keep-alive, no chunks: the whole answer, then the socket closes), the Host
// "local-tailscaled.sock" tailscaled insists on. Nothing is ever written to tailscaled.
//
//   tailscale::LocalApi api;
//   api.status([](const std::optional<tailscale::Status> &s, const QString &error) { … });
//   api.whois(peer, port, [](const std::optional<tailscale::Identity> &who, const QString &error) { … });
//
// Asynchronous, on the thread it lives in; a callback never comes after the object is gone.

#include "tailscale/tailscale.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace brulion::tailscale {

class LocalApi : public QObject {
    Q_OBJECT
public:
    explicit LocalApi(QObject *parent = nullptr);
    ~LocalApi() override;

    /// One answer: an HTTP status (0: none — `error` says why) and its body.
    struct Answer {
        int status = 0;
        QByteArray body;
        QString error;
    };
    using Done = std::function<void(const Answer &answer)>;

    /// The socket (default defaultSocketPath(); "" none: only the program).
    void setSocketPath(const QString &path) { m_socket = path; }
    QString socketPath() const { return m_socket; }
    /// The `tailscale` program asked when the socket is not reached (default defaultProgram();
    /// "" none).
    void setProgram(const QString &program) { m_program = program; }
    QString program() const { return m_program; }
    /// How long one question may take (default 5 s).
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    /// GET `pathAndQuery` ("/localapi/v0/status") over the socket.
    void get(const QString &pathAndQuery, Done done);
    /// This computer in its tailnet; nullopt with `error` when tailscaled answered nothing usable.
    void status(std::function<void(const std::optional<Status> &status, const QString &error)> done);
    /// Who is at `address`:`port`; nullopt with "" `error`: tailscaled knows nobody there (404),
    /// with an `error`: it was not asked (not reached, a malformed answer).
    void whois(const QHostAddress &address, quint16 port,
               std::function<void(const std::optional<Identity> &identity, const QString &error)> done);

    /// BRULION_TAILSCALE_SOCKET, else the platform's place where one is there (Windows: its pipe;
    /// elsewhere the first that exists of /run/tailscale/tailscaled.sock,
    /// /var/run/tailscale/tailscaled.sock, /var/run/tailscaled.socket; "" none).
    static QString defaultSocketPath();
    /// BRULION_TAILSCALE_PROGRAM, else `tailscale` on PATH, else the macOS app's
    /// (/Applications/Tailscale.app/Contents/MacOS/Tailscale; "" none).
    static QString defaultProgram();
    /// The largest answer read (a status with many devices stays far below).
    static constexpr qint64 MaxAnswerBytes = 16LL * 1024 * 1024;

private:
    void viaSocket(const QString &pathAndQuery, Done done);
    void viaProgram(const QStringList &arguments, Done done);

    QString m_socket;
    QString m_program;
    int m_timeoutMs = 5000;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

/// An HTTP answer read whole ("HTTP/1.0 200 OK\r\n…\r\n\r\n<body>"): its status (0 when it is none)
/// and its body (to Content-Length where it gives one).
LocalApi::Answer parseHttpAnswer(const QByteArray &bytes);

} // namespace brulion::tailscale
