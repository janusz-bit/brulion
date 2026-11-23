#include "publish/site.h"

#include "github/gitobjects.h"
#include "index/vaultindex.h"
#include "io/exporter.h"
#include "publish/messages.h"
#include "util/paths.h"

#include <QCryptographicHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

const QString kConfigPath = u".brulion/config.json"_s;

SiteFile siteFile(const QString &path, QByteArray data)
{
    SiteFile f;
    f.path = path;
    f.sha256 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    f.gitSha = github::blobSha(data);
    f.data = std::move(data);
    return f;
}

} // namespace

const SiteFile *SiteBuild::file(const QString &path) const
{
    const auto it = std::ranges::find(files, path, &SiteFile::path);
    return it == files.cend() ? nullptr : &*it;
}

SiteBuild buildSite(const SiteInput &input)
{
    SiteBuild out;
    io::ExportInput exportInput;
    exportInput.config = input.config;
    exportInput.vaultName = input.title;
    exportInput.now = input.now;
    exportInput.footer = input.footer.isEmpty() ? publishedFooter() : input.footer;
    exportInput.calcLocale = input.calcLocale;
    for (const io::FileEntry &f : input.files) {
        if (f.path == kConfigPath) {
            exportInput.files.append(f);
            continue;
        }
        if (isHiddenPath(f.path) || isEncryptedFile(f.path) || paths::isConflictCopy(f.path))
            continue;
        if (!paths::isMarkdown(f.path)) {
            exportInput.files.append(f);
            ++out.attachments;
            continue;
        }
        const StrippedText stripped = stripEncrypted(QString::fromUtf8(f.data));
        if (stripped.wholePage) {
            out.encryptedPages.append(f.path);
            continue;
        }
        if (stripped.removed > 0) {
            out.fragmentsRemoved += stripped.removed;
            out.pagesWithFragmentsRemoved.append(f.path);
            exportInput.files.append(io::FileEntry{f.path, stripped.text.toUtf8()});
        } else {
            exportInput.files.append(f);
        }
        ++out.pages;
    }
    QList<io::FileEntry> files;
    if (input.content == SiteContent::Document) {
        if (const io::Exporter *single = io::exporterById(u"html-single"))
            for (const io::ExportResult result = io::runExport(*single, exportInput);
                 const io::ExportedFile &f : result.files)
                files.append(io::FileEntry{u"index.html"_s, f.data});
    } else {
        io::HtmlSiteOptions options;
        options.search = true;
        files = io::htmlSiteFiles(exportInput, options);
    }
    if (input.nojekyll)
        files.append(io::FileEntry{u".nojekyll"_s, QByteArray()});
    QSet<QString> seen;
    for (io::FileEntry &f : files) {
        if (f.path.isEmpty() || seen.contains(f.path))
            continue;
        seen.insert(f.path);
        out.bytes += f.data.size();
        out.files.append(siteFile(f.path, std::move(f.data)));
    }
    std::ranges::sort(out.files, {}, &SiteFile::path);
    return out;
}

QList<VaultPath> filesToRead(const PublishSelection &selection)
{
    QList<VaultPath> out = selection.pages;
    out += selection.attachments;
    out.append(kConfigPath);
    return out;
}

SiteInput siteInputFor(const PublishTarget &target, QList<io::FileEntry> files, const VaultConfig &config,
                       const QString &notebookName, const QDateTime &now)
{
    SiteInput input;
    input.files = std::move(files);
    input.config = config;
    input.title = target.title.trimmed().isEmpty() ? notebookName : target.title.trimmed();
    input.now = now;
    input.content = target.content;
    input.nojekyll = target.kind == TargetKind::GitHubPages && target.content == SiteContent::Site;
    return input;
}

PreparedSite prepareSite(const VaultIndex &index, const PublishTarget &target, const ReadFile &read,
                         const QString &notebookName, const QDateTime &now)
{
    PreparedSite out;
    out.selection = selectFiles(index, target.scope);
    QList<io::FileEntry> files;
    for (const QList<VaultPath> wanted = filesToRead(out.selection); const VaultPath &path : wanted) {
        const std::optional<QByteArray> data = read ? read(path) : std::nullopt;
        if (!data) {
            if (path != kConfigPath)
                out.unreadable.append(path);
            continue;
        }
        files.append(io::FileEntry{path, *data});
    }
    out.input = siteInputFor(target, std::move(files), index.config(), notebookName, now);
    return out;
}

} // namespace brulion::publish
