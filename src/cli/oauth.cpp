// Secrets and the state file of the remote MCP server (oauth.h).
#include "oauth.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThread>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::cli::oauth {

namespace {

constexpr auto kFormat = "brulion-mcp-auth/1";
constexpr int kLockTimeoutMs = 5000;
constexpr int kMaxIterations = 10000000;
constexpr qsizetype kMaxStateBytes = 16 * 1024 * 1024;

QByteArray base64url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

/// PBKDF2-HMAC-SHA256 (RFC 8018) of 32 bytes (Qt's, optimized also in a debug build of ours).
QByteArray pbkdf2(const QByteArray &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password, salt, iterations, 32);
}

QJsonObject tokenJson(const Token &token)
{
    return {{u"hash"_s, token.hash}, {u"expires"_s, timeText(token.expires)}};
}

Token tokenFrom(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    return {o.value(u"hash"_s).toString(), parseTime(o.value(u"expires"_s).toString())};
}

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray()) {
        if (v.isString())
            out.append(v.toString());
    }
    return out;
}

} // namespace

// ---- Secrets

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    QRandomGenerator *random = QRandomGenerator::system();
    for (int i = 0; i < count; i += 4) {
        const quint32 value = random->generate();
        for (int k = 0; k < 4 && i + k < count; ++k)
            bytes[i + k] = char((value >> (8 * k)) & 0xff);
    }
    return bytes;
}

QString randomToken(const QString &prefix, int bytes)
{
    return prefix + QString::fromLatin1(base64url(randomBytes(bytes)));
}

QString tokenHash(const QString &token)
{
    return QString::fromLatin1(base64url(QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256)));
}

bool constantTimeEquals(QByteArrayView a, QByteArrayView b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

QString hashPassword(const QString &password, int iterations)
{
    const QByteArray salt = randomBytes(16);
    const QByteArray key = pbkdf2(password.toUtf8(), salt, iterations);
    return u"pbkdf2-sha256$%1$%2$%3"_s.arg(QString::number(iterations), QString::fromLatin1(salt.toBase64()),
                                           QString::fromLatin1(key.toBase64()));
}

namespace {

struct ParsedHash {
    int iterations = 0;
    QByteArray salt;
    QByteArray key;
};

std::optional<ParsedHash> parseHash(const QString &stored)
{
    const QStringList parts = stored.trimmed().split(u'$');
    if (parts.size() != 4 || parts.at(0) != u"pbkdf2-sha256")
        return std::nullopt;
    bool ok = false;
    const int iterations = parts.at(1).toInt(&ok);
    if (!ok || iterations < 1 || iterations > kMaxIterations)
        return std::nullopt;
    const auto salt = QByteArray::fromBase64Encoding(parts.at(2).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const auto key = QByteArray::fromBase64Encoding(parts.at(3).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!salt || !key || salt.decoded.size() < 4 || key.decoded.size() != 32)
        return std::nullopt;
    return ParsedHash{iterations, salt.decoded, key.decoded};
}

} // namespace

bool isPasswordHash(const QString &text)
{
    return parseHash(text).has_value();
}

bool verifyPassword(const QString &stored, const QString &password)
{
    const auto parsed = parseHash(stored);
    if (!parsed)
        return false;
    return constantTimeEquals(pbkdf2(password.toUtf8(), parsed->salt, parsed->iterations), parsed->key);
}

bool isPkceChallenge(const QString &challenge)
{
    return challenge.size() == 43 && std::ranges::all_of(challenge, [](QChar c) {
               return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'
                      || c == u'_';
           });
}

bool pkceMatches(const QString &verifier, const QString &challenge)
{
    // RFC 7636 4.1: 43–128 characters of [A-Z] / [a-z] / [0-9] / "-" / "." / "_" / "~".
    if (verifier.size() < 43 || verifier.size() > 128)
        return false;
    if (!std::ranges::all_of(verifier, [](QChar c) {
            return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'
                   || c == u'.' || c == u'_' || c == u'~';
        }))
        return false;
    const QByteArray computed = base64url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
    return constantTimeEquals(computed, challenge.toLatin1());
}

// ---- Times

QString timeText(const QDateTime &time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString();
}

QDateTime parseTime(const QString &text)
{
    QDateTime t = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!t.isValid())
        t = QDateTime::fromString(text, Qt::ISODate);
    return t.isValid() ? t.toUTC() : QDateTime();
}

// ---- State

bool Grant::canWrite() const
{
    return scopes.contains(u"notes:write"_s);
}

const Client *State::client(const QString &id) const
{
    const auto it = std::ranges::find_if(clients, [&id](const Client &c) { return c.id == id; });
    return it == clients.end() ? nullptr : &*it;
}

Grant *State::grant(const QString &id)
{
    const auto it = std::ranges::find_if(grants, [&id](const Grant &g) { return g.id == id; });
    return it == grants.end() ? nullptr : &*it;
}

const Grant *State::grant(const QString &id) const
{
    const auto it = std::ranges::find_if(grants, [&id](const Grant &g) { return g.id == id; });
    return it == grants.end() ? nullptr : &*it;
}

QJsonObject State::toJson() const
{
    QJsonArray clientList;
    for (const Client &c : clients) {
        QJsonObject o{{u"id"_s, c.id},
                      {u"name"_s, c.name},
                      {u"redirectUris"_s, QJsonArray::fromStringList(c.redirectUris)},
                      {u"authMethod"_s, c.authMethod},
                      {u"created"_s, timeText(c.created)}};
        if (!c.secretHash.isEmpty())
            o.insert(u"secretHash"_s, c.secretHash);
        if (!c.uri.isEmpty())
            o.insert(u"uri"_s, c.uri);
        if (!c.softwareId.isEmpty())
            o.insert(u"softwareId"_s, c.softwareId);
        if (!c.softwareVersion.isEmpty())
            o.insert(u"softwareVersion"_s, c.softwareVersion);
        clientList.append(o);
    }
    QJsonArray grantList;
    for (const Grant &g : grants) {
        QJsonArray access;
        for (const Token &t : g.access)
            access.append(tokenJson(t));
        QJsonArray retired;
        for (const Token &t : g.retired)
            retired.append(tokenJson(t));
        QJsonObject o{{u"id"_s, g.id},
                      {u"client"_s, g.clientId},
                      {u"clientName"_s, g.clientName},
                      {u"clientKind"_s, g.clientKind},
                      {u"redirectUri"_s, g.redirectUri},
                      {u"scopes"_s, QJsonArray::fromStringList(g.scopes)},
                      {u"resource"_s, g.resource},
                      {u"created"_s, timeText(g.created)},
                      {u"lastUsed"_s, timeText(g.lastUsed)},
                      {u"refreshed"_s, timeText(g.refreshed)},
                      {u"access"_s, access},
                      {u"refresh"_s, tokenJson(g.refresh)},
                      {u"retired"_s, retired}};
        if (!g.previous.hash.isEmpty()) {
            o.insert(u"previous"_s, tokenJson(g.previous));
            o.insert(u"previousRotated"_s, timeText(g.previousRotated));
        }
        grantList.append(o);
    }
    QJsonObject out{{u"format"_s, QString::fromLatin1(kFormat)}, {u"clients"_s, clientList}, {u"grants"_s, grantList}};
    if (!password.isEmpty())
        out.insert(u"password"_s, password);
    return out;
}

State State::fromJson(const QJsonObject &json)
{
    State s;
    s.password = json.value(u"password"_s).toString();
    for (const QJsonValue &v : json.value(u"clients"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Client c;
        c.id = o.value(u"id"_s).toString();
        if (c.id.isEmpty())
            continue;
        c.name = o.value(u"name"_s).toString();
        c.redirectUris = strings(o.value(u"redirectUris"_s));
        c.authMethod = o.value(u"authMethod"_s).toString(u"none"_s);
        c.secretHash = o.value(u"secretHash"_s).toString();
        c.uri = o.value(u"uri"_s).toString();
        c.softwareId = o.value(u"softwareId"_s).toString();
        c.softwareVersion = o.value(u"softwareVersion"_s).toString();
        c.created = parseTime(o.value(u"created"_s).toString());
        s.clients.append(c);
    }
    for (const QJsonValue &v : json.value(u"grants"_s).toArray()) {
        const QJsonObject o = v.toObject();
        Grant g;
        g.id = o.value(u"id"_s).toString();
        if (g.id.isEmpty())
            continue;
        g.clientId = o.value(u"client"_s).toString();
        g.clientName = o.value(u"clientName"_s).toString();
        g.clientKind = o.value(u"clientKind"_s).toString(u"registered"_s);
        g.redirectUri = o.value(u"redirectUri"_s).toString();
        g.scopes = strings(o.value(u"scopes"_s));
        g.resource = o.value(u"resource"_s).toString();
        g.created = parseTime(o.value(u"created"_s).toString());
        g.lastUsed = parseTime(o.value(u"lastUsed"_s).toString());
        g.refreshed = parseTime(o.value(u"refreshed"_s).toString());
        for (const QJsonValue &t : o.value(u"access"_s).toArray())
            g.access.append(tokenFrom(t));
        g.refresh = tokenFrom(o.value(u"refresh"_s));
        g.previous = tokenFrom(o.value(u"previous"_s));
        g.previousRotated = parseTime(o.value(u"previousRotated"_s).toString());
        for (const QJsonValue &t : o.value(u"retired"_s).toArray())
            g.retired.append(tokenFrom(t));
        s.grants.append(g);
    }
    return s;
}

void State::prune(const QDateTime &now)
{
    grants.removeIf([&now](const Grant &g) { return !g.refresh.expires.isValid() || g.refresh.expires <= now; });
    for (Grant &g : grants) {
        g.access.removeIf([&now](const Token &t) { return !t.expires.isValid() || t.expires <= now; });
        g.retired.removeIf([&now](const Token &t) { return !t.expires.isValid() || t.expires <= now; });
        if (g.previous.expires.isValid() && g.previous.expires <= now) {
            g.previous = {};
            g.previousRotated = {};
        }
    }
}

// ---- Store

Store::Store(QString path) : m_path(std::move(path)) { }

bool Store::changedOnDisk() const
{
    const QFileInfo info(m_path);
    if (!info.exists())
        return m_size != -1;
    return info.lastModified(QTimeZone::UTC) != m_modified || info.size() != m_size;
}

bool Store::load(QString *error)
{
    QFile file(m_path);
    const QFileInfo info(m_path);
    if (!info.exists()) {
        m_state = {};
        m_size = -1;
        m_modified = {};
        m_loaded = true;
        index();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.read(kMaxStateBytes + 1);
    if (bytes.size() > kMaxStateBytes) {
        if (error)
            *error = u"the file is too large"_s;
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()
        || doc.object().value(u"format"_s).toString() != QString::fromLatin1(kFormat)) {
        if (error)
            *error = u"not a state file of brulion-cli mcp serve"_s;
        return false;
    }
    m_state = State::fromJson(doc.object());
    m_modified = info.lastModified(QTimeZone::UTC);
    m_size = info.size();
    m_loaded = true;
    index();
    return true;
}

void Store::index()
{
    m_accessIndex.clear();
    for (const Grant &g : std::as_const(m_state.grants)) {
        for (const Token &t : g.access)
            m_accessIndex.insert(t.hash, g.id);
    }
}

bool Store::refresh(QString *error)
{
    if (m_loaded && !changedOnDisk())
        return true;
    return load(error);
}

bool Store::update(const std::function<bool(State &)> &change, QString *error)
{
    const QFileInfo info(m_path);
    QDir dir = info.absoluteDir();
    if (!dir.exists()) {
        if (!QDir().mkpath(dir.absolutePath())) {
            if (error)
                *error = u"cannot create %1"_s.arg(dir.absolutePath());
            return false;
        }
        QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                          | QFileDevice::ExeOwner);
    }
    QLockFile lock(m_path + u".lock"_s);
    if (!lock.tryLock(kLockTimeoutMs)) {
        if (error)
            *error = u"the state file is locked by another process"_s;
        return false;
    }
    if (!load(error))
        return false;
    State next = m_state;
    if (!change(next))
        return true;
    const QByteArray bytes = QJsonDocument(next.toJson()).toJson(QJsonDocument::Indented);
    // On Windows the file cannot be replaced while another process (`mcp clients`, a backup, an
    // indexer) has it open for a moment: the replacement is tried again for up to a second.
    for (int attempt = 0;; ++attempt) {
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error)
                *error = file.errorString();
            return false;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(bytes);
        if (file.commit())
            break;
        if (attempt >= 20) {
            if (error)
                *error = file.errorString();
            return false;
        }
        QThread::msleep(50);
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return load(error);
}

const Grant *Store::grantByAccessToken(const QString &token) const
{
    const auto it = m_accessIndex.constFind(tokenHash(token));
    return it == m_accessIndex.cend() ? nullptr : m_state.grant(*it);
}

} // namespace brulion::cli::oauth
