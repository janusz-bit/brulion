// URL, header and encoding helpers for WebDAV (all pure).
#pragma once

#include "types.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace brulion::sync {

/// Percent-encode every segment of a vault path, dropping empty segments:
/// "Notatki/Zażółć gęślą.md" -> "Notatki/Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85.md".
/// Everything except the RFC 3986 unreserved characters is encoded.
QString encodePath(const QString &path);

/// Decode one percent-encoded URL path segment (UTF-8). A segment with a malformed escape
/// ("100%", "%zz") or with bytes that are not UTF-8 is returned as written.
QString decodeSegment(const QString &segment);

/// Remove trailing slashes: "https://h/a//" -> "https://h/a".
QString trimTrailingSlashes(const QString &s);

/// WebDAV URL of a Nextcloud user folder:
/// nextcloudDavUrl("cloud.example.com/", "jan", "Notatki/Brulion") ==
/// "https://cloud.example.com/remote.php/dav/files/jan/Notatki/Brulion".
/// The server may be given with or without a scheme (default https), with a sub-path
/// ("https://example.com/nextcloud"), or as a full DAV / web UI URL pasted from the browser
/// (the "/remote.php/…", "/index.php/…", "/apps/…" and "/login" parts are dropped). User name
/// and folder segments are percent-encoded; the folder is normalised ("./a/../b" -> "b"). User
/// info typed into the address ("https://jan:secret@host") is dropped.
QString nextcloudDavUrl(const QString &server, const QString &user, const QString &folder);

/// Decoded path of a URL or absolute path: "https://h/a%20b/%C5%BC.md?x=1" -> "/a b/ż.md".
/// Query and fragment are dropped; dot segments of full URLs are resolved.
QString urlPathname(const QString &url);

/// Decoded path of a multistatus <href>, which may be a full URL, an absolute path or a path
/// relative to `requestUrl` (the collection that was listed).
QString hrefPathname(const QString &href, const QString &requestUrl);

/// `pathname` relative to `basePathname` (both decoded), compared segment by segment: NFC
/// (NFD names written by macOS clients match), with a case-insensitive fallback for proxies
/// that change the case of the prefix. The remaining segments keep the server's spelling.
/// std::nullopt when it lies outside the base or contains "." / ".." segments.
std::optional<QString> relativeTo(const QString &pathname, const QString &basePathname);

/// Base URL + encoded vault path; `dir` appends a slash. joinUrl("https://h/dav/", "a b.md")
/// == "https://h/dav/a%20b.md".
QString joinUrl(const QString &baseUrl, const VaultPath &path, bool dir = false);

/// ETag for storage and comparison: trimmed, without "W/" and without surrounding quotes
/// (also entity-encoded ones as some servers send in XML).
QString normalizeEtag(const QString &etag);

/// ETag in the quoted form If-Match needs: "abc" -> "\"abc\"".
QString quoteEtag(const QString &etag);

/// Authorization header value for HTTP Basic auth; user name and password are UTF-8.
QByteArray basicAuth(const QString &username, const QString &password);

} // namespace brulion::sync
