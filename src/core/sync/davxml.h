// WebDAV XML: the PROPFIND request body and a tolerant 207 Multi-Status parser (any namespace
// prefix or none, several propstat blocks, missing properties, entity-encoded ETags), plus the
// HTTP date parser used for <getlastmodified>.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::sync {

/// One <response> of a multistatus document.
struct DavResource {
    QString href;              ///< exactly as sent by the server (still percent-encoded)
    bool isCollection = false;
    QString etag;              ///< normalised (no quotes, no W/); empty if missing
    qint64 mtime = 0;          ///< ms since epoch; 0 if missing or unparsable
    qint64 size = 0;           ///< bytes; 0 if missing or invalid
    QString fileId;            ///< oc:fileid (Nextcloud, ownCloud), trimmed; empty if missing

    friend bool operator==(const DavResource &, const DavResource &) = default;
};

struct MultistatusResult {
    QList<DavResource> resources;
    QString error; ///< non-empty when the document is not a WebDAV multistatus

    bool ok() const { return error.isEmpty(); }
};

/// PROPFIND body asking for getetag, getlastmodified, getcontentlength, resourcetype and
/// oc:fileid (a server that does not know it answers it in a 404 propstat, which is dropped).
QByteArray propfindBody();

/// Parse a 207 Multi-Status body. Responses with a non-2xx status or without <href> are
/// skipped; only properties of successful <propstat> blocks count. Element names are matched
/// by local name, so "d:", "D:", "lp1:" or no prefix all work. Malformed XML or a document
/// whose root is not <multistatus> (an HTML login page) gives an error.
MultistatusResult parseMultistatus(QByteArrayView xml);

/// HTTP status code of a status line ("HTTP/1.1 404 Not Found" -> 404).
std::optional<int> parseStatusLine(QStringView line);

/// HTTP date (RFC 9110 IMF-fixdate "Tue, 14 Oct 2026 08:00:00 GMT", obsolete RFC 850 and
/// asctime forms, numeric offsets) or ISO 8601, as ms since epoch; 0 when unparsable.
qint64 parseHttpDate(QStringView text);

} // namespace brulion::sync
