#include "io/exporter.h"

#include "calc/calc.h"
#include "dataview/dataview.h"
#include "index/vaultindex.h"
#include "markdown/inlineexpr.h"
#include "markdown/macros.h"

#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/htmlrender_p.h"
#include "io/zip.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>
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
        for (auto *make :
             {makeMarkdownZipExporter, makeObsidianZipExporter, makeLogseqZipExporter, makeHtmlSiteExporter,
              makeHtmlSingleExporter, makePdfExporter, makeDocxExporter, makePandocExporter, makeJsonExporter,
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
    const auto it = std::ranges::find_if(exporters(), [id](const Exporter *e) { return e->id() == id; });
    return it == exporters().cend() ? nullptr : *it;
}

ExportInput detail::cleanExportInput(const ExportInput &input)
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

QStringList webPictureTargets(const ExportInput &input)
{
    ExportInput clean = cleanExportInput(input);
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
    links.diagramsAsSource = true; // (no diagram has pictures: none laid out for nothing)
    const HtmlRenderer renderer(links);
    for (const ExportVault::Note &n : vault.notes()) {
        if (n.parsed)
            renderer.renderNote(*n.parsed);
    }
    return out;
}

namespace {

/// The files of a document export with each inline calculation and inline query in a note replaced
/// by its value (docs/SYNTAX.md §9 "Inline calculations", §10 "Inline queries"): the calculator
/// first (the variables above it on its page), then Dataview over the exported notes; one with no
/// value or an error stays as written.
void putInlineValues(ExportInput &input)
{
    QList<qsizetype> withExpressions;
    for (qsizetype i = 0; i < input.files.size(); ++i) {
        const FileEntry &f = input.files.at(i);
        if (paths::isMarkdown(f.path) && f.data.contains("`") && f.data.contains("="))
            withExpressions.append(i);
    }
    if (withExpressions.isEmpty())
        return;
    // (Built once, only for notes that have some: the notes and the other files, as the export reads them.)
    std::optional<VaultIndex> index;
    const auto vaultIndex = [&]() -> const VaultIndex & {
        if (!index) {
            index.emplace(input.config);
            VaultIndex::Batch batch(*index);
            for (const FileEntry &f : std::as_const(input.files)) {
                if (paths::isIgnored(f.path) || paths::isConflictCopy(f.path))
                    continue;
                const bool text = paths::isMarkdown(f.path) || VaultIndex::readsText(f.path);
                index->update(f.path, text ? QString::fromUtf8(f.data) : QString());
            }
        }
        return *index;
    };
    const calc::CalcLocale locale = input.calcLocale;
    for (const qsizetype i : std::as_const(withExpressions)) {
        FileEntry &f = input.files[i];
        const QString content = QString::fromUtf8(f.data);
        if (markdown::findInlineExpressions(content).isEmpty())
            continue;
        f.data = markdown::replaceInlineExpressions(content, [&](const markdown::InlineExpression &e)
                                                                     -> std::optional<QString> {
                     const calc::CalcLineResult r =
                             calc::evaluateInline(e.expression, calc::pageScope(content, e.line, locale), locale);
                     if (r.hasValue())
                         return markdown::escapedMarkdownText(r.value);
                     if (!input.now.isValid()) // (exporters never read the clock: no `date(today)`)
                         return std::nullopt;
                     dataview::Options options;
                     options.currentPath = f.path;
                     options.now = input.now;
                     options.weekStart = input.config.weekStartsOn;
                     const auto value = dataview::evaluateInline(e.expression, vaultIndex(), options);
                     if (!value || value->kind != dataview::InlineValue::Kind::Value)
                         return std::nullopt;
                     return value->markdown;
                 }).toUtf8();
    }
}

} // namespace

ExportInput detail::prepareExportInput(const QString &exporterId, const ExportInput &input)
{
    ExportInput clean = cleanExportInput(input);
    // The documents show what a macro of an extension showed in the app; the Markdown exports and
    // the data ones keep the macro as written (docs/SYNTAX.md §13 "Macros").
    static const QStringList documents{u"html-site"_s, u"html-single"_s, u"pdf"_s, u"docx"_s, u"pandoc"_s};
    if (!input.macroTexts.isEmpty() && documents.contains(exporterId)) {
        for (FileEntry &f : clean.files) {
            const auto texts = input.macroTexts.constFind(f.path);
            if (texts == input.macroTexts.constEnd() || texts->isEmpty() || !paths::isMarkdown(f.path))
                continue;
            const QString content = QString::fromUtf8(f.data);
            f.data = markdown::replaceMacros(content, [&](const markdown::MacroSpan &m) -> std::optional<QString> {
                         const auto text = texts->constFind(content.sliced(m.from, m.to - m.from));
                         return text == texts->constEnd() ? std::nullopt : std::optional<QString>(*text);
                     }).toUtf8();
        }
    }
    if (documents.contains(exporterId))
        putInlineValues(clean);
    return clean;
}

ExportResult runExport(const Exporter &exporter, const ExportInput &input)
{
    return exporter.run(prepareExportInput(exporter.id(), input));
}

QList<FileEntry> htmlSiteFiles(const ExportInput &input, const HtmlSiteOptions &options)
{
    return htmlSiteEntries(prepareExportInput(u"html-site"_s, input), options);
}

} // namespace brulion::io
