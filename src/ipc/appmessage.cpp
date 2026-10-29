#include "appmessage.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::ipc {

QByteArray encodeMessage(const QByteArray &verb, const QJsonObject &payload)
{
    if (payload.isEmpty())
        return verb;
    return verb + ' ' + QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

std::optional<AppMessage> parseMessage(const QByteArray &rawLine)
{
    const QByteArray line = rawLine.trimmed();
    if (line.isEmpty())
        return std::nullopt;
    const qsizetype space = line.indexOf(' ');
    AppMessage out;
    out.verb = space < 0 ? line : line.left(space);
    for (const char c : std::as_const(out.verb)) {
        if (!((c >= 'a' && c <= 'z') || c == '-'))
            return std::nullopt;
    }
    if (space >= 0) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line.mid(space + 1).trimmed(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return std::nullopt;
        out.payload = doc.object();
    }
    return out;
}

QByteArray openMessage(const QJsonObject &route, const QString &vaultRoot)
{
    return encodeMessage("open", QJsonObject{{u"route"_s, route}, {u"vault"_s, vaultRoot}});
}

namespace {

QJsonObject withToken(QJsonObject payload, const QByteArray &activationToken)
{
    if (!activationToken.isEmpty())
        payload.insert(u"activationToken"_s, QString::fromLatin1(activationToken));
    return payload;
}

} // namespace

QByteArray shareMessage(const QJsonObject &share, const QByteArray &activationToken)
{
    return encodeMessage("share", withToken(QJsonObject{{u"share"_s, share}}, activationToken));
}

QByteArray launchMessage(const QByteArray &verb, const QByteArray &activationToken)
{
    return encodeMessage(verb, withToken({}, activationToken));
}

} // namespace brulion::ipc
