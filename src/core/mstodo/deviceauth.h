// OAuth 2.0 device-code sign-in at the Microsoft identity platform (v2 endpoint) for the To Do
// download: the user's own app registration (a public client: no secret, "Allow public client
// flows" on), delegated permission Tasks.Read, no refresh token. Part of the mstodo module — see
// mstodo/mstodo.h for the overview.
#pragma once

#include "sync/http.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringView>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace brulion::mstodo {

/// The authority for personal Microsoft accounts (outlook.com, hotmail.com, live.com) — what an
/// app registered for "Personal Microsoft accounts only" needs.
inline constexpr QStringView DefaultAuthority = u"consumers";
/// What the sign-in asks for: reading the user's To Do lists and tasks.
inline constexpr QStringView Scope = u"Tasks.Read";
/// The Microsoft identity platform.
inline constexpr QStringView LoginHost = u"login.microsoftonline.com";

/// Error text for an Application (client) ID ("" when usable): a GUID such as
/// "3f2504e0-4f89-41d3-9a0c-0305e82c3301" (braces and surrounding spaces are tolerated).
QString validateClientId(const QString &clientId);
/// The client id as sent: trimmed, braces removed, lower case.
QString normalizedClientId(const QString &clientId);
/// Error text for an authority ("" when usable): "consumers", "organizations", "common", a
/// tenant id (GUID) or a tenant domain ("contoso.onmicrosoft.com").
QString validateAuthority(const QString &authority);
/// The authority as sent: trimmed, lower case; DefaultAuthority when empty.
QString normalizedAuthority(const QString &authority);

/// A failure explained for the user.
struct AuthError {
    /// "invalid-client-id", "invalid-authority", "network", "server", "declined", "expired",
    /// "bad-code", "public-client-disabled", "unknown-client", "wrong-account-type", "consent",
    /// "cancelled".
    QString code;
    QString message; ///< translated; what happened and what to do
};

/// An error answer of the identity platform explained: `error` ("authorization_declined",
/// "invalid_client", …), `description` (its error_description, with "AADSTS7000218: …") and the
/// AADSTS numbers of `error_codes`.
AuthError describeAuthError(const QString &error, const QString &description, const QList<int> &codes = {});

/// One sign-in: asks for a device code, shows it (codeReady), polls the token endpoint until the
/// user has signed in at the verification address (signedIn) or the code expires, the user
/// declines, or something fails (failed). Each run emits exactly one of signedIn / failed —
/// none after cancel().
class DeviceCodeLogin : public QObject {
    Q_OBJECT
public:
    /// `transport` is not owned and must outlive the sign-in.
    explicit DeviceCodeLogin(sync::HttpTransport *transport, QObject *parent = nullptr);
    ~DeviceCodeLogin() override;

    /// Start a sign-in (cancels a running one). Invalid input fails at once (failed()).
    void start(const QString &clientId, const QString &authority = DefaultAuthority.toString());
    /// Stop polling; no signal follows.
    void cancel();
    bool active() const { return m_active; }
    /// Current polling interval in seconds (the server's, +5 for every slow_down).
    int intervalSeconds() const { return m_interval; }
    /// Length of one "second" of the server's interval / expiry in ms (default 1000; tests).
    void setTimeUnitMs(int ms) { m_unitMs = qMax(1, ms); }
    /// Request time limit in ms (default 30 s).
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

Q_SIGNALS:
    /// Show `userCode` and `verificationUri` (https://microsoft.com/devicelogin): the user opens
    /// the address on any device and types the code there within `expiresInSeconds`.
    void codeReady(const QString &userCode, const QUrl &verificationUri, int expiresInSeconds);
    /// The access token (about an hour); never stored by this class.
    void signedIn(const QString &accessToken);
    void failed(const QString &code, const QString &message);

private:
    void requestCode();
    void poll();
    void fail(const AuthError &error);
    void send(const QString &endpoint, const QByteArray &body, std::function<void(const sync::HttpResponse &)> done);
    QString endpoint(QStringView path) const;

    sync::HttpTransport *m_transport;
    QTimer m_pollTimer;
    QElapsedTimer m_started;
    QString m_clientId;
    QString m_authority;
    QString m_deviceCode;
    qint64 m_expiresMs = 0;
    int m_interval = 5;
    int m_unitMs = 1000;
    int m_timeoutMs = 30000;
    int m_networkErrors = 0;
    int m_generation = 0;
    bool m_active = false;
};

} // namespace brulion::mstodo
