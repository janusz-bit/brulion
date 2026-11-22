#include "acp/hostoffer.h"

#include "tailscale/tailscale.h"

#include <QJsonArray>
#include <QUrl>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::acp {

namespace {

/// A name as it may be shown: one line, no control characters, 80 at most.
QString shownName(const QString &given)
{
    QString out;
    for (const QChar c : given) {
        if (c.isPrint() || c.isSpace())
            out += c;
    }
    return out.simplified().left(80);
}

bool validId(const QString &id)
{
    if (id.isEmpty() || id.size() > 40)
        return false;
    for (const QChar c : id) {
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'
              || c == u'_'))
            return false;
    }
    return true;
}

QString hostOf(const QString &url)
{
    return QUrl(url).host().toLower();
}

std::optional<HostOffer> offerOf(const QJsonObject &o)
{
    const QString typed = o.value(u"url"_s).toString();
    // (Written by an app: with its scheme. One without is no address of ours.)
    if (!typed.contains(u"://"_s))
        return std::nullopt;
    const QUrl base = normalizeRemoteBase(typed);
    if (!base.isValid() || base.isEmpty() || !tailscale::isTailnetHost(base.host()))
        return std::nullopt;
    HostOffer offer;
    offer.url = base.toString();
    offer.host = shownName(o.value(u"host"_s).toString());
    if (offer.host.isEmpty())
        offer.host = base.host().section(u'.', 0, 0);
    for (const QJsonValue &v : o.value(u"agents"_s).toArray()) {
        const QJsonObject a = v.toObject();
        const QString id = a.value(u"id"_s).toString();
        if (!validId(id) || std::ranges::any_of(offer.agents, [&id](const RemoteAgent &r) { return r.id == id; }))
            continue;
        QString name = shownName(a.value(u"name"_s).toString());
        if (name.isEmpty())
            name = id;
        offer.agents.append({id, name, QString()});
        if (offer.agents.size() >= MaxOfferedAgents)
            break;
    }
    if (offer.agents.isEmpty())
        return std::nullopt;
    return offer;
}

QJsonObject toJson(const HostOffer &offer)
{
    QJsonArray agents;
    for (const RemoteAgent &a : offer.agents)
        agents.append(QJsonObject{{u"id"_s, a.id}, {u"name"_s, a.name}});
    return QJsonObject{{u"url"_s, offer.url}, {u"host"_s, offer.host}, {u"agents"_s, agents}};
}

} // namespace

bool sameHost(const QString &a, const QString &b)
{
    const QString ha = hostOf(a);
    return !ha.isEmpty() && ha == hostOf(b);
}

QList<HostOffer> hostOffers(const QJsonObject &config)
{
    QList<HostOffer> out;
    for (const QJsonValue &v : config.value(HostOffersKey).toArray()) {
        const std::optional<HostOffer> offer = offerOf(v.toObject());
        if (!offer || std::ranges::any_of(out, [&offer](const HostOffer &o) { return sameHost(o.url, offer->url); }))
            continue;
        out.append(*offer);
        if (out.size() >= MaxHostOffers)
            break;
    }
    return out;
}

QJsonObject withHostOffer(const QJsonObject &config, const HostOffer &offer)
{
    QJsonArray list{toJson(offer)};
    for (const QJsonValue &v : config.value(HostOffersKey).toArray()) {
        if (!sameHost(v.toObject().value(u"url"_s).toString(), offer.url))
            list.append(v);
    }
    QJsonObject out = config;
    out.insert(HostOffersKey, list);
    return out;
}

QJsonObject withoutHostOffer(const QJsonObject &config, const QString &url)
{
    QJsonArray list;
    for (const QJsonValue &v : config.value(HostOffersKey).toArray()) {
        if (!sameHost(v.toObject().value(u"url"_s).toString(), url))
            list.append(v);
    }
    QJsonObject out = config;
    if (list.isEmpty())
        out.remove(HostOffersKey);
    else
        out.insert(HostOffersKey, list);
    return out;
}

} // namespace brulion::acp
