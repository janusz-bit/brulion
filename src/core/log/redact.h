// Redaction of secrets in free text. Applied by the Logger to every message (before the ring,
// the file and the previous message handler), and by the sync history and the sync UI to every
// error message they keep or show. Idempotent, thread-safe, never throws.
#pragma once

#include <QString>
#include <QStringView>

namespace brulion::logging {

/// What a secret is replaced with.
inline constexpr QStringView kMask = u"***";

/// Removes:
///  * registered secrets (addSecret) in their plain, percent-encoded (UTF-8) and Base64 forms;
///  * header values of Authorization, Proxy-Authorization, Cookie, Set-Cookie, X-API-Key,
///    X-Auth-Token, requesttoken — as "Name: value" (to the end of the line), QDebug pairs
///    ("Authorization", "Basic …") and JSON ("authorization": "…");
///  * "Basic <token>", "Bearer <token>" anywhere;
///  * user info in URLs: "https://jan:tajne@host/…" -> "https://***@host/…" (any scheme, IPv6
///    hosts, percent-encoded user info);
///  * values of secret-like keys in query strings ("?token=abc&x=1" -> "?token=***&x=1"),
///    key=value pairs (to the next & ; , quote or the end of the line) and "key": "value":
///    names ending in password, passwd, pwd, passphrase, secret, token, apikey, api_key,
///    sessionid, session, auth, signature, credential(s) (any prefix: app_password,
///    access_token, nc_session_id), and exactly pass, sid, sig; "code" only in a URL query
///    (OAuth). Case-insensitive; "passage", "keyboard=pl", "monkey=1", "author=Jan" stay;
///  * the Nextcloud app-password shape "xxxxx-xxxxx-xxxxx-xxxxx-xxxxx" and JWTs (eyJ….….…).
/// Text without any trigger character and without registered secrets is returned unchanged
/// (cheap).
QString redact(QStringView text);
/// Overload for QString expressions (string builders).
inline QString redact(const QString &text)
{
    return redact(QStringView(text));
}

/// A secret known at run time (the stored app password, the Basic token a WebDavClient
/// built, the Syncthing API key). Reference-counted; values shorter than 4 characters are
/// ignored (they would mask ordinary words). Process-wide, thread-safe.
void addSecret(const QString &secret);
void removeSecret(const QString &secret);
/// Forget every registered secret (tests).
void clearSecrets();
/// Number of registered secrets (tests).
int secretCount();

/// RAII registration (members of WebDavClient, SyncthingClient, the app settings).
class ScopedSecret {
public:
    ScopedSecret() = default;
    explicit ScopedSecret(QString secret);
    ~ScopedSecret();
    ScopedSecret(ScopedSecret &&other) noexcept;
    ScopedSecret &operator=(ScopedSecret &&other) noexcept;
    ScopedSecret(const ScopedSecret &) = delete;
    ScopedSecret &operator=(const ScopedSecret &) = delete;

    /// Replace the registered value ("" = none).
    void reset(const QString &secret = {});
    QString value() const { return m_secret; }

private:
    QString m_secret;
};

} // namespace brulion::logging
