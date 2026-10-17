#include "io/importer.h"

#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "io/tar.h"
#include "io/zip.h"
#include "markdown/frontmatter.h"
#include "markdown/rename.h"
#include "text/text.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>

#include <memory>
#include <vector>

namespace brulion::io {

using namespace Qt::StringLiterals;
using namespace detail;

namespace {

struct Registry {
    std::vector<std::unique_ptr<Importer>> owned;
    QList<const Importer *> list;

    Registry()
    {
        for (auto *make :
             {makeObsidianImporter, makeLogseqImporter, makeNotionImporter, makeEvernoteImporter, makeKeepImporter,
              makeJoplinImporter, makeSimplenoteImporter, makeRoamImporter, makeTriliumImporter, makeBearImporter,
              makeMarkdownImporter, makeHtmlImporter, makeIcsImporter}) {
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

constexpr int MaxArchiveDepth = 4;

bool isJunk(const QString &path)
{
    const QStringList segments = path.split(u'/');
    if (segments.contains(u"__MACOSX"_s))
        return true;
    const QString name = segments.last();
    return name == u".DS_Store" || name == u"Thumbs.db" || name == u"desktop.ini" || name.startsWith(u"._");
}

/// A file holding notes (Markdown, text, HTML, an Evernote or calendar export).
bool isNote(const QString &path)
{
    const QString ext = paths::extension(path);
    return paths::isMarkdown(path) || ext == u".txt" || ext == u".html" || ext == u".htm" || ext == u".enex"
           || ext == u".ics";
}

enum class ArchiveKind { None, Zip, Tar };

ArchiveKind archiveKind(const InputFile &file)
{
    const QString ext = paths::extension(file.path);
    if ((ext == u".zip" || ext == u".bear2bk" || ext == u".textpack") && isZip(file.data))
        return ArchiveKind::Zip;
    if ((ext == u".jex" || ext == u".tar") && isTar(file.data))
        return ArchiveKind::Tar;
    return ArchiveKind::None;
}

QByteArray toBytes(const QString &text, bool bom)
{
    return (bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + text.toUtf8();
}

} // namespace

const QList<const Importer *> &importers()
{
    return registry().list;
}

const Importer *importerById(QStringView id)
{
    for (const Importer *importer : importers()) {
        if (importer->id() == id)
            return importer;
    }
    return nullptr;
}

QList<InputFile> prepareImportFiles(const QList<InputFile> &files, QStringList *warnings, const ZipReadOptions &limits)
{
    struct Item {
        InputFile file;
        int depth = 0;
        bool unpack = true; ///< false: an archive inside an archive of notes (an attachment)
    };
    QList<InputFile> out;
    QHash<QString, qsizetype> seen;
    const auto add = [&](InputFile file) {
        const QString key = pathKey(file.path);
        const auto it = seen.constFind(key);
        if (it == seen.constEnd()) {
            seen.insert(key, out.size());
            out.append(std::move(file));
        } else if (out[*it].data != file.data && warnings) {
            warnings->append(
                    IoMessages::tr("“%1” appears twice in the input; the first copy was used.").arg(file.path));
        }
    };
    // What all archives of this input may unpack together (archives inside archives too).
    qint64 unpackedBytes = 0;
    qint64 unpackedEntries = 0;
    const auto remaining = [](qint64 limit, qint64 used) { return std::max<qint64>(0, limit - used); };
    // Depth-first in input order: an archive's files take its place.
    QList<Item> stack;
    for (qsizetype i = files.size() - 1; i >= 0; --i)
        stack.append(Item{files[i], 0});
    while (!stack.isEmpty()) {
        Item item = stack.takeLast();
        InputFile &file = item.file;
        file.path = sanitizeArchivePath(file.path);
        if (file.path.isEmpty())
            file.path = u"file"_s;
        if (isJunk(file.path))
            continue;
        const ArchiveKind kind = item.unpack && item.depth < MaxArchiveDepth ? archiveKind(file) : ArchiveKind::None;
        if (kind == ArchiveKind::None) {
            add(std::move(file));
            continue;
        }
        QStringList archiveWarnings;
        ZipReadOptions budget = limits;
        budget.maxTotalSize = remaining(limits.maxTotalSize, unpackedBytes);
        budget.maxEntries = int(remaining(limits.maxEntries, unpackedEntries));
        budget.expandNested = false; // nesting is decided here, level by level
        const QList<FileEntry> entries = kind == ArchiveKind::Zip ? readZip(file.data, &archiveWarnings, budget)
                                                                  : readTar(file.data, &archiveWarnings);
        for (const FileEntry &e : entries)
            unpackedBytes += e.data.size();
        unpackedEntries += entries.size();
        if (warnings)
            warnings->append(archiveWarnings);
        if (entries.isEmpty() && !archiveWarnings.isEmpty()) {
            add(std::move(file)); // damaged: kept as a plain file
            continue;
        }
        // Archives inside an archive are unpacked only when it holds no notes of its own (Notion's
        // multi-part export: a ZIP of ZIPs); in a vault or an export of notes they are attachments.
        const bool unpackNested =
                std::none_of(entries.begin(), entries.end(), [](const FileEntry &e) { return isNote(e.path); });
        const QString folder = paths::dirname(file.path);
        for (qsizetype k = entries.size() - 1; k >= 0; --k) {
            const FileEntry &e = entries[k];
            stack.append(Item{InputFile{folder.isEmpty() ? e.path : folder + u'/' + e.path, e.data}, item.depth + 1,
                              unpackNested});
        }
    }
    return out;
}

const Importer *detectImporter(const QList<InputFile> &files)
{
    const QList<InputFile> prepared = prepareImportFiles(files);
    const Importer *best = nullptr;
    int bestScore = 0;
    for (const Importer *importer : importers()) {
        const int score = importer->detect(prepared);
        if (score > bestScore) {
            best = importer;
            bestScore = score;
        }
    }
    return best;
}

ImportResult runImport(const Importer &importer, const QList<InputFile> &files, const ImportOptions &options)
{
    QStringList warnings;
    const QList<InputFile> prepared = prepareImportFiles(files, &warnings);
    ImportResult raw = importer.run(prepared, options);
    warnings.append(raw.warnings);

    ImportResult out;
    QHash<QString, qsizetype> index;
    PathClaims claims(options.exists);
    struct Rename {
        VaultPath from;
        VaultPath to;
    };
    QList<Rename> renamedPages;
    for (ImportedFile &file : raw.files) {
        const VaultPath path = paths::normalize(sanitizeArchivePath(file.path));
        if (path.isEmpty())
            continue;
        if (paths::isIgnored(path)) {
            warnings.append(IoMessages::tr("“%1” would be saved in a protected folder and was skipped.").arg(path));
            continue;
        }
        const QString key = pathKey(path);
        if (file.append) {
            const auto it = index.constFind(key);
            if (it != index.constEnd()) {
                ImportedFile &existing = out.files[*it];
                existing.data = appendImported(existing.data, file.data);
                existing.append = true;
                continue;
            }
            index.insert(key, out.files.size());
            claims.reserve(path); // a page with the same path gets another name
            out.files.append(ImportedFile{path, std::move(file.data), true});
            continue;
        }
        VaultPath target = path;
        const bool duplicate = index.contains(key);
        if (duplicate || claims.isTaken(path)) {
            target = claims.claim(path);
            warnings.append(IoMessages::tr("“%1” already exists; it was saved as “%2”.").arg(path, target));
            if (!duplicate && paths::isMarkdown(path))
                renamedPages.append(Rename{path, target});
        } else {
            claims.claim(path);
        }
        index.insert(pathKey(target), out.files.size());
        out.files.append(ImportedFile{target, std::move(file.data), false});
    }
    if (!renamedPages.isEmpty()) {
        for (ImportedFile &file : out.files) {
            if (!paths::isMarkdown(file.path))
                continue;
            QString content = decodeText(file.data);
            const QString before = content;
            for (const Rename &r : std::as_const(renamedPages))
                content =
                        markdown::renameLinks(content, paths::pageTitle(r.from), paths::pageTitle(r.to), r.from, r.to);
            if (content != before)
                file.data = content.toUtf8();
        }
    }
    QSet<QString> unique;
    for (const QString &w : std::as_const(warnings)) {
        if (!unique.contains(w)) {
            unique.insert(w);
            out.warnings.append(w);
        }
    }
    return out;
}

QByteArray appendImported(const QByteArray &existing, const QByteArray &imported)
{
    const QString current = decodeText(existing);
    if (current.trimmed().isEmpty())
        return imported;
    QString addition = decodeText(imported);
    if (const auto fm = markdown::findFrontmatter(addition))
        addition = addition.sliced(fm->bodyStart);
    addition = normalizeNewlines(addition);
    if (addition.isEmpty())
        return existing;
    const QString eol = text::detectEol(current);
    // Drop the trailing blank lines only; the last line itself stays as written (a hard line
    // break "  " included).
    qsizetype end = current.size();
    while (end > 0 && current[end - 1].isSpace())
        --end;
    while (end < current.size() && current[end] != u'\n' && current[end] != u'\r')
        ++end;
    const QString body = current.first(end);
    if (eol != u"\n")
        addition.replace(u'\n', eol);
    return toBytes(body + eol + eol + addition + eol, existing.startsWith("\xEF\xBB\xBF"));
}

} // namespace brulion::io
