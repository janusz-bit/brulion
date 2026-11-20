#include "publish/manifest.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace brulion::publish {

QJsonObject toJson(const PublishManifest &m)
{
    QJsonObject files;
    for (auto it = m.files.cbegin(); it != m.files.cend(); ++it) {
        QJsonObject f{{u"sha256"_s, it->sha256}, {u"size"_s, it->size}};
        if (!it->remote.isEmpty())
            f.insert(u"remote"_s, it->remote);
        files.insert(it.key(), f);
    }
    QJsonObject o{{u"version"_s, 1}, {u"identity"_s, m.identity}, {u"files"_s, files}};
    if (m.publishedAt.isValid())
        o.insert(u"publishedAt"_s, m.publishedAt.toString(Qt::ISODate));
    const auto put = [&o](const QString &key, const QString &value) {
        if (!value.isEmpty())
            o.insert(key, value);
    };
    put(u"url"_s, m.url);
    put(u"commit"_s, m.commit);
    put(u"shareId"_s, m.shareId);
    put(u"shareUrl"_s, m.shareUrl);
    put(u"sharePasswordHash"_s, m.sharePasswordHash);
    if (m.shareExpires.isValid())
        o.insert(u"shareExpires"_s, m.shareExpires.toString(Qt::ISODate));
    return o;
}

PublishManifest manifestFromJson(const QJsonObject &o)
{
    PublishManifest m;
    if (o.value(u"version"_s).toInt() != 1)
        return m;
    m.identity = o.value(u"identity"_s).toString();
    m.publishedAt = QDateTime::fromString(o.value(u"publishedAt"_s).toString(), Qt::ISODate);
    m.url = o.value(u"url"_s).toString();
    m.commit = o.value(u"commit"_s).toString();
    m.shareId = o.value(u"shareId"_s).toString();
    m.shareUrl = o.value(u"shareUrl"_s).toString();
    m.sharePasswordHash = o.value(u"sharePasswordHash"_s).toString();
    m.shareExpires = QDate::fromString(o.value(u"shareExpires"_s).toString(), Qt::ISODate);
    const QJsonObject files = o.value(u"files"_s).toObject();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QJsonObject f = it.value().toObject();
        const QString sha = f.value(u"sha256"_s).toString();
        if (it.key().isEmpty() || sha.isEmpty())
            continue;
        m.files.insert(it.key(), ManifestFile{sha, f.value(u"size"_s).toInteger(), f.value(u"remote"_s).toString()});
    }
    return m;
}

} // namespace brulion::publish
