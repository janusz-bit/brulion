#include "documentpaths.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

/// The decoded document id of a document URI ("…/document/<id>", also "…/tree/<t>/document/<id>").
QString documentId(const QUrl &url)
{
    // Encoded: the id's own "/" stay %2F, so the segments are the URI's.
    const QStringList segments = url.path(QUrl::FullyEncoded).split(u'/');
    for (qsizetype i = 0; i + 1 < segments.size(); ++i) {
        if (segments.at(i) == u"document" && !segments.at(i + 1).isEmpty())
            return QUrl::fromPercentEncoding(segments.at(i + 1).toUtf8());
    }
    return {};
}

/// `base` + "/" + `relative`, cleaned; "" when the result is not inside `base` (a ".." segment).
QString inside(const QString &base, const QString &relative)
{
    if (base.isEmpty() || !base.startsWith(u'/'))
        return {};
    const QString root = QDir::cleanPath(base);
    if (relative.isEmpty())
        return root;
    if (relative.split(u'/').contains(u".."_s))
        return {};
    const QString path = QDir::cleanPath(root + u'/' + relative);
    return path.startsWith(root + u'/') ? path : QString();
}

} // namespace

QString localPathOfDocument(const QUrl &url, const QString &primaryStorage)
{
    if (url.scheme() != u"content" || url.host().isEmpty())
        return {};
    const QString id = documentId(url);
    if (id.isEmpty() || id.contains(QChar(0)))
        return {};
    const QString authority = url.host();
    if (authority == u"com.android.externalstorage.documents") {
        // "<volume>:<path in it>": "primary" the phone's storage, "home" its Documents folder, a
        // memory card by its id ("1234-ABCD").
        const qsizetype colon = id.indexOf(u':');
        if (colon <= 0)
            return {};
        const QString volume = id.left(colon);
        const QString relative = id.mid(colon + 1);
        if (volume == u"primary")
            return inside(primaryStorage, relative);
        if (volume == u"home")
            return primaryStorage.isEmpty() ? QString() : inside(primaryStorage + u"/Documents"_s, relative);
        static const QRegularExpression card(u"^[0-9A-Za-z][0-9A-Za-z-]*$"_s);
        return card.match(volume).hasMatch() ? inside(u"/storage/"_s + volume, relative) : QString();
    }
    if (authority == u"com.android.providers.downloads.documents" && id.startsWith(u"raw:/"_s)) {
        // "raw:<absolute path>" (a file the Downloads app knows by its path).
        const QString path = id.mid(4);
        return path.split(u'/').contains(u".."_s) ? QString() : QDir::cleanPath(path);
    }
    return {};
}

QString primaryStorageOf(const QString &path)
{
    static const QRegularExpression primary(u"^(/storage/emulated/[0-9]+)(?:/|$)"_s);
    const QRegularExpressionMatch m = primary.match(path);
    return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace brulion::platform
