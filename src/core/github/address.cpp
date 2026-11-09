#include "github/address.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::github {

namespace {

/// First path segments of github.com that are pages of the site, not accounts.
const QSet<QString> &reservedOwners()
{
    static const QSet<QString> names = {
            u"about"_s,       u"apps"_s,    u"codespaces"_s,    u"collections"_s, u"customer-stories"_s,
            u"enterprise"_s,  u"explore"_s, u"features"_s,      u"issues"_s,      u"login"_s,
            u"marketplace"_s, u"new"_s,     u"notifications"_s, u"orgs"_s,        u"organizations"_s,
            u"pricing"_s,     u"pulls"_s,   u"readme"_s,        u"search"_s,      u"security"_s,
            u"settings"_s,    u"site"_s,    u"sponsors"_s,      u"team"_s,        u"topics"_s,
            u"trending"_s,
    };
    return names;
}

bool isAsciiControl(QChar c)
{
    return c.unicode() < 0x20 || c.unicode() == 0x7f;
}

/// An ASCII letter or digit, "-" or "_": what account and repository names are made of.
bool isNameChar(QChar c)
{
    return (c.unicode() < 0x80 && c.isLetterOrNumber()) || c == u'-' || c == u'_';
}

QUrl githubUrl(const QString &encodedPath, const QString &encodedQuery = {})
{
    QString text = u"https://"_s + WebHost + u'/' + encodedPath;
    if (!encodedQuery.isEmpty())
        text += u'?' + encodedQuery;
    return QUrl::fromEncoded(text.toLatin1(), QUrl::StrictMode);
}

} // namespace

std::optional<RepoAddress> parseRepoAddress(const QString &input)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return std::nullopt;

    QString host;
    QString path;
    static const QRegularExpression scp(u"^[A-Za-z0-9._-]+@([A-Za-z0-9.-]+):(?!//)(.*)$"_s);
    if (const QRegularExpressionMatch m = scp.match(text); m.hasMatch() && !text.contains(u"://"_s)) {
        host = m.captured(1).toLower();
        path = m.captured(2);
    } else if (text.contains(u"://"_s)) {
        const QUrl url(text, QUrl::TolerantMode);
        if (!url.isValid())
            return std::nullopt;
        const QString scheme = url.scheme().toLower();
        if (scheme != u"https"_s && scheme != u"http"_s && scheme != u"ssh"_s && scheme != u"git"_s)
            return std::nullopt;
        host = url.host().toLower();
        path = url.path(QUrl::FullyEncoded);
    } else {
        const qsizetype slash = text.indexOf(u'/');
        const QString first = (slash < 0 ? text : text.left(slash)).toLower();
        if (first == WebHost || first == u"www.github.com"_s || first == RawHost) {
            host = first;
            path = slash < 0 ? QString() : text.mid(slash + 1);
        } else {
            host = WebHost.toString();
            path = text;
        }
        // Typed, not a URL: the query and fragment of a pasted address go.
        static const QRegularExpression queryOrFragment(u"[?#]"_s);
        if (const qsizetype cut = path.indexOf(queryOrFragment); cut >= 0)
            path.truncate(cut);
    }
    if (host == u"www.github.com"_s)
        host = WebHost.toString();
    const bool raw = host == RawHost;
    if (host != WebHost && !raw)
        return std::nullopt;

    QStringList segments = path.split(u'/', Qt::SkipEmptyParts);
    if (segments.size() < 2)
        return std::nullopt;
    for (QString &segment : segments)
        segment = QUrl::fromPercentEncoding(segment.toUtf8());
    RepoAddress address;
    address.owner = segments.at(0);
    address.name = segments.at(1);
    if (address.name.endsWith(u".git"_s, Qt::CaseInsensitive))
        address.name.chop(4);
    if (!isValidOwner(address.owner) || !isValidRepoName(address.name)
        || reservedOwners().contains(address.owner.toLower())) {
        return std::nullopt;
    }
    if (raw) {
        address.refPath = segments.mid(2);
        address.file = !address.refPath.isEmpty();
    } else if (segments.size() >= 3 && (segments.at(2) == u"tree"_s || segments.at(2) == u"blob"_s)) {
        address.refPath = segments.mid(3);
        address.file = segments.at(2) == u"blob"_s && !address.refPath.isEmpty();
    }
    return address;
}

bool isValidOwner(QStringView owner)
{
    if (owner.isEmpty() || owner.size() > 39 || owner.startsWith(u'-'))
        return false;
    return std::ranges::all_of(owner, isNameChar);
}

bool isValidRepoName(QStringView name)
{
    if (name.isEmpty() || name.size() > 100 || name == u"." || name == u"..")
        return false;
    return std::ranges::all_of(name, [](QChar c) { return isNameChar(c) || c == u'.'; });
}

bool isValidBranchName(QStringView branch)
{
    if (branch.isEmpty() || branch == u"@" || branch.startsWith(u'/') || branch.startsWith(u'-')
        || branch.endsWith(u'/') || branch.endsWith(u'.') || branch.contains(u"..") || branch.contains(u"@{")
        || branch.contains(u"//")) {
        return false;
    }
    const auto forbidden = [](QChar c) { return isAsciiControl(c) || QStringView(u" ~^:?*[\\").contains(c); };
    const auto hidden = [](QStringView segment) { return segment.startsWith(u'.') || segment.endsWith(u".lock"); };
    return std::ranges::none_of(branch, forbidden) && std::ranges::none_of(branch.tokenize(u'/'), hidden);
}

std::optional<QString> normalizeFolder(const QString &folder)
{
    const QStringList segments =
            folder.trimmed().normalized(QString::NormalizationForm_C).split(u'/', Qt::SkipEmptyParts);
    const bool invalid = std::ranges::any_of(segments, [](const QString &segment) {
        return segment == u"."_s || segment == u".."_s || segment.contains(u'\\')
               || std::ranges::any_of(segment, isAsciiControl);
    });
    if (invalid)
        return std::nullopt;
    return segments.join(u'/');
}

std::pair<QString, QString> splitRefPath(const QStringList &refPath, const QStringList &branches, bool file)
{
    if (refPath.isEmpty())
        return {};
    qsizetype best = 0;
    for (const QString &branch : branches) {
        const QStringList parts = branch.split(u'/');
        if (parts.size() > refPath.size() || parts.size() <= best)
            continue;
        if (std::equal(parts.cbegin(), parts.cend(), refPath.cbegin()))
            best = parts.size();
    }
    if (best == 0)
        best = 1;
    QStringList rest = refPath.mid(best);
    if (file && !rest.isEmpty())
        rest.removeLast();
    return {refPath.mid(0, best).join(u'/'), rest.join(u'/')};
}

QString fullName(const QString &owner, const QString &name)
{
    return owner + u'/' + name;
}

QString encodePath(const QString &path)
{
    QStringList segments = path.split(u'/');
    for (QString &segment : segments)
        segment = QString::fromLatin1(QUrl::toPercentEncoding(segment));
    return segments.join(u'/');
}

QUrl webUrl(const QString &owner, const QString &name, const QString &branch, const QString &folder)
{
    QString path = encodePath(owner) + u'/' + encodePath(name);
    if (!branch.isEmpty()) {
        path += u"/tree/"_s + encodePath(branch);
        if (!folder.isEmpty())
            path += u'/' + encodePath(folder);
    }
    return githubUrl(path);
}

QUrl commitUrl(const QString &owner, const QString &name, const QString &sha)
{
    return githubUrl(encodePath(owner) + u'/' + encodePath(name) + u"/commit/"_s + encodePath(sha));
}

QUrl fileUrl(const QString &owner, const QString &name, const QString &branch, const QString &path)
{
    return githubUrl(encodePath(owner) + u'/' + encodePath(name) + u"/blob/"_s + encodePath(branch) + u'/'
                     + encodePath(path));
}

QUrl newTokenUrl(bool publicOnly)
{
    return githubUrl(u"settings/tokens/new"_s,
                     u"description=Brulion&scopes="_s + (publicOnly ? u"public_repo"_s : u"repo"_s));
}

} // namespace brulion::github
