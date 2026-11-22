#include "tailscale/tailscale.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

using namespace Qt::StringLiterals;

namespace brulion::tailscale {

namespace {

QString withoutTrailingDot(QString name)
{
    while (name.endsWith(u'.'))
        name.chop(1);
    return name;
}

qint64 idOf(const QJsonValue &v)
{
    // (tailscaled writes user ids as numbers; a string is taken too.)
    return v.isString() ? v.toString().toLongLong() : v.toInteger();
}

std::optional<QJsonObject> objectOf(QByteArrayView json, QString *error)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json.data(), json.size()), &parse);
    if (!doc.isObject()) {
        if (error)
            *error = parse.error != QJsonParseError::NoError ? u"not JSON: %1"_s.arg(parse.errorString())
                                                             : u"not a JSON object"_s;
        return std::nullopt;
    }
    return doc.object();
}

/// An address as tailscaled writes it: "100.98.93.11", "100.98.93.11/32" (a node's Addresses).
QHostAddress addressOf(const QString &text)
{
    const qsizetype slash = text.indexOf(u'/');
    return QHostAddress(slash < 0 ? text : text.left(slash));
}

QHostAddress plain(const QHostAddress &address)
{
    bool v4 = false;
    const quint32 ipv4 = address.toIPv4Address(&v4);
    return v4 ? QHostAddress(ipv4) : address;
}

} // namespace

QHostAddress Status::ipv4() const
{
    for (const QHostAddress &a : addresses) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol)
            return a;
    }
    return {};
}

std::optional<Status> parseStatus(QByteArrayView json, QString *error)
{
    const std::optional<QJsonObject> o = objectOf(json, error);
    if (!o)
        return std::nullopt;
    const QJsonObject self = o->value(u"Self"_s).toObject();
    if (self.isEmpty()) {
        if (error)
            *error = u"no Self in tailscaled's status"_s;
        return std::nullopt;
    }
    Status s;
    s.backendState = o->value(u"BackendState"_s).toString();
    s.dnsName = withoutTrailingDot(self.value(u"DNSName"_s).toString()).toLower();
    s.hostName = self.value(u"HostName"_s).toString();
    QList<QHostAddress> v6;
    for (const QJsonValue &v : self.value(u"TailscaleIPs"_s).toArray()) {
        const QHostAddress a = addressOf(v.toString());
        if (a.isNull())
            continue;
        (a.protocol() == QAbstractSocket::IPv4Protocol ? s.addresses : v6).append(a);
    }
    s.addresses += v6;
    s.userId = idOf(self.value(u"UserID"_s));
    const QJsonObject users = o->value(u"User"_s).toObject();
    const QJsonObject user = users.value(QString::number(s.userId)).toObject();
    s.login = user.value(u"LoginName"_s).toString();
    s.displayName = user.value(u"DisplayName"_s).toString();
    s.tailnet = o->value(u"CurrentTailnet"_s).toObject().value(u"Name"_s).toString();
    return s;
}

std::optional<Identity> parseWhois(QByteArrayView json, QString *error)
{
    const std::optional<QJsonObject> o = objectOf(json, error);
    if (!o)
        return std::nullopt;
    const QJsonObject node = o->value(u"Node"_s).toObject();
    const QJsonObject profile = o->value(u"UserProfile"_s).toObject();
    if (node.isEmpty() || profile.isEmpty()) {
        if (error)
            *error = u"no Node or UserProfile in tailscaled's whois"_s;
        return std::nullopt;
    }
    Identity id;
    id.login = profile.value(u"LoginName"_s).toString();
    id.displayName = profile.value(u"DisplayName"_s).toString();
    id.userId = idOf(profile.value(u"ID"_s));
    id.nodeDnsName = withoutTrailingDot(node.value(u"Name"_s).toString()).toLower();
    id.node = node.value(u"ComputedName"_s).toString();
    if (id.node.isEmpty())
        id.node = id.nodeDnsName.section(u'.', 0, 0);
    for (const QJsonValue &t : node.value(u"Tags"_s).toArray()) {
        if (const QString tag = t.toString(); !tag.isEmpty())
            id.tags.append(tag);
    }
    return id;
}

bool isTailnetAddress(const QHostAddress &address)
{
    const QHostAddress a = plain(address);
    if (a.protocol() == QAbstractSocket::IPv4Protocol)
        return a.isInSubnet(QHostAddress(u"100.64.0.0"_s), 10);
    if (a.protocol() == QAbstractSocket::IPv6Protocol)
        return a.isInSubnet(QHostAddress(u"fd7a:115c:a1e0::"_s), 48);
    return false;
}

bool isTailnetName(const QString &host)
{
    const QString h = withoutTrailingDot(host.trimmed()).toLower();
    if (!h.endsWith(u".ts.net"_s))
        return false;
    // "<device>.<tailnet>.ts.net": two labels before ts.net, none empty, only a host name's letters.
    const QStringList labels = h.split(u'.');
    if (labels.size() < 4)
        return false;
    for (const QString &label : labels) {
        if (label.isEmpty() || label.size() > 63 || label.startsWith(u'-') || label.endsWith(u'-'))
            return false;
        for (const QChar c : label) {
            if (!((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-'))
                return false;
        }
    }
    return true;
}

bool isTailnetHost(const QString &host)
{
    QString h = host.trimmed();
    if (h.startsWith(u'[') && h.endsWith(u']'))
        h = h.mid(1, h.size() - 2);
    const QHostAddress address(h);
    if (!address.isNull())
        return isTailnetAddress(address);
    return isTailnetName(h);
}

bool sameLogin(const QString &a, const QString &b)
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

TrustPlan planTrust(const PeerFacts &facts)
{
    TrustPlan plan;
    const QHostAddress peer = plain(facts.peer);
    const bool loopback = peer.isLoopback();
    plan.askWhois = loopback || isTailnetAddress(peer);
    plan.serveHeader = loopback && facts.listensOnLoopbackOnly && facts.behindServe && facts.hasServeHeader
                       && !facts.serveLogin.trimmed().isEmpty();
    plan.headerIgnored = facts.hasServeHeader && !plan.serveHeader;
    return plan;
}

Judgement judge(const Identity &identity, const QStringList &allowedLogins, const QString &thisNode)
{
    Judgement j;
    j.login = identity.login;
    j.node = identity.node;
    const QString self = withoutTrailingDot(thisNode.trimmed()).toLower();
    if (!self.isEmpty() && identity.nodeDnsName == self) {
        j.why = u"a connection from this computer itself (any of its users could have made it)"_s;
        return j;
    }
    if (!identity.tags.isEmpty()) {
        j.why = u"a tagged device (%1) belongs to no user"_s.arg(identity.tags.join(u", "_s));
        return j;
    }
    if (identity.login.trimmed().isEmpty() || sameLogin(identity.login, TaggedLogin)) {
        j.why = u"a device of no known user"_s;
        return j;
    }
    for (const QString &allowed : allowedLogins) {
        if (sameLogin(identity.login.trimmed(), allowed.trimmed())) {
            j.trusted = true;
            j.why = u"a device of %1 in the tailnet"_s.arg(identity.login);
            return j;
        }
    }
    j.why = u"a device of another user (%1)"_s.arg(identity.login);
    return j;
}

Judgement decide(const PeerFacts &facts, const std::optional<Identity> &whois, const QStringList &allowedLogins,
                 const QString &thisNode)
{
    const TrustPlan plan = planTrust(facts);
    if (plan.askWhois && whois)
        return judge(*whois, allowedLogins, thisNode);
    if (plan.serveHeader) {
        Identity fromHeader;
        fromHeader.login = facts.serveLogin.trimmed();
        Judgement j = judge(fromHeader, allowedLogins);
        if (j.trusted)
            j.why = u"a device of %1 through Tailscale Serve"_s.arg(fromHeader.login);
        return j;
    }
    Judgement j;
    j.why = plan.askWhois ? u"tailscaled does not know who is at %1"_s.arg(plain(facts.peer).toString())
                          : u"%1 is no address of the tailnet"_s.arg(plain(facts.peer).toString());
    return j;
}

} // namespace brulion::tailscale
