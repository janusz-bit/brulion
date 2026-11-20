// The sign-in state of `brulion-cli mcp serve` (docs/CLI.md "Remote MCP server"): secrets and
// their hashes, and the state file that keeps the owner's password hash, the registered clients
// and the connections (grants) with their tokens — only hashes of them — so that a restart keeps
// every connection and `brulion-cli mcp revoke` takes effect in a running server.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion::cli::oauth {

// ---- Secrets

/// PBKDF2-HMAC-SHA256 iterations of a new password hash (OWASP's 2023 recommendation).
constexpr int kPasswordIterations = 600000;
/// The shortest password `mcp password` accepts.
constexpr int kMinPasswordLength = 8;

/// `count` bytes from the system's cryptographic random source.
QByteArray randomBytes(int count);
/// `prefix` + `bytes` random bytes as base64url without padding ("bat_…").
QString randomToken(const QString &prefix, int bytes = 32);
/// What the state file keeps of a token: SHA-256, base64url.
QString tokenHash(const QString &token);
/// Equal bytes, in a time that depends on the length only.
bool constantTimeEquals(QByteArrayView a, QByteArrayView b);
/// "pbkdf2-sha256$<iterations>$<salt>$<hash>" (base64) of `password` with a fresh salt.
QString hashPassword(const QString &password, int iterations = kPasswordIterations);
/// Is `text` a hash made by hashPassword()?
bool isPasswordHash(const QString &text);
/// Does `password` match `stored`? False for anything malformed.
bool verifyPassword(const QString &stored, const QString &password);
/// PKCE S256 (RFC 7636): BASE64URL(SHA256(verifier)) == challenge; the verifier 43–128 unreserved
/// characters.
bool pkceMatches(const QString &verifier, const QString &challenge);
/// A code_challenge: 43 base64url characters (a SHA-256).
bool isPkceChallenge(const QString &challenge);

// ---- The state

/// A client registered through dynamic client registration (RFC 7591). A client known by its
/// metadata document (an https:// client_id) is not kept: its document is fetched when needed.
struct Client {
    QString id;
    QString name;
    QStringList redirectUris;
    QString authMethod = QStringLiteral("none"); ///< none, client_secret_post, client_secret_basic
    QString secretHash;                          ///< confidential clients
    QString uri;
    QString softwareId;
    QString softwareVersion;
    QDateTime created;
};

struct Token {
    QString hash;
    QDateTime expires;
};

/// One connection: what the owner allowed one client, with its tokens.
struct Grant {
    QString id;
    QString clientId;
    QString clientName;
    QString clientKind; ///< "registered" or "metadata" (a client ID metadata document)
    QString redirectUri;
    QStringList scopes;
    QString resource;
    QDateTime created;
    QDateTime lastUsed;
    QDateTime refreshed;
    QList<Token> access;
    Token refresh;
    /// The refresh token before the current one, for a client whose answer got lost (it may use
    /// it again for a short while).
    Token previous;
    QDateTime previousRotated;
    /// Older refresh tokens: one of them used again means it was stolen (the grant is revoked).
    QList<Token> retired;

    bool canWrite() const;
};

struct State {
    QString password; ///< hashPassword() of the owner's password; "" = none set
    QList<Client> clients;
    QList<Grant> grants;

    const Client *client(const QString &id) const;
    Grant *grant(const QString &id);
    const Grant *grant(const QString &id) const;
    QJsonObject toJson() const;
    static State fromJson(const QJsonObject &json);
    /// Tokens and grants past their time dropped; clients without grants kept.
    void prune(const QDateTime &now);
};

/// The state file: read when it changed on disk, changed under a lock (QLockFile) and written
/// atomically with only the owner allowed to read it.
class Store {
public:
    explicit Store(QString path);
    const QString &path() const { return m_path; }
    /// Read it again if the file changed since (a missing file is an empty state).
    bool refresh(QString *error);
    const State &state() const { return m_state; }
    /// Lock, read, change and write: `change` returns false to write nothing.
    bool update(const std::function<bool(State &)> &change, QString *error);

    // Lookups over the state as read last.
    /// The grant whose access token this is (any expiry), or nullptr.
    const Grant *grantByAccessToken(const QString &token) const;

private:
    bool load(QString *error);
    bool changedOnDisk() const;
    void index();

    QString m_path;
    State m_state;
    bool m_loaded = false;
    QDateTime m_modified;
    qint64 m_size = -1;
    QHash<QString, QString> m_accessIndex; ///< token hash -> grant id
};

/// Times in the state file: ISO 8601, UTC, milliseconds.
QString timeText(const QDateTime &time);
QDateTime parseTime(const QString &text);

} // namespace brulion::cli::oauth
