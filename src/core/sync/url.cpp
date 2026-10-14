#include "sync/url.h"

#include "sync/util_p.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

const QRegularExpression &schemeRe()
{
    static const QRegularExpression re(u"^[a-z][a-z0-9+.-]*://"_s, QRegularExpression::CaseInsensitiveOption);
    return re;
}

bool hasScheme(const QString &s)
{
    return schemeRe().match(s).hasMatch();
}

bool isHexDigit(QChar c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
}

/// Every '%' starts a complete two-digit escape.
bool hasValidEscapes(const QString &s)
{
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (s.at(i) != u'%')
            continue;
        if (i + 2 >= s.size() || !isHexDigit(s.at(i + 1)) || !isHexDigit(s.at(i + 2)))
            return false;
        i += 2;
    }
    return true;
}

QString decodeSegments(const QString &path)
{
    QStringList parts = path.split(u'/');
    for (QString &part : parts)
        part = decodeSegment(part);
    return parts.join(u'/');
}

QString stripQueryAndFragment(QString s)
{
    static const QRegularExpression re(u"[?#].*$"_s, QRegularExpression::DotMatchesEverythingOption);
    return s.remove(re);
}

QStringList nonEmptySegments(const QString &path)
{
    return path.split(u'/', Qt::SkipEmptyParts);
}

} // namespace

QString encodePath(const QString &path)
{
    QStringList out;
    for (const QString &segment : nonEmptySegments(path))
        out.append(QString::fromLatin1(QUrl::toPercentEncoding(segment)));
    return out.join(u'/');
}

QString decodeSegment(const QString &segment)
{
    if (!segment.contains(u'%') || !hasValidEscapes(segment))
        return segment;
    const QByteArray bytes = QByteArray::fromPercentEncoding(segment.toUtf8());
    return detail::decodeUtf8Strict(bytes, false).value_or(segment);
}

QString trimTrailingSlashes(const QString &s)
{
    qsizetype end = s.size();
    while (end > 0 && s.at(end - 1) == u'/')
        --end;
    return s.left(end);
}

QString nextcloudDavUrl(const QString &server, const QString &user, const QString &folder)
{
    static const QRegularExpression leadingSlashes(u"^/+"_s);
    static const QRegularExpression entryPoint(u"/(?:remote\\.php|index\\.php)(?:/.*)?$"_s,
                                               QRegularExpression::CaseInsensitiveOption
                                                   | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression webUiPage(u"/(?:apps/[^/]+|login)$"_s, QRegularExpression::CaseInsensitiveOption);

    QString s = server.trimmed();
    if (!hasScheme(s))
        s = u"https://"_s + s.remove(leadingSlashes);
    s = stripQueryAndFragment(s);
    s.remove(entryPoint);
    // Web UI pages pasted from the address bar: ".../apps/files/", ".../login".
    s = trimTrailingSlashes(s).remove(webUiPage);
    s = trimTrailingSlashes(s);
    const QString userPart = QString::fromLatin1(
        QUrl::toPercentEncoding(user.trimmed().normalized(QString::NormalizationForm_C)));
    const QString folderPart = encodePath(paths::normalize(folder.trimmed()));
    QString out = s + u"/remote.php/dav/files/"_s + userPart;
    if (!folderPart.isEmpty())
        out += u'/' + folderPart;
    return out;
}

QString urlPathname(const QString &url)
{
    QString pathname = url;
    if (hasScheme(url)) {
        const QUrl parsed(url, QUrl::TolerantMode);
        if (parsed.isValid()) {
            pathname = parsed.adjusted(QUrl::NormalizePathSegments).path(QUrl::FullyEncoded);
        } else {
            static const QRegularExpression hostPart(u"^[^/]*"_s);
            pathname = url;
            pathname.remove(schemeRe()).remove(hostPart);
        }
    }
    return decodeSegments(stripQueryAndFragment(pathname));
}

QString hrefPathname(const QString &href, const QString &requestUrl)
{
    const QString h = href.trimmed();
    if (hasScheme(h) || h.startsWith(u'/'))
        return urlPathname(h);
    static const QRegularExpression lastSegment(u"[^/]*$"_s);
    QString dir = urlPathname(requestUrl);
    dir.remove(lastSegment);
    return dir + decodeSegments(stripQueryAndFragment(h));
}

std::optional<QString> relativeTo(const QString &pathname, const QString &basePathname)
{
    const QStringList p = nonEmptySegments(pathname);
    const QStringList b = nonEmptySegments(basePathname);
    if (p.size() < b.size())
        return std::nullopt;
    const auto nfc = [](const QString &x) { return x.normalized(QString::NormalizationForm_C); };
    bool exact = true;
    bool folded = true;
    for (qsizetype i = 0; i < b.size(); ++i) {
        const QString pb = nfc(p.at(i));
        const QString bb = nfc(b.at(i));
        exact = exact && pb == bb;
        folded = folded && pb.toLower() == bb.toLower();
    }
    if (!exact && !folded)
        return std::nullopt;
    const QStringList rest = p.mid(b.size());
    for (const QString &segment : rest) {
        if (segment == u"."_s || segment == u".."_s)
            return std::nullopt;
    }
    return rest.join(u'/');
}

QString joinUrl(const QString &baseUrl, const VaultPath &path, bool dir)
{
    const QString base = trimTrailingSlashes(baseUrl);
    const QString encoded = encodePath(path);
    QString url = encoded.isEmpty() ? base : base + u'/' + encoded;
    if (dir)
        url += u'/';
    return url;
}

QString normalizeEtag(const QString &etag)
{
    QString s = etag.trimmed();
    if (s.startsWith(u"W/"_s, Qt::CaseInsensitive))
        s = s.mid(2);
    if (s.size() >= 2 && s.startsWith(u'"') && s.endsWith(u'"'))
        s = s.mid(1, s.size() - 2);
    if (s.size() >= 12 && s.startsWith(u"&quot;"_s) && s.endsWith(u"&quot;"_s))
        s = s.mid(6, s.size() - 12);
    return s;
}

QString quoteEtag(const QString &etag)
{
    return u'"' + normalizeEtag(etag) + u'"';
}

QByteArray basicAuth(const QString &username, const QString &password)
{
    const QString credentials = username + u':' + password;
    return "Basic " + credentials.toUtf8().toBase64();
}

} // namespace brulion::sync
