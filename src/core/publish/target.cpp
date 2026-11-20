#include "publish/target.h"

#include "github/address.h"
#include "sync/url.h"
#include "util/paths.h"

#include <QDir>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

QJsonObject scopeToJson(const PublishScope &scope)
{
    QJsonArray items;
    for (const PublishItem &item : scope.items)
        items.append(QJsonObject{{u"kind"_s, itemKindName(item.kind)}, {u"value"_s, item.value}});
    QJsonObject out{{u"items"_s, items}};
    if (scope.linkedPages)
        out.insert(u"linkedPages"_s, true);
    if (scope.onlyMarked)
        out.insert(u"onlyMarked"_s, true);
    if (scope.extensionPages)
        out.insert(u"extensionPages"_s, true);
    if (scope.aiConversations)
        out.insert(u"aiConversations"_s, true);
    return out;
}

PublishScope scopeFromJson(const QJsonObject &object)
{
    PublishScope scope;
    const QJsonArray items = object.value(u"items"_s).toArray();
    for (const QJsonValue v : items) {
        const QJsonObject o = v.toObject();
        if (const auto kind = itemKindFromName(o.value(u"kind"_s).toString()))
            scope.add(PublishItem{*kind, o.value(u"value"_s).toString()});
    }
    scope.linkedPages = object.value(u"linkedPages"_s).toBool();
    scope.onlyMarked = object.value(u"onlyMarked"_s).toBool();
    scope.extensionPages = object.value(u"extensionPages"_s).toBool();
    scope.aiConversations = object.value(u"aiConversations"_s).toBool();
    return scope;
}

void putString(QJsonObject &o, const QString &key, const QString &value)
{
    if (!value.isEmpty())
        o.insert(key, value);
}

bool isHttpUrl(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    return url.isValid() && (url.scheme() == u"https" || url.scheme() == u"http") && !url.host().isEmpty();
}

/// A URL for show: scheme and user info removed, no trailing slash.
QString shortUrl(const QString &text)
{
    const QUrl url(text.trimmed());
    if (!url.isValid() || url.host().isEmpty())
        return text.trimmed();
    QString out = url.host() + url.path();
    while (out.endsWith(u'/'))
        out.chop(1);
    return out;
}

} // namespace

QString kindName(TargetKind kind)
{
    switch (kind) {
    case TargetKind::GitHubPages: return u"github"_s;
    case TargetKind::WebDav: return u"webdav"_s;
    case TargetKind::Nextcloud: return u"nextcloud"_s;
    case TargetKind::Folder: return u"folder"_s;
    }
    return u"github"_s;
}

std::optional<TargetKind> kindFromName(QStringView name)
{
    if (name == u"github")
        return TargetKind::GitHubPages;
    if (name == u"webdav")
        return TargetKind::WebDav;
    if (name == u"nextcloud")
        return TargetKind::Nextcloud;
    if (name == u"folder")
        return TargetKind::Folder;
    return std::nullopt;
}

QString contentName(SiteContent content)
{
    return content == SiteContent::Document ? u"document"_s : u"site"_s;
}

SiteContent contentFromName(QStringView name)
{
    return name == u"document" ? SiteContent::Document : SiteContent::Site;
}

QJsonObject toJson(const PublishStatus &status)
{
    QJsonObject o{{u"at"_s, status.at.toString(Qt::ISODate)}, {u"operation"_s, status.operation}, {u"ok"_s, status.ok}};
    putString(o, u"url"_s, status.url);
    o.insert(u"pages"_s, status.pages);
    o.insert(u"uploaded"_s, status.uploaded);
    o.insert(u"deleted"_s, status.deleted);
    o.insert(u"unchanged"_s, status.unchanged);
    if (status.conflicts > 0)
        o.insert(u"conflicts"_s, status.conflicts);
    putString(o, u"commit"_s, status.commit);
    putString(o, u"errorCode"_s, status.errorCode);
    putString(o, u"error"_s, status.error);
    return o;
}

PublishStatus statusFromJson(const QJsonObject &o)
{
    PublishStatus s;
    s.at = QDateTime::fromString(o.value(u"at"_s).toString(), Qt::ISODate);
    s.operation = o.value(u"operation"_s).toString();
    s.ok = o.value(u"ok"_s).toBool();
    s.url = o.value(u"url"_s).toString();
    s.pages = o.value(u"pages"_s).toInt();
    s.uploaded = o.value(u"uploaded"_s).toInt();
    s.deleted = o.value(u"deleted"_s).toInt();
    s.unchanged = o.value(u"unchanged"_s).toInt();
    s.conflicts = o.value(u"conflicts"_s).toInt();
    s.commit = o.value(u"commit"_s).toString();
    s.errorCode = o.value(u"errorCode"_s).toString();
    s.error = o.value(u"error"_s).toString();
    return s;
}

QJsonObject toJson(const PublishTarget &t)
{
    QJsonObject o{{u"id"_s, t.id}, {u"name"_s, t.name}, {u"kind"_s, kindName(t.kind)}};
    switch (t.kind) {
    case TargetKind::GitHubPages:
        o.insert(u"repository"_s, t.repository);
        o.insert(u"branch"_s, t.branch);
        putString(o, u"folder"_s, t.folder);
        break;
    case TargetKind::WebDav:
        o.insert(u"url"_s, t.url);
        putString(o, u"username"_s, t.username);
        break;
    case TargetKind::Nextcloud:
        o.insert(u"server"_s, t.server);
        o.insert(u"username"_s, t.username);
        o.insert(u"folder"_s, t.folder);
        o.insert(u"share"_s, t.share);
        if (t.shareExpires.isValid())
            o.insert(u"shareExpires"_s, t.shareExpires.toString(Qt::ISODate));
        if (t.sharePassword)
            o.insert(u"sharePassword"_s, true);
        break;
    case TargetKind::Folder: o.insert(u"path"_s, t.path); break;
    }
    putString(o, u"siteUrl"_s, t.siteUrl);
    putString(o, u"title"_s, t.title);
    o.insert(u"content"_s, contentName(t.content));
    o.insert(u"scope"_s, scopeToJson(t.scope));
    if (t.last.isValid())
        o.insert(u"last"_s, toJson(t.last));
    return o;
}

PublishTarget targetFromJson(const QJsonObject &o)
{
    PublishTarget t;
    t.id = o.value(u"id"_s).toString();
    t.name = o.value(u"name"_s).toString();
    t.kind = kindFromName(o.value(u"kind"_s).toString()).value_or(TargetKind::GitHubPages);
    t.repository = o.value(u"repository"_s).toString();
    t.branch = o.value(u"branch"_s).toString(t.branch);
    t.folder = o.value(u"folder"_s).toString();
    t.url = o.value(u"url"_s).toString();
    t.server = o.value(u"server"_s).toString();
    t.username = o.value(u"username"_s).toString();
    t.path = o.value(u"path"_s).toString();
    t.share = o.value(u"share"_s).toBool(true);
    t.shareExpires = QDate::fromString(o.value(u"shareExpires"_s).toString(), Qt::ISODate);
    t.sharePassword = o.value(u"sharePassword"_s).toBool();
    t.siteUrl = o.value(u"siteUrl"_s).toString();
    t.title = o.value(u"title"_s).toString();
    t.content = contentFromName(o.value(u"content"_s).toString());
    t.scope = scopeFromJson(o.value(u"scope"_s).toObject());
    if (o.value(u"last"_s).isObject())
        t.last = statusFromJson(o.value(u"last"_s).toObject());
    return t;
}

QString newTargetId()
{
    QString id = u"t"_s;
    for (int i = 0; i < 12; ++i)
        id += QString::number(QRandomGenerator::global()->bounded(16), 16);
    return id;
}

QString checkTarget(const PublishTarget &t)
{
    if (t.name.trimmed().isEmpty())
        return u"no-name"_s;
    switch (t.kind) {
    case TargetKind::GitHubPages: {
        const QStringList parts = t.repository.trimmed().split(u'/');
        if (parts.size() != 2 || !github::isValidOwner(parts.at(0)) || !github::isValidRepoName(parts.at(1)))
            return u"bad-repository"_s;
        if (!github::isValidBranchName(t.branch.trimmed()))
            return u"bad-branch"_s;
        if (!github::normalizeFolder(t.folder))
            return u"bad-folder"_s;
        break;
    }
    case TargetKind::WebDav:
        if (!isHttpUrl(t.url))
            return u"bad-url"_s;
        break;
    case TargetKind::Nextcloud: {
        if (t.server.trimmed().isEmpty() || !isHttpUrl(sync::nextcloudDavUrl(t.server, u"x"_s, {})))
            return u"bad-server"_s;
        if (t.username.trimmed().isEmpty())
            return u"no-username"_s;
        const QString folder = paths::normalize(t.folder);
        // A public link to the user's whole folder would publish every file of the account.
        if (folder.isEmpty())
            return u"no-folder"_s;
        if (!github::normalizeFolder(folder))
            return u"bad-folder"_s;
        break;
    }
    case TargetKind::Folder:
        if (t.path.trimmed().isEmpty() || !QDir::isAbsolutePath(t.path.trimmed()))
            return u"no-path"_s;
        break;
    }
    if (!t.siteUrl.trimmed().isEmpty() && !isHttpUrl(t.siteUrl))
        return u"bad-url"_s;
    return {};
}

QString davUrl(const PublishTarget &t)
{
    switch (t.kind) {
    case TargetKind::WebDav: return sync::trimTrailingSlashes(t.url.trimmed());
    case TargetKind::Nextcloud:
        return sync::nextcloudDavUrl(t.server.trimmed(), t.username.trimmed(), paths::normalize(t.folder));
    case TargetKind::GitHubPages:
    case TargetKind::Folder: break;
    }
    return {};
}

QString identity(const PublishTarget &t)
{
    switch (t.kind) {
    case TargetKind::GitHubPages:
        return u"github:"_s + t.repository.trimmed().toLower() + u'@' + t.branch.trimmed() + u':'
               + github::normalizeFolder(t.folder).value_or(t.folder);
    case TargetKind::WebDav: return u"webdav:"_s + davUrl(t);
    case TargetKind::Nextcloud: return u"nextcloud:"_s + davUrl(t);
    case TargetKind::Folder: return u"folder:"_s + QDir::cleanPath(t.path.trimmed());
    }
    return {};
}

QString describe(const PublishTarget &t)
{
    switch (t.kind) {
    case TargetKind::GitHubPages: {
        QString out = t.repository.trimmed() + u" · "_s + t.branch.trimmed();
        if (const QString folder = github::normalizeFolder(t.folder).value_or(QString()); !folder.isEmpty())
            out += u" · "_s + folder;
        return out;
    }
    case TargetKind::WebDav: return shortUrl(t.url);
    case TargetKind::Nextcloud:
        return shortUrl(sync::nextcloudDavUrl(t.server, u"x"_s, {})).section(u'/', 0, 0) + u" · "_s
               + paths::normalize(t.folder);
    case TargetKind::Folder: return QDir::toNativeSeparators(QDir::cleanPath(t.path.trimmed()));
    }
    return {};
}

QString pagesPath(const PublishTarget &t)
{
    if (t.kind != TargetKind::GitHubPages)
        return {};
    const QString folder = github::normalizeFolder(t.folder).value_or(u"-"_s);
    if (folder.isEmpty())
        return u"/"_s;
    if (folder == u"docs")
        return u"/docs"_s;
    return {};
}

QString defaultPagesUrl(const QString &repository)
{
    const QString owner = repository.section(u'/', 0, 0).trimmed().toLower();
    const QString name = repository.section(u'/', 1, 1).trimmed();
    if (owner.isEmpty() || name.isEmpty())
        return {};
    const QString host = owner + u".github.io"_s;
    if (name.toLower() == host)
        return u"https://"_s + host + u'/';
    return u"https://"_s + host + u'/' + name + u'/';
}

QString siteAddress(const PublishTarget &t, const QString &pagesUrl, const QString &pagesFolder,
                    const QString &shareUrl)
{
    if (!t.siteUrl.trimmed().isEmpty())
        return t.siteUrl.trimmed();
    switch (t.kind) {
    case TargetKind::GitHubPages: {
        QString base = pagesUrl.isEmpty() ? defaultPagesUrl(t.repository) : pagesUrl;
        if (base.isEmpty())
            return {};
        if (!base.endsWith(u'/'))
            base += u'/';
        // The target's folder below the one Pages serves ("/" serving "blog" → "blog/").
        const QString folder = github::normalizeFolder(t.folder).value_or(QString());
        // Unknown: Pages serves the folder it can (the root, docs/), else the branch's root.
        QString served = pagesFolder.isEmpty() ? pagesPath(t) : pagesFolder;
        while (served.startsWith(u'/'))
            served.remove(0, 1);
        if (folder.isEmpty() || folder == served)
            return base;
        const QString below = served.isEmpty()
                                      ? folder
                                      : (folder.startsWith(served + u'/') ? folder.mid(served.size() + 1) : QString());
        return below.isEmpty() ? base : base + github::encodePath(below) + u'/';
    }
    case TargetKind::Nextcloud: return shareUrl;
    case TargetKind::Folder:
        return QUrl::fromLocalFile(QDir::cleanPath(t.path.trimmed()) + u"/index.html"_s).toString();
    case TargetKind::WebDav: break;
    }
    return {};
}

} // namespace brulion::publish
