#include "io/exporter.h"

#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/htmlrender_p.h"
#include "io/zip.h"
#include "util/paths.h"

#include <QSet>

#include <vector>

namespace brulion::io {

using namespace Qt::StringLiterals;
using namespace detail;

namespace {

struct Registry {
    std::vector<std::unique_ptr<Exporter>> owned;
    QList<const Exporter *> list;

    Registry()
    {
        for (auto *make : {makeMarkdownZipExporter, makeObsidianZipExporter, makeLogseqZipExporter,
                           makeHtmlSiteExporter, makeHtmlSingleExporter, makePandocExporter, makeJsonExporter,
                           makeIcsExporter, makeOpmlExporter, makeTasksCsvExporter}) {
            owned.push_back(make());
            list.append(owned.back().get());
        }
    }
};

const Registry &registry()
{
    static const Registry r;
    return r;
}

} // namespace

const QList<const Exporter *> &exporters()
{
    return registry().list;
}

const Exporter *exporterById(QStringView id)
{
    for (const Exporter *e : exporters()) {
        if (e->id() == id)
            return e;
    }
    return nullptr;
}

namespace {

ExportInput cleanInput(const ExportInput &input)
{
    ExportInput clean = input;
    clean.files.clear();
    QSet<QString> seen;
    for (const FileEntry &f : input.files) {
        const QString path = paths::normalize(sanitizeArchivePath(f.path));
        if (path.isEmpty() || seen.contains(path))
            continue;
        const bool config = path == u".brulion/config.json";
        if (!config && (paths::isIgnored(path) || paths::isConflictCopy(path)))
            continue;
        seen.insert(path);
        clean.files.append(FileEntry{path, f.data});
    }
    return clean;
}

} // namespace

QStringList webPictureTargets(const ExportInput &input)
{
    ExportInput clean = cleanInput(input);
    clean.webPictures.clear();
    const ExportVault vault(clean);
    QStringList out;
    QSet<QString> seen;
    // The HTML renderer asks for every picture from the internet it shows (the Pandoc one sees
    // the same links): nothing is rendered for real.
    HtmlLinks links;
    links.webPicture = [&](const QString &url) -> std::optional<QString> {
        if (!seen.contains(url)) {
            seen.insert(url);
            out.append(url);
        }
        return std::nullopt;
    };
    const HtmlRenderer renderer(links);
    for (const ExportVault::Note &n : vault.notes()) {
        if (n.parsed)
            renderer.renderNote(*n.parsed);
    }
    return out;
}

ExportResult runExport(const Exporter &exporter, const ExportInput &input)
{
    return exporter.run(cleanInput(input));
}

} // namespace brulion::io
