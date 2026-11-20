// Import from other apps (docs/CLI.md "Commands", docs/IMPORT.md): `import <path>` — a file, a
// folder or a ZIP — through the io module's importers (the app's import screen does the same):
// detected or chosen with --from, written under --folder (default Import/<app>), daily notes
// appended to existing ones under "## Import (<app>)", nothing overwritten (a taken name gets
// " 2"); `--dry-run` shows the plan. CSV and Airtable take the column mapping as options;
// --download-attachments fetches a CSV's attachment cells first; Todoist and Airtable can download
// with a personal token (--token-file). `import formats` lists the importers. Not an MCP tool: it
// reads any file of this computer.
#include "commands_p.h"

#include "cloudimport/downloads.h"
#include "io/csvimport.h"
#include "io/importer.h"
#include "markdown/frontmatter.h"
#include "markdown/journal.h"
#include "sync/http.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// What one import reads from disk at most (as the app's file gatherer).
constexpr qint64 MaxFileBytes = 1024LL * 1024 * 1024;
constexpr qint64 MaxTotalBytes = 1536LL * 1024 * 1024;
constexpr int MaxFiles = 200000;
/// Files listed in the result.
constexpr int ListedFiles = 200;

/// The files of a path: the file itself, or a folder's files (its name kept as the top folder).
std::optional<QList<io::InputFile>> readInput(Call &call, const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        call.setError(ExitNotFound, u"not_found"_s, tr("No such file or folder: %1").arg(path));
        return std::nullopt;
    }
    QList<io::InputFile> out;
    qint64 total = 0;
    const auto add = [&](const QString &file, const QString &name) -> bool {
        QFile f(file);
        if (f.size() > MaxFileBytes) {
            call.warn(u"file_too_large"_s, tr("“%1” is larger than 1 GB and was skipped.").arg(name));
            return true;
        }
        if (total + f.size() > MaxTotalBytes || out.size() >= MaxFiles) {
            call.warn(u"input_too_large"_s,
                      tr("The input is larger than Brulion imports at once; the rest was left out."));
            return false;
        }
        if (!f.open(QIODevice::ReadOnly)) {
            call.warn(u"unreadable"_s, tr("“%1” cannot be read.").arg(name));
            return true;
        }
        out.append(io::InputFile{name, f.readAll()});
        total += out.last().data.size();
        return true;
    };
    if (info.isDir()) {
        const QDir root(info.absoluteFilePath());
        QDirIterator it(root.absolutePath(), QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            if (!add(file, info.fileName() + u'/' + root.relativeFilePath(file)))
                break;
        }
    } else {
        add(info.absoluteFilePath(), info.fileName());
    }
    return out;
}

/// "Rok=text; Cena=number:Cena brutto; Uwagi=skip" -> the columns' choices.
QHash<QString, io::CsvColumnOptions> columnOptions(Call &call, const QString &spec)
{
    QHash<QString, io::CsvColumnOptions> out;
    for (const QString &raw : spec.split(u';', Qt::SkipEmptyParts)) {
        const QString item = raw.trimmed();
        const qsizetype eq = item.indexOf(u'=');
        if (eq <= 0) {
            call.warn(u"bad_column"_s, tr("“%1” is no column=type[:property]; it was ignored.").arg(item));
            continue;
        }
        const QString name = item.first(eq).trimmed();
        const QString rest = item.sliced(eq + 1);
        const QString typeName = rest.section(u':', 0, 0).trimmed();
        io::CsvColumnOptions o;
        if (const auto t = io::csvColumnTypeFromName(typeName.isEmpty() ? u"auto"_s : typeName))
            o.type = *t;
        else
            call.warn(
                    u"bad_column"_s,
                    tr("“%1” is no column type; types: %2.")
                            .arg(typeName,
                                 u"auto, text, number, date, datetime, checkbox, list, tags, links, attachments, skip"_s));
        if (rest.contains(u':'))
            o.property = rest.section(u':', 1).trimmed();
        out.insert(name, o);
    }
    return out;
}

io::CsvImportOptions csvOptions(Call &call)
{
    io::CsvImportOptions o;
    o.mode = call.str(u"csvMode"_s) == u"table" ? io::CsvImportOptions::Mode::Table : io::CsvImportOptions::Mode::Pages;
    o.encoding = call.str(u"encoding"_s);
    const QString separator = call.str(u"separator"_s);
    if (separator == u"tab" || separator == u"\\t")
        o.separator = u'\t';
    else if (separator.size() == 1)
        o.separator = separator.front();
    if (call.str(u"header"_s) == u"yes")
        o.header = true;
    else if (call.str(u"header"_s) == u"no")
        o.header = false;
    o.titleColumn = call.str(u"titleColumn"_s);
    o.titleTemplate = call.str(u"titleTemplate"_s);
    o.folderTemplate = call.str(u"folderTemplate"_s);
    o.bodyColumn = call.str(u"bodyColumn"_s);
    o.listSeparator = call.str(u"listSeparator"_s);
    o.base = !call.flag(u"noBase"_s);
    o.columns = columnOptions(call, call.str(u"columns"_s));
    return o;
}

/// Run a download to its end (an event loop of its own): its files, or nullopt (call.error set).
std::optional<QList<io::FileEntry>> waitFor(Call &call, cloudimport::Download &download,
                                            const std::function<void()> &start)
{
    QEventLoop loop;
    std::optional<QList<io::FileEntry>> files;
    QObject::connect(&download, &cloudimport::Download::finished, &loop,
                     [&](const QList<io::FileEntry> &result, const QStringList &warnings) {
                         files = result;
                         for (const QString &w : warnings)
                             call.warn(u"download"_s, w);
                         loop.quit();
                     });
    QObject::connect(&download, &cloudimport::Download::failed, &loop,
                     [&](const QString &code, const QString &message) {
                         call.setError(code == u"network"_s ? ExitUnavailable : ExitConflict,
                                       u"download_"_s + code.split(u'-').join(u'_'), message);
                         loop.quit();
                     });
    QTimer::singleShot(0, &loop, start);
    loop.exec();
    return files;
}

QString readToken(Call &call)
{
    const QString file = call.str(u"tokenFile"_s);
    QString token;
    if (file == u"-") {
        QFile in;
        if (in.open(stdin, QIODevice::ReadOnly))
            token = QString::fromUtf8(in.readAll());
    } else if (!file.isEmpty()) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            token = QString::fromUtf8(f.read(4096));
        else
            call.setError(ExitNotFound, u"not_found"_s, tr("The token file cannot be read: %1").arg(file));
    } else {
        token = qEnvironmentVariable("BRULION_IMPORT_TOKEN");
    }
    return token.trimmed();
}

/// Todoist or Airtable through their APIs.
std::optional<QList<io::InputFile>> downloadService(Call &call, const QString &format)
{
    const QString token = readToken(call);
    if (call.error)
        return std::nullopt;
    if (token.isEmpty()) {
        call.setError(ExitUsage, u"token_required"_s,
                      tr("Give the token with --token-file FILE (or - for standard input) or BRULION_IMPORT_TOKEN."));
        return std::nullopt;
    }
    sync::NetworkHttpTransport transport;
    if (format == u"todoist") {
        cloudimport::TodoistDownload d(&transport);
        return waitFor(call, d, [&] { d.start(token); });
    }
    cloudimport::AirtableDownload d(&transport);
    QString base = call.str(u"base"_s);
    QString baseName = base;
    if (!base.startsWith(u"app"_s)) {
        // A base's name: find its id.
        QEventLoop loop;
        QList<cloudimport::AirtableBase> bases;
        QObject::connect(&d, &cloudimport::AirtableDownload::basesListed, &loop,
                         [&](const QList<cloudimport::AirtableBase> &b) {
                             bases = b;
                             loop.quit();
                         });
        QObject::connect(&d, &cloudimport::Download::failed, &loop, [&](const QString &code, const QString &message) {
            call.setError(ExitConflict, u"download_"_s + code.split(u'-').join(u'_'), message);
            loop.quit();
        });
        QTimer::singleShot(0, &loop, [&] { d.listBases(token); });
        loop.exec();
        if (call.error)
            return std::nullopt;
        QStringList names;
        for (const cloudimport::AirtableBase &b : std::as_const(bases)) {
            names.append(b.name);
            if (base.isEmpty() ? bases.size() == 1 : b.name.compare(base, Qt::CaseInsensitive) == 0) {
                base = b.id;
                baseName = b.name;
            }
        }
        if (!base.startsWith(u"app"_s)) {
            call.setError(ExitNotFound, u"base_not_found"_s,
                          tr("Choose the base with --base (its name or id): %1").arg(names.join(u", "_s)),
                          QJsonObject{{u"bases"_s, QJsonArray::fromStringList(names)}});
            return std::nullopt;
        }
    }
    const QStringList tables = call.str(u"tables"_s).split(u',', Qt::SkipEmptyParts);
    QStringList ids;
    for (const QString &t : tables)
        ids.append(t.trimmed());
    return waitFor(call, d, [&] { d.start(token, base, baseName, ids); });
}

Result importCommand(Call &call)
{
    const QString path = call.str(u"path"_s);
    QString format = call.str(u"from"_s);
    if (!format.isEmpty() && !io::importerById(format))
        return call.fail(ExitUsage, u"bad_format"_s,
                         tr("No importer “%1” (brulion-cli import formats lists them).").arg(format));
    const bool viaToken =
            call.has(u"tokenFile"_s) || (path.isEmpty() && (format == u"todoist" || format == u"airtable"));
    QList<io::InputFile> input;
    QString source;
    if (viaToken) {
        if (format != u"todoist" && format != u"airtable")
            return call.fail(ExitUsage, u"bad_format"_s,
                             tr("A token downloads from Todoist or Airtable: --from todoist or airtable."));
        auto files = downloadService(call, format);
        if (!files)
            return call.failed();
        input = *files;
        source = io::importerById(format)->name();
    } else {
        if (path.isEmpty())
            return call.fail(ExitUsage, u"path_required"_s, tr("Give the file, folder or ZIP to import."));
        auto files = readInput(call, path);
        if (!files)
            return call.failed();
        input = *files;
        source = QFileInfo(path).fileName();
    }
    if (input.isEmpty())
        return call.fail(ExitNotFound, u"nothing_to_import"_s, tr("Nothing was read from “%1”.").arg(path));
    const io::Importer *importer = format.isEmpty() ? io::detectImporter(input) : io::importerById(format);
    if (!importer)
        return call.fail(ExitParse, u"not_recognised"_s,
                         tr("Brulion does not recognise these files; choose the app with --from (brulion-cli import "
                            "formats)."));
    io::ImportOptions options;
    options.csv = csvOptions(call);
    options.now = call.ctx.now();
    if (call.flag(u"downloadAttachments"_s) && (importer->id() == u"csv" || importer->id() == u"airtable")) {
        const io::CsvAnalysis a =
                io::analyzeCsv(io::prepareImportFiles(input), options.csv,
                               importer->id() == u"airtable" ? io::CsvFlavor::Airtable : io::CsvFlavor::Generic);
        QList<QPair<QString, QString>> wanted;
        for (const io::CsvFileAnalysis &f : a.files) {
            for (const io::CsvAttachmentRef &r : f.attachments)
                wanted.append({r.url, r.name});
        }
        if (!wanted.isEmpty() && !call.dryRun()) {
            sync::NetworkHttpTransport transport;
            cloudimport::UrlFilesDownload d(&transport);
            if (auto files = waitFor(call, d, [&] { d.start(wanted); }))
                input += *files;
            else
                return call.failed();
        }
    }
    Vault *vault = call.vault();
    if (!vault)
        return call.failed();
    QString folder =
            call.has(u"folder"_s) ? call.str(u"folder"_s) : u"Import/"_s + paths::sanitizeFileName(importer->name());
    if (folder == u".")
        folder.clear();
    if (!folder.isEmpty()) {
        const auto clean = call.vaultPath(folder);
        if (!clean)
            return call.failed();
        folder = *clean;
    }
    options.targetFolder = folder;
    options.config = vault->config();
    options.exists = [vault](const VaultPath &p) { return vault->exists(vault->existingCase(p)); };
    // The plan (and the CSV mapping, for a look before writing).
    const io::ImportResult plan = io::runImport(*importer, input, options);
    for (const QString &w : plan.warnings)
        call.warn(u"import"_s, w);
    QJsonObject result{{u"importer"_s, importer->id()}, {u"name"_s, importer->name()}, {u"folder"_s, folder}};
    if (importer->id() == u"csv" || importer->id() == u"airtable") {
        const io::CsvAnalysis a = io::analyzeCsv(
                io::prepareImportFiles(input), options.csv,
                importer->id() == u"airtable" ? io::CsvFlavor::Airtable : io::CsvFlavor::Generic, options.timeZone);
        QJsonArray files;
        for (const io::CsvFileAnalysis &f : a.files) {
            QJsonArray columns;
            for (const io::CsvColumnInfo &c : f.columns)
                columns.append(QJsonObject{{u"name"_s, c.name},
                                           {u"property"_s, c.property},
                                           {u"type"_s, io::csvColumnTypeName(c.type)},
                                           {u"inferred"_s, io::csvColumnTypeName(c.inferred)},
                                           {u"filled"_s, c.filled}});
            files.append(QJsonObject{{u"file"_s, f.path},
                                     {u"encoding"_s, f.encoding},
                                     {u"separator"_s, f.separator == u'\t' ? u"tab"_s : QString(f.separator)},
                                     {u"header"_s, f.header},
                                     {u"rows"_s, f.rowCount},
                                     {u"titleColumn"_s, f.titleColumn},
                                     {u"bodyColumn"_s, f.bodyColumn},
                                     {u"columns"_s, columns},
                                     {u"attachments"_s, int(f.attachments.size())},
                                     {u"samplePage"_s, f.samplePage}});
        }
        result.insert(u"csv"_s, files);
    }
    int pages = 0, journals = 0, attachments = 0, appended = 0, renamed = 0, written = 0;
    QJsonArray listed, failed;
    const VaultConfig config = vault->config();
    if (!call.dryRun() && !call.lock())
        return call.failed();
    for (const io::ImportedFile &f : plan.files) {
        const bool journal = f.append || markdown::journalDateFromPath(f.path, config).has_value();
        const bool page = paths::isMarkdown(f.path);
        VaultPath used = f.path;
        if (!call.dryRun()) {
            if (f.append && vault->exists(f.path)) {
                bool ok = false;
                const QByteArray existing = vault->readBinary(f.path, &ok);
                QString addition = QString::fromUtf8(f.data);
                if (const auto fm = markdown::findFrontmatter(addition))
                    addition = addition.sliced(fm->bodyStart);
                addition = addition.trimmed();
                const QString current = QString::fromUtf8(existing);
                if (!ok) {
                    failed.append(QJsonObject{{u"path"_s, f.path}, {u"message"_s, vault->errorString()}});
                    continue;
                }
                if (!addition.isEmpty() && !current.contains(addition)) {
                    const QByteArray merged = io::appendImported(
                            existing, (u"## Import (%1)\n\n"_s.arg(importer->name()) + addition + u'\n').toUtf8());
                    if (!vault->writeBinary(f.path, merged, ChangeSource::Import)) {
                        failed.append(QJsonObject{{u"path"_s, f.path}, {u"message"_s, vault->errorString()}});
                        continue;
                    }
                }
                ++appended;
            } else {
                used = vault->createBinary(f.path, f.data, ChangeSource::Import);
                if (used.isEmpty()) {
                    failed.append(QJsonObject{{u"path"_s, f.path}, {u"message"_s, vault->errorString()}});
                    continue;
                }
                if (used.compare(f.path, Qt::CaseInsensitive) != 0)
                    ++renamed;
            }
        }
        ++written;
        if (journal)
            ++journals;
        else if (page)
            ++pages;
        else
            ++attachments;
        if (listed.size() < ListedFiles)
            listed.append(QJsonObject{{u"path"_s, used},
                                      {u"kind"_s, journal ? u"journal"_s
                                                  : page  ? u"page"_s
                                                          : u"attachment"_s},
                                      {u"size"_s, qint64(f.data.size())}});
    }
    result.insert(u"pages"_s, pages);
    result.insert(u"journals"_s, journals);
    result.insert(u"appended"_s, appended);
    result.insert(u"attachments"_s, attachments);
    result.insert(u"renamed"_s, renamed);
    result.insert(u"written"_s, call.dryRun() ? 0 : written);
    result.insert(u"files"_s, listed);
    result.insert(u"total"_s, int(plan.files.size()));
    result.insert(u"failed"_s, failed);
    if (call.dryRun())
        result.insert(u"dryRun"_s, true);
    QString text = call.dryRun() ? tr("%1: would write %2 page(s), %3 daily note(s), %4 attachment(s) into %5")
                                 : tr("%1: wrote %2 page(s), %3 daily note(s), %4 attachment(s) into %5");
    text = text.arg(importer->name()).arg(pages).arg(journals).arg(attachments).arg(folder.isEmpty() ? u"."_s : folder);
    if (!failed.isEmpty())
        return call.fail(ExitInternal, u"write_failed"_s,
                         QCoreApplication::translate("brulion::cli", "%n file(s) could not be written.", nullptr,
                                                     int(failed.size())),
                         result);
    return call.ok(result, text);
}

Result importFormats(Call &call)
{
    QJsonArray out;
    QStringList lines;
    for (const io::Importer *i : io::importers()) {
        out.append(QJsonObject{{u"id"_s, i->id()},
                               {u"name"_s, i->name()},
                               {u"fileTypes"_s, QJsonArray::fromStringList(i->fileTypes())}});
        lines.append(u"%1  %2  %3"_s.arg(i->id(), -11).arg(i->name(), -18).arg(i->fileTypes().join(u' ')));
    }
    return call.ok(QJsonObject{{u"formats"_s, out}}, lines.join(u'\n'));
}

} // namespace

void addImportCommands(QList<Command> &out)
{
    QStringList ids;
    for (const io::Importer *i : io::importers())
        ids.append(i->id());
    Command c;
    c.id = u"import"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                          "Import notes from another app: a file, a folder or a ZIP (Obsidian, Notion, "
                                          "Evernote, Apple Notes, OneNote downloads, Todoist, Airtable, CSV …)"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "As the app's import screen (docs/IMPORT.md): the format is recognised or chosen with --from, pages go "
            "under --folder (default Import/<app>), daily notes to the journal (appended under \"## Import (<app>)\" "
            "when the day exists), attachments to the attachments folder; nothing is overwritten (a taken name gets "
            "\" 2\"). --dry-run shows the plan. CSV and Airtable: --csv-mode, --title-column, --title-template, "
            "--folder-template, --body-column, --columns \"Rok=text; Cena=number:Cena brutto; Uwagi=skip\", "
            "--encoding, --separator, --header, --list-separator, --no-base, --download-attachments. Todoist and "
            "Airtable through their APIs: --from todoist|airtable --token-file FILE (or - for standard input, or "
            "BRULION_IMPORT_TOKEN), Airtable also --base and --tables."));
    c.run = importCommand;
    c.writes = true;
    c.mcp = false; // it reads any file of this computer
    c.args = {
            positional(u"path"_s, ArgType::String,
                       QT_TRANSLATE_NOOP("brulion::cli", "The file, folder or ZIP to import"), false),
            choice(u"from"_s, ids,
                   QT_TRANSLATE_NOOP("brulion::cli", "The app the files come from (default: recognised)")),
            option(u"folder"_s, ArgType::Path,
                   QT_TRANSLATE_NOOP("brulion::cli", "Folder for the pages (default: Import/<app>; . = the top)")),
            choice(u"csvMode"_s, {u"pages"_s, u"table"_s},
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: a page per row (default) or one page with the table")),
            option(u"titleColumn"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: the column naming the pages")),
            option(u"titleTemplate"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: the pages' titles, e.g. \"{{Name}} – {{Date}}\"")),
            option(u"folderTemplate"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: subfolders, e.g. \"{{Category}}\"")),
            option(u"bodyColumn"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: the column that becomes the page's text (- = none)")),
            option(u"columns"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: column=type[:property]; … (types: auto, text, number, date, "
                                                     "datetime, checkbox, list, tags, links, attachments, skip)")),
            option(u"encoding"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: utf-8, utf-16le, windows-1250, iso-8859-2, windows-1252 "
                                                     "(default: detected)")),
            option(u"separator"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: , ; | or tab (default: detected)")),
            choice(u"header"_s, {u"yes"_s, u"no"_s},
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "CSV: whether the first row names the columns (default: detected)")),
            option(u"listSeparator"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "CSV: what separates list items (default: detected)")),
            flag(u"noBase"_s, QT_TRANSLATE_NOOP("brulion::cli", "CSV: no Obsidian base next to the pages")),
            flag(u"downloadAttachments"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "CSV, Airtable: download the files of attachment cells first")),
            option(u"tokenFile"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "Todoist, Airtable: a file with the personal token (- = standard "
                                                     "input)")),
            option(u"base"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "Airtable: the base (name or id)")),
            option(u"tables"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli", "Airtable: table ids, separated by commas (default: all)")),
    };
    out.append(c);

    c = Command();
    c.id = u"import.formats"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "The apps and formats `import` reads"));
    c.run = importFormats;
    c.needsVault = false;
    c.mcp = false;
    out.append(c);
}

} // namespace brulion::cli
