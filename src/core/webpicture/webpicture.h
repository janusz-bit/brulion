// Pictures from the internet in notes (docs/SYNTAX.md §2 "Pictures from the internet",
// docs/ARCHITECTURE.md "Data safety"): `![alt](https://example.com/photo.jpg)`.
//
// The QML engine never loads from the network (src/app/ui/localnetwork.h). A picture from the
// internet is fetched by the app itself (PictureFetcher: http(s) only, a size limit, a time limit,
// the bytes checked to be a picture by their signature) into a cache on the device
// (PictureCache), and the editor shows the cached file — when the user allowed it, once or for
// every picture. "Pobierz do notatnika" stores the same bytes as an attachment of the notebook.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>
#include <QUrl>

#include <functional>

namespace brulion::webpicture {

/// Largest picture fetched (a bigger one fails with FetchError::TooLarge).
inline constexpr qint64 MaxBytes = 20 * 1024 * 1024;
/// A fetch without progress for this long fails (FetchError::Timeout).
inline constexpr int TimeoutMs = 30 * 1000;

/// `target` (a Markdown picture's destination as written) is an http: or https: URL with a host —
/// a picture from the internet. Other schemes (ftp:, data:, file:, javascript:) are not.
bool isWebUrl(QStringView target);
/// The URL of a web target (isWebUrl()), invalid otherwise.
QUrl webUrl(QStringView target);

/// Tells whether a host is on this device or its private network (isPrivateHost(); tests swap
/// it to treat 127.0.0.1 as a host of the internet).
using HostCheck = std::function<bool(const QString &host)>;

/// `host` (a URL's host as QUrl gives it) is this device or a private network: the IP addresses
/// that are loopback, private-use (10/8, 172.16/12, 192.168/16, fc00::/7), shared (100.64/10:
/// carrier NAT, Tailscale), link-local (169.254/16, fe80::/10), site-local (fec0::/10) or
/// unspecified (0.0.0.0/8, ::), also IPv4-mapped; and the names of a home network by their form:
/// one label ("localhost", "nas") or ending with .localhost, .local, .lan, .home, .home.arpa,
/// .internal, .intranet, .corp, .private, .localdomain or .fritz.box. Other names are not
/// resolved: "nas.example" is not private, whatever the DNS says. Such a host is never fetched
/// without asking, and no redirect leads to it from another host.
bool isPrivateHost(const QString &host);
/// A redirect of a picture request first sent to `from` may be followed to `to`: http(s), never
/// from https to http, never from a host of the internet to a private one (`isPrivate`). A
/// private `from` (a picture the user asked for on a home server) may lead to any host.
bool redirectAllowed(const QUrl &from, const QUrl &to, const HostCheck &isPrivate = isPrivateHost);

/// The picture format of `data` by its signature — "png", "jpg", "gif", "webp", "bmp", "ico",
/// "avif", "svg" — or "" when the bytes are no picture (an HTML error page, a login form).
QString sniffFormat(const QByteArray &data);

/// The attachment's file name for a picture from `url` in `format` (sniffFormat()): the URL
/// path's last segment, percent-decoded and made file-name-safe, with the format's extension
/// (an existing picture extension of the same format is kept as written: ".jpeg", ".JPG");
/// "<host> picture.<ext>" when the path names no file.
QString attachmentName(const QUrl &url, const QString &format);

/// How the UI names a picture from `url`: its host and the path's last segment, decoded
/// ("example.com › zdjęcie.jpg"); just the host when the path names no file.
QString displayName(const QUrl &url);

} // namespace brulion::webpicture
