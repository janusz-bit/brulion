#include "webpicture.h"

#include "util/paths.h"

#include <QHostAddress>

using namespace Qt::StringLiterals;

namespace brulion::webpicture {

namespace {

bool startsWith(const QByteArray &data, qsizetype at, QByteArrayView prefix)
{
    return data.size() >= at + prefix.size() && QByteArrayView(data).sliced(at, prefix.size()) == prefix;
}

/// An ISO base media file ("ftyp" box) whose major or a compatible brand is AVIF.
bool isAvif(const QByteArray &data)
{
    if (!startsWith(data, 4, "ftyp"))
        return false;
    const auto byte = [&](qsizetype i) { return quint32(quint8(data.at(i))); };
    const qsizetype boxSize = qsizetype(byte(0) << 24 | byte(1) << 16 | byte(2) << 8 | byte(3));
    const qsizetype end = std::min<qsizetype>(data.size(), std::max<qsizetype>(boxSize, 16));
    // Major brand at 8, minor version at 12, compatible brands from 16 on.
    for (qsizetype at = 8; at + 4 <= end; at += at == 8 ? 8 : 4) {
        const QByteArrayView brand = QByteArrayView(data).sliced(at, 4);
        if (brand == "avif" || brand == "avis")
            return true;
    }
    return false;
}

/// An SVG document: after a BOM and whitespace, an XML declaration, a comment, a doctype or the
/// root itself, and an `<svg` element in the first 4 KB before any `<html`.
bool isSvg(const QByteArray &data)
{
    QByteArray head = data.left(4096).toLower();
    if (head.startsWith("\xef\xbb\xbf"))
        head.remove(0, 3);
    head = head.trimmed();
    if (!head.startsWith("<?xml") && !head.startsWith("<svg") && !head.startsWith("<!--")
        && !head.startsWith("<!doctype svg"))
        return false;
    const qsizetype svg = head.indexOf("<svg");
    const qsizetype html = head.indexOf("<html");
    return svg >= 0 && (html < 0 || html > svg);
}

/// File extensions (lower case, with the dot) of a format; the first is written for a new name.
QStringList extensionsOf(const QString &format)
{
    if (format == u"jpg")
        return {u".jpg"_s, u".jpeg"_s, u".jpe"_s};
    return {u'.' + format};
}

} // namespace

bool isWebUrl(QStringView target)
{
    return webUrl(target).isValid();
}

QUrl webUrl(QStringView target)
{
    const QString text = target.trimmed().toString();
    if (!text.startsWith(u"http://", Qt::CaseInsensitive) && !text.startsWith(u"https://", Qt::CaseInsensitive))
        return {};
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid())
        url = QUrl(text, QUrl::TolerantMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != u"http" && scheme != u"https") || url.host().isEmpty())
        return {};
    return url;
}

bool isPrivateHost(const QString &host)
{
    QString name = host.trimmed().toLower();
    if (name.startsWith(u'[') && name.endsWith(u']'))
        name = name.mid(1, name.size() - 2);
    if (name.endsWith(u'.'))
        name.chop(1);
    if (name == u"localhost" || name.endsWith(u".localhost"))
        return true;
    QHostAddress address;
    if (name.isEmpty() || !address.setAddress(name))
        return false; // a name: not resolved here
    bool ip4 = false;
    const quint32 v4 = address.toIPv4Address(&ip4); // also of ::ffff:a.b.c.d
    if (ip4 && (v4 >> 24) == 0)
        return true; // 0.0.0.0/8, "this host"
    return address.isLoopback() || address.isPrivateUse() || address.isLinkLocal() || address.isUniqueLocalUnicast()
           || address == QHostAddress(QHostAddress::AnyIPv6);
}

bool redirectAllowed(const QUrl &from, const QUrl &to, const HostCheck &isPrivate)
{
    const QString scheme = to.scheme().toLower();
    if (!to.isValid() || to.host().isEmpty() || (scheme != u"http" && scheme != u"https"))
        return false;
    if (from.scheme().toLower() == u"https" && scheme != u"https")
        return false;
    return isPrivate(from.host()) || !isPrivate(to.host());
}

QString sniffFormat(const QByteArray &data)
{
    if (startsWith(data, 0, "\x89PNG\r\n\x1a\n"))
        return u"png"_s;
    if (startsWith(data, 0, "\xff\xd8\xff"))
        return u"jpg"_s;
    if (startsWith(data, 0, "GIF87a") || startsWith(data, 0, "GIF89a"))
        return u"gif"_s;
    if (startsWith(data, 0, "RIFF") && startsWith(data, 8, "WEBP"))
        return u"webp"_s;
    // BMP: "BM", the file size, two reserved words that are zero.
    if (startsWith(data, 0, "BM") && data.size() >= 26 && startsWith(data, 6, QByteArrayView("\0\0\0\0", 4)))
        return u"bmp"_s;
    // ICO: reserved 0, type 1, at least one picture.
    if (startsWith(data, 0, QByteArrayView("\0\0\1\0", 4)) && data.size() >= 22 && (data.at(4) != 0 || data.at(5) != 0))
        return u"ico"_s;
    if (isAvif(data))
        return u"avif"_s;
    if (isSvg(data))
        return u"svg"_s;
    return {};
}

QString attachmentName(const QUrl &url, const QString &format)
{
    const QStringList extensions = extensionsOf(format);
    QString path = url.path(QUrl::FullyDecoded);
    while (path.endsWith(u'/'))
        path.chop(1);
    QString name = path.mid(path.lastIndexOf(u'/') + 1).trimmed();
    if (!name.isEmpty()) {
        name = paths::sanitizeFileName(name);
        if (name == u"Untitled")
            name.clear();
    }
    if (name.isEmpty())
        name = paths::sanitizeFileName(url.host() + u" picture"_s);
    // The name's own extension: kept when it is the format's, else the format's is appended (a
    // ".php" or ".jpg" of a PNG would make other programs misread the file).
    const qsizetype dot = name.lastIndexOf(u'.');
    const QString ext = dot > 0 ? name.mid(dot).toLower() : QString();
    if (extensions.contains(ext))
        return name;
    if (dot > 0 && paths::pictureExtensions().contains(ext))
        name.truncate(dot);
    name = name.left(120).trimmed();
    return name + extensions.first();
}

QString displayName(const QUrl &url)
{
    QString path = url.path(QUrl::FullyDecoded);
    while (path.endsWith(u'/'))
        path.chop(1);
    const QString file = path.mid(path.lastIndexOf(u'/') + 1).trimmed();
    return file.isEmpty() ? url.host() : url.host() + u" › "_s + file;
}

} // namespace brulion::webpicture
