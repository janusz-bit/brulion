// The website a target publishes, built from the chosen notes (publish/selection.h) by the io
// module's HTML exports: the static site (io::htmlSiteFiles: a page per note, the index, tag
// pages, backlinks among the published pages only, search) or one HTML document; every file with
// its hashes for the incremental publish. Part of the publish module — see publish/publish.h.
#pragma once

#include "calc/calclocale.h"
#include "io/files.h"
#include "publish/protect.h"
#include "publish/selection.h"
#include "publish/target.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::publish {

/// One file of the site.
struct SiteFile {
    QString path; ///< from the site's root: "index.html", "Projekty/Plan.html", "assets/kot.jpg"
    QByteArray data;
    QString sha256; ///< hex
    QString gitSha; ///< Git's blob id (github::blobSha): what a GitHub tree lists
};

struct SiteInput {
    /// The chosen pages and attachments with their bytes (vault paths), and
    /// `.brulion/config.json` when the notebook has one (its folders and formats).
    QList<io::FileEntry> files;
    VaultConfig config;
    /// The site's title (the notebook's name, or the target's title).
    QString title;
    /// The time the charts' queries count from ("today"); never written into the pages.
    QDateTime now;
    SiteContent content = SiteContent::Site;
    /// `.nojekyll` at the root (GitHub Pages then serves files and folders starting with "_").
    bool nojekyll = false;
    /// The pages' footer ("" = "Published with Brulion").
    QString footer;
    /// Password-protected pages (publish/protect.h): the site without its search, then every page
    /// and file encrypted. `protection.password` is the caller's to fill (the secret "pages").
    bool protect = false;
    SiteProtection protection;
    /// The language of inline calculations' results (io::ExportInput::calcLocale): the UI's.
    calc::CalcLocale calcLocale = calc::CalcLocale::Polish;
};

struct SiteBuild {
    QList<SiteFile> files; ///< sorted by path
    int pages = 0;
    int attachments = 0;
    qint64 bytes = 0;
    /// Encrypted fragments removed (stripEncrypted()) and the pages they were in.
    int fragmentsRemoved = 0;
    QList<VaultPath> pagesWithFragmentsRemoved;
    /// Pages left out while building: encrypted as a whole (their text, read now, said so).
    QList<VaultPath> encryptedPages;
    /// The pages are password-protected (their files hold only ciphertext).
    bool protectedPages = false;
    /// "" — built; "no-page-password": protection without a password (no files then).
    QString errorCode;

    const SiteFile *file(const QString &path) const;
};

/// Build the site of `input` (pure; any thread): encrypted fragments removed from every page, a
/// page encrypted as a whole left out, macros of extensions kept as written (they are computed
/// over the whole notebook, private pages too), pictures from the internet as their addresses;
/// with `protect` encrypted with the password (protectSite()).
SiteBuild buildSite(const SiteInput &input);

/// Reads a notebook file's bytes (nullopt when it cannot be read).
using ReadFile = std::function<std::optional<QByteArray>(const VaultPath &)>;

/// What publishing a target of a notebook needs, prepared in one go (the command line; the app
/// reads the files in chunks itself): its selection and the SiteInput with the files read.
struct PreparedSite {
    PublishSelection selection;
    SiteInput input;
    QStringList unreadable; ///< files that could not be read (left out)
};
PreparedSite prepareSite(const VaultIndex &index, const PublishTarget &target, const ReadFile &read,
                         const QString &notebookName, const QDateTime &now);

/// The SiteInput of `target` for the read `files` (no reading); a protected target's salt, not its
/// password.
SiteInput siteInputFor(const PublishTarget &target, QList<io::FileEntry> files, const VaultConfig &config,
                       const QString &notebookName, const QDateTime &now);

/// The files of a selection to read: its pages and attachments (and `.brulion/config.json`).
QList<VaultPath> filesToRead(const PublishSelection &selection);

} // namespace brulion::publish
