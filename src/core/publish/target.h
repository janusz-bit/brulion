// Publishing targets (docs/UI.md "Publishing"): where a site goes — GitHub Pages, a WebDAV folder,
// Nextcloud with a public link, a folder on this device — what it holds (PublishScope) and how the
// last publish went. Plain data with JSON for the store (no secrets: passwords and tokens are kept
// in owner-only files, publish/store.h). Part of the publish module — see publish/publish.h.
#pragma once

#include "publish/selection.h"

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::publish {

enum class TargetKind { GitHubPages, WebDav, Nextcloud, Folder };

/// "github" | "webdav" | "nextcloud" | "folder".
QString kindName(TargetKind kind);
std::optional<TargetKind> kindFromName(QStringView name);

/// What is uploaded: the static website (a page per note, index, tags, search) or one HTML
/// document holding every page (`index.html`, the pictures inside).
enum class SiteContent { Site, Document };
QString contentName(SiteContent content); ///< "site" | "document"
SiteContent contentFromName(QStringView name);

/// How the last publish (or unpublish) of a target went.
struct PublishStatus {
    QDateTime at;      ///< local time; invalid: never
    QString operation; ///< "publish" | "unpublish"
    bool ok = false;
    QString url; ///< where the site is
    int pages = 0;
    int uploaded = 0; ///< new and changed files
    int deleted = 0;
    int unchanged = 0;
    int conflicts = 0;
    QString commit;    ///< GitHub: the commit made
    QString errorCode; ///< "" when it went well (publish/messages.h: errorText())
    QString error;     ///< the server's own words, when it gave any
    bool isValid() const { return at.isValid(); }
};

struct PublishTarget {
    QString id;   ///< stable, file-name safe ("t" + 12 hex digits: newTargetId())
    QString name; ///< the user's name for it ("Blog", "Przepisy dla rodziny")
    TargetKind kind = TargetKind::GitHubPages;

    // GitHub Pages
    QString repository;                          ///< "owner/name"
    QString branch = QStringLiteral("gh-pages"); ///< made (without history) when missing
    /// The folder in the branch ("" its root, "docs"). GitHub Pages serves a branch's root or its
    /// `docs` folder: another folder is published, but Pages is not turned on for it.
    QString folder;

    // WebDAV
    QString url; ///< the folder's WebDAV address ("https://example.com/dav/strona")

    // Nextcloud (the folder: `folder`, in the user's files — "Publiczne/Przepisy")
    QString server;             ///< "cloud.example.com", "https://example.com/nextcloud"
    bool share = true;          ///< a public link to the folder
    QDate shareExpires;         ///< the link's expiry date (invalid: none)
    bool sharePassword = false; ///< the link asks for a password (the secret "share")

    // WebDAV, Nextcloud
    QString username;

    // A folder on this device
    QString path;

    /// The address people open, when known beforehand (WebDAV, a folder served by a web server;
    /// GitHub: a custom domain — else GitHub's own). Nextcloud: the public link once made.
    QString siteUrl;
    /// The site's title ("" = the notebook's name).
    QString title;
    SiteContent content = SiteContent::Site;
    PublishScope scope;
    PublishStatus last;
};

QJsonObject toJson(const PublishTarget &target);
PublishTarget targetFromJson(const QJsonObject &object);
QJsonObject toJson(const PublishStatus &status);
PublishStatus statusFromJson(const QJsonObject &object);

/// A new target id: "t" and 12 random hex digits.
QString newTargetId();

/// What keeps the target from being used, as a code ("" = none): "no-name", "bad-repository",
/// "bad-branch", "bad-folder", "bad-url", "bad-server", "no-username", "no-folder", "no-path".
QString checkTarget(const PublishTarget &target);

/// Where the target publishes, as one string — a manifest made for another place does not apply:
/// "github:owner/name@gh-pages:docs", "webdav:https://…/strona", "nextcloud:https://…/remote.php/
/// dav/files/jan/Publiczne/Przepisy", "folder:/home/jan/www".
QString identity(const PublishTarget &target);

/// The place in a few words: "owner/name · gh-pages · docs", "example.com/dav/strona",
/// "cloud.example.com · Publiczne/Przepisy", "/home/jan/www".
QString describe(const PublishTarget &target);

/// The WebDAV folder a target writes to (WebDAV: `url`; Nextcloud: the user's `folder` on
/// `server`); "" for the others.
QString davUrl(const PublishTarget &target);

/// GitHub: the folder a Pages site serves for the target's folder ("/" for "", "/docs" for
/// "docs"); "" when Pages cannot serve it.
QString pagesPath(const PublishTarget &target);

/// GitHub's address of a repository's Pages site: https://<owner>.github.io/<name>/ (the root for
/// a repository named <owner>.github.io).
QString defaultPagesUrl(const QString &repository);

/// The address of the target's site after a publish: `siteUrl` when given; GitHub: `pagesUrl`
/// (what Pages reported, else defaultPagesUrl()) with the part of the target's folder below the
/// folder Pages serves; Nextcloud: `shareUrl`; a folder: its index.html as a file URL.
QString siteAddress(const PublishTarget &target, const QString &pagesUrl = {}, const QString &pagesFolder = {},
                    const QString &shareUrl = {});

} // namespace brulion::publish
