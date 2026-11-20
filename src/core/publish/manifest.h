// The manifest of a target: what its last publish put there, file by file, with hashes — what
// makes the next publish incremental (unchanged files are not sent again, files no longer in the
// site are removed, a file changed there since is a conflict). Kept per target in the device's
// store (publish/store.h). Part of the publish module — see publish/publish.h.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace brulion::publish {

struct ManifestFile {
    QString sha256; ///< of the content published
    qint64 size = 0;
    /// The version the target holds: GitHub — the blob id; WebDAV, Nextcloud — the ETag after the
    /// upload ("" when the server gave none); a folder — the SHA-256.
    QString remote;
    friend bool operator==(const ManifestFile &, const ManifestFile &) = default;
};

struct PublishManifest {
    /// publish::identity() of the place it describes: a manifest of another place is empty.
    QString identity;
    QDateTime publishedAt;
    QString url;
    QString commit; ///< GitHub: the last commit made
    // Nextcloud's public link
    QString shareId;
    QString shareUrl;
    /// SHA-256 of the link's password as last set ("" none): a changed password is set again.
    QString sharePasswordHash;
    QDate shareExpires;
    QMap<QString, ManifestFile> files; ///< by the site path

    bool isEmpty() const { return files.isEmpty() && shareId.isEmpty(); }
    friend bool operator==(const PublishManifest &, const PublishManifest &) = default;
};

QJsonObject toJson(const PublishManifest &manifest);
PublishManifest manifestFromJson(const QJsonObject &object);

} // namespace brulion::publish
