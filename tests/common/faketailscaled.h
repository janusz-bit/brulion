// A tailscaled for tests: its LocalAPI's two read-only answers over a real local socket
// (QLocalServer, a file in a temporary folder) — GET /localapi/v0/status and GET
// /localapi/v0/whois?addr=<ip:port> — as tailscaled 1.104 gives them, Host "local-tailscaled.sock"
// required (another: 403, as tailscaled answers). Used by tests/core/tailscale and tests/cli
// (`acp serve --tailscale` against it through BRULION_TAILSCALE_SOCKET or LocalApi::setSocketPath).
//
//   FakeTailscaled ts(dir.filePath(u"ts.sock"_s));
//   ts.status = FakeTailscaled::statusJson(u"nixos.tail1ea20e.ts.net"_s, {u"100.108.227.25"_s}, u"ala@github"_s);
//   ts.whois.insert(u"100.98.93.11"_s, FakeTailscaled::whoisJson(u"ala@github"_s, u"pixel-9-pro"_s));
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

class FakeTailscaled : public QObject {
public:
    explicit FakeTailscaled(const QString &path, QObject *parent = nullptr) : QObject(parent)
    {
        QLocalServer::removeServer(path);
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        m_ok = m_server.listen(path);
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *s = m_server.nextPendingConnection())
                serve(s);
        });
    }
    bool listening() const { return m_ok; }
    QString path() const { return m_server.fullServerName(); }

    /// The status's body ("" answers 500).
    QByteArray status;
    /// whois: by "ip:port", else by "ip" — a body (an address none answers 404 "no match").
    QHash<QString, QByteArray> whois;
    /// Never answer (a tailscaled that hangs).
    bool silent = false;
    /// Answer this instead of HTTP.
    QByteArray garbage;
    /// The requests' targets ("/localapi/v0/whois?addr=…") and Host fields, in order.
    QStringList targets;
    QStringList hosts;

    static QByteArray statusJson(const QString &dnsName, const QStringList &ips, const QString &login,
                                 qint64 userId = 6448733692706832, const QString &backend = QStringLiteral("Running"))
    {
        QJsonArray addresses;
        for (const QString &ip : ips)
            addresses.append(ip);
        const QJsonObject self{{QStringLiteral("ID"), QStringLiteral("nRXvjhnHbg11CNTRL")},
                               {QStringLiteral("HostName"), dnsName.section(u'.', 0, 0)},
                               {QStringLiteral("DNSName"), QString(dnsName + u'.')},
                               {QStringLiteral("TailscaleIPs"), addresses},
                               {QStringLiteral("UserID"), double(userId)},
                               {QStringLiteral("OS"), QStringLiteral("linux")}};
        const QJsonObject users{
                {QString::number(userId),
                 QJsonObject{{QStringLiteral("ID"), double(userId)},
                             {QStringLiteral("LoginName"), login},
                             {QStringLiteral("DisplayName"), QStringLiteral("Ala Żółć")}}},
                {QStringLiteral("8031273801053428"),
                 QJsonObject{{QStringLiteral("ID"), 8031273801053428.0},
                             {QStringLiteral("LoginName"), QStringLiteral("tagged-devices")},
                             {QStringLiteral("DisplayName"), QStringLiteral("Tagged Devices")}}}};
        const QJsonObject o{{QStringLiteral("Version"), QStringLiteral("1.104.6")},
                            {QStringLiteral("BackendState"), backend},
                            {QStringLiteral("TailscaleIPs"), addresses},
                            {QStringLiteral("Self"), self},
                            {QStringLiteral("MagicDNSSuffix"), dnsName.section(u'.', 1)},
                            {QStringLiteral("CurrentTailnet"),
                             QJsonObject{{QStringLiteral("Name"), QStringLiteral("ala.github")},
                                         {QStringLiteral("MagicDNSSuffix"), dnsName.section(u'.', 1)},
                                         {QStringLiteral("MagicDNSEnabled"), true}}},
                            {QStringLiteral("User"), users},
                            {QStringLiteral("Peer"), QJsonObject()}};
        return QJsonDocument(o).toJson();
    }

    static QByteArray whoisJson(const QString &login, const QString &node, const QStringList &tags = {},
                                qint64 userId = 6448733692706832)
    {
        QJsonObject n{{QStringLiteral("ID"), 2928916477164116.0},
                      {QStringLiteral("Name"), QString(node + QStringLiteral(".tail1ea20e.ts.net."))},
                      {QStringLiteral("ComputedName"), node},
                      {QStringLiteral("User"), double(userId)},
                      {QStringLiteral("Addresses"), QJsonArray{QStringLiteral("100.98.93.11/32")}}};
        n.insert(QStringLiteral("Tags"), tags.isEmpty() ? QJsonValue() : QJsonValue(QJsonArray::fromStringList(tags)));
        const QJsonObject o{{QStringLiteral("Node"), n},
                            {QStringLiteral("UserProfile"),
                             QJsonObject{{QStringLiteral("ID"), double(userId)},
                                         {QStringLiteral("LoginName"), login},
                                         {QStringLiteral("DisplayName"), QStringLiteral("Ala Żółć")}}},
                            {QStringLiteral("CapMap"), QJsonObject()}};
        return QJsonDocument(o).toJson();
    }

private:
    void serve(QLocalSocket *s)
    {
        auto *buffer = new QByteArray;
        connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        connect(s, &QObject::destroyed, s, [buffer] { delete buffer; });
        connect(s, &QLocalSocket::readyRead, s, [this, s, buffer] {
            *buffer += s->readAll();
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
            const QString target = QString::fromUtf8(request.value(1));
            QString host;
            for (const QByteArray &line : lines) {
                if (line.toLower().startsWith("host:"))
                    host = QString::fromUtf8(line.mid(5).trimmed());
            }
            targets.append(target);
            hosts.append(host);
            if (silent)
                return;
            if (!garbage.isEmpty()) {
                s->write(garbage);
                s->disconnectFromServer();
                return;
            }
            int code = 404;
            QByteArray body = "not found\n";
            if (host != u"local-tailscaled.sock") {
                code = 403;
                body = "invalid localapi request\n";
            } else if (target == u"/localapi/v0/status") {
                code = status.isEmpty() ? 500 : 200;
                body = status.isEmpty() ? QByteArray("no status\n") : status;
            } else if (target.startsWith(u"/localapi/v0/whois?")) {
                const QString addr = QUrlQuery(target.section(u'?', 1)).queryItemValue(QStringLiteral("addr"),
                                                                                       QUrl::FullyDecoded);
                QString ip = addr.section(u':', 0, -2);
                if (ip.startsWith(u'['))
                    ip = ip.mid(1, ip.size() - 2);
                if (whois.contains(addr)) {
                    code = 200;
                    body = whois.value(addr);
                } else if (whois.contains(ip)) {
                    code = 200;
                    body = whois.value(ip);
                } else {
                    body = "no match for IP:port\n";
                }
            }
            const QByteArray reason = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
            s->write("HTTP/1.0 " + QByteArray::number(code) + ' ' + reason
                     + "\r\nContent-Type: application/json\r\nTailscale-Version: 1.104.6\r\nContent-Length: "
                     + QByteArray::number(body.size()) + "\r\n\r\n" + body);
            s->disconnectFromServer();
        });
    }

    QLocalServer m_server;
    bool m_ok = false;
};
