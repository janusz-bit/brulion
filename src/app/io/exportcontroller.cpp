#include "exportcontroller.h"

#include "log/categories.h"
#include "log/slowscope.h"

#include "fileexchange.h"
#include "iobackground.h"

#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "webpicture/webpicture.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WASM
#include <QFileDialog>
#endif
#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

bool isInFolder(const VaultPath &path, const QString &folder)
{
    return folder.isEmpty() || path.startsWith(folder + u'/');
}

} // namespace

ExportSelection selectExportFiles(const QList<FileStat> &files, const QString &scope, const QString &folder,
                                  const QString &page,
                                  const std::function<QList<VaultPath>(const VaultPath &)> &linkedFiles)
{
    QHash<VaultPath, FileStat> byPath;
    for (const FileStat &f : files) {
        if (!f.isDir)
            byPath.insert(f.path, f);
    }
    QSet<VaultPath> chosen;
    const QString cleanFolder = paths::normalize(folder);
    const QString cleanPage = paths::normalize(page);
    QList<VaultPath> pagesToFollow;
    if (scope == u"page") {
        if (byPath.contains(cleanPage)) {
            chosen.insert(cleanPage);
            pagesToFollow.append(cleanPage);
        }
    } else {
        const bool all = scope != u"folder";
        for (const FileStat &f : files) {
            if (f.isDir || (!all && !isInFolder(f.path, cleanFolder)))
                continue;
            chosen.insert(f.path);
            if (!all && paths::isMarkdown(f.path))
                pagesToFollow.append(f.path);
        }
        if (!all && cleanFolder.isEmpty())
            pagesToFollow.clear(); // the whole notebook: everything is in already
    }
    if (linkedFiles) {
        for (const VaultPath &p : std::as_const(pagesToFollow)) {
            for (const VaultPath &target : linkedFiles(p)) {
                if (!paths::isMarkdown(target) && byPath.contains(target))
                    chosen.insert(target);
            }
        }
    }
    ExportSelection out;
    out.paths = QList<VaultPath>(chosen.cbegin(), chosen.cend());
    std::sort(out.paths.begin(), out.paths.end());
    for (const VaultPath &p : std::as_const(out.paths)) {
        if (paths::isMarkdown(p))
            ++out.pages;
        else
            ++out.attachments;
        out.bytes += byPath.value(p).size;
    }
    return out;
}

// ---- ExportController -----------------------------------------------------------------------------

ExportController::ExportController(QObject *parent)
    : QObject(parent)
{
    m_summaryTimer.setSingleShot(true);
    m_summaryTimer.setInterval(150);
    // Vault changes come in bursts (an import writes thousands of files): the summary and the
    // folder list follow once the burst is over.
    connect(&m_summaryTimer, &QTimer::timeout, this, [this] {
        updateSummary();
        if (m_foldersDirty) {
            m_foldersDirty = false;
            Q_EMIT foldersChanged();
        }
    });
    m_readTimer.setSingleShot(true);
    m_readTimer.setInterval(0);
    connect(&m_readTimer, &QTimer::timeout, this, &ExportController::readStep);
    connect(this, &ExportController::optionsChanged, &m_summaryTimer, qOverload<>(&QTimer::start));
    // Switching the UI language installs another translator: the translated lists follow.
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);
}

ExportController::~ExportController() = default;

bool ExportController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::LanguageChange && watched == QCoreApplication::instance()) {
        Q_EMIT formatsChanged();
        m_summaryTimer.start(); // the summary's texts (sizes, problems)
    }
    return QObject::eventFilter(watched, event);
}

void ExportController::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace, nullptr, this, nullptr);
    m_workspace = workspace;
    if (m_workspace) {
        connect(m_workspace, &shell::Workspace::revisionChanged, this, [this] {
            m_foldersDirty = true;
            m_summaryTimer.start();
        });
        connect(m_workspace, &shell::Workspace::openChanged, this, [this] {
            cancel();
            discard();
            Q_EMIT foldersChanged();
            m_summaryTimer.start();
        });
        connect(m_workspace, &shell::Workspace::readyChanged, &m_summaryTimer, qOverload<>(&QTimer::start));
    }
    Q_EMIT workspaceChanged();
    Q_EMIT foldersChanged();
    updateSummary();
}

QVariantList ExportController::formats() const
{
    const auto describe = [](const QString &id) -> std::pair<QString, QString> {
        if (id == u"markdown-zip")
            return { tr("All notes as plain Markdown files with their attachments – a backup any app can read."),
                     u"files"_s };
        if (id == u"obsidian-zip")
            return { tr("A vault to open in Obsidian: wiki links, attachments and daily notes in its layout."),
                     u"notebook-pen"_s };
        if (id == u"logseq-zip")
            return { tr("A graph for Logseq: pages, journals and assets in Logseq's outline format."),
                     u"list-indent-increase"_s };
        if (id == u"html-site")
            return { tr("A small website: one page per note with working links, for a browser or a web server."),
                     u"monitor"_s };
        if (id == u"html-single")
            return { tr("Everything in one HTML file – easy to print, send or archive."), u"file-text"_s };
        if (id == u"pandoc")
            return {tr("One Markdown document for Pandoc, to turn into Word, LibreOffice, PDF or EPUB."),
                    u"book-open"_s};
        if (id == u"json")
            return { tr("Notes with their tasks, tags and links in one JSON file, for scripts and other tools."),
                     u"code"_s };
        if (id == u"ics")
            return { tr("Dated tasks and reminders as a calendar file for Google Calendar, Outlook or a phone."),
                     u"calendar-days"_s };
        if (id == u"opml")
            return { tr("The outline of the pages for outliner and mind-mapping apps."), u"list"_s };
        if (id == u"tasks-csv")
            return { tr("All tasks as a table for a spreadsheet (LibreOffice Calc, Excel)."), u"table"_s };
        return { QString(), u"file-output"_s };
    };
    QVariantList out;
    for (const io::Exporter *e : io::exporters()) {
        const auto [description, icon] = describe(e->id());
        out.append(QVariantMap{ { u"id"_s, e->id() },
                                { u"name"_s, e->name() },
                                { u"ext"_s, e->fileExtension() },
                                { u"description"_s, description },
                                { u"icon"_s, icon } });
    }
    return out;
}

void ExportController::setFormat(const QString &format)
{
    if (format == m_format || !io::exporterById(format))
        return;
    m_format = format;
    discard();
    Q_EMIT optionsChanged();
}

void ExportController::setWebPictures(const QString &mode)
{
    const QString clean = mode == u"include" ? mode : u"link"_s;
    if (clean == m_webPictures)
        return;
    m_webPictures = clean;
    discard();
    Q_EMIT optionsChanged();
}

bool ExportController::webPicturesApply() const
{
    return m_format == u"html-site" || m_format == u"html-single" || m_format == u"pandoc";
}

void ExportController::setScope(const QString &scope)
{
    const QString clean = scope == u"folder" || scope == u"page" ? scope : u"vault"_s;
    if (clean == m_scope)
        return;
    m_scope = clean;
    discard();
    Q_EMIT optionsChanged();
}

void ExportController::setFolder(const QString &folder)
{
    const QString clean = paths::normalize(folder);
    if (clean == m_folder)
        return;
    m_folder = clean;
    discard();
    Q_EMIT optionsChanged();
}

void ExportController::setPage(const QString &page)
{
    const QString clean = paths::normalize(page);
    if (clean == m_page)
        return;
    m_page = clean;
    discard();
    Q_EMIT optionsChanged();
}

QString ExportController::pageTitle() const
{
    return m_page.isEmpty() ? QString() : paths::pageTitle(m_page);
}

QStringList ExportController::folders() const
{
    QStringList out;
    if (Vault *vault = m_workspace ? m_workspace->vault() : nullptr) {
        for (const FileStat &f : vault->folders())
            out.append(f.path);
    }
    return out;
}

ExportSelection ExportController::currentSelection() const
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault)
        return {};
    VaultIndex *index = m_workspace->index();
    const auto linked = [index](const VaultPath &page) {
        QList<VaultPath> out;
        if (index) {
            for (const ResolvedLink &l : index->outgoingLinks(page)) {
                if (!l.target.isEmpty())
                    out.append(l.target);
            }
        }
        return out;
    };
    return selectExportFiles(vault->files(), m_scope, m_folder, m_page, linked);
}

void ExportController::updateSummary()
{
    QVariantMap s;
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    QString error;
    if (!vault)
        error = tr("Open a notebook first.");
    else if (m_scope == u"folder" && (m_folder.isEmpty() || !vault->exists(m_folder)))
        error = m_folder.isEmpty() ? tr("Choose a folder.") : tr("The folder “%1” does not exist.").arg(m_folder);
    else if (m_scope == u"page" && (m_page.isEmpty() || !vault->exists(m_page)))
        error = m_page.isEmpty() ? tr("Choose a page.") : tr("The page “%1” does not exist.").arg(pageTitle());
    const ExportSelection sel = error.isEmpty() ? currentSelection() : ExportSelection();
    if (error.isEmpty() && sel.paths.isEmpty())
        error = tr("There is nothing to export yet.");
    s = {
        { u"files"_s, int(sel.paths.size()) },
        { u"pages"_s, sel.pages },
        { u"attachments"_s, sel.attachments },
        { u"bytes"_s, sel.bytes },
        { u"sizeText"_s, sizeText(sel.bytes) },
        { u"valid"_s, error.isEmpty() },
        { u"error"_s, error },
    };
    if (s == m_summary)
        return;
    m_summary = s;
    Q_EMIT summaryChanged();
}

void ExportController::setState(const QString &state, const QString &error)
{
    if (state == m_state && error == m_errorText)
        return;
    m_state = state;
    m_errorText = error;
    Q_EMIT stateChanged();
}

void ExportController::setProgress(qreal value, const QString &text)
{
    m_progress = value;
    m_progressText = text;
    Q_EMIT progressChanged();
}

QString ExportController::fileName() const
{
    return m_result.isEmpty() ? QString() : m_result.first().name;
}

QString ExportController::fileSizeText() const
{
    qint64 bytes = 0;
    for (const io::ExportedFile &f : m_result)
        bytes += f.data.size();
    return m_result.isEmpty() ? QString() : sizeText(bytes);
}

QUrl ExportController::defaultSaveFolder() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    return QUrl::fromLocalFile(dir);
}

QUrl ExportController::suggestedSaveFile() const
{
    const QString name = fileName();
    if (name.isEmpty())
        return {};
    return QUrl::fromLocalFile(QDir(defaultSaveFolder().toLocalFile()).filePath(name));
}

QString ExportController::exportFolder() const
{
    for (const auto location : { QStandardPaths::DownloadLocation, QStandardPaths::DocumentsLocation }) {
        const QString dir = QStandardPaths::writableLocation(location);
        if (!dir.isEmpty())
            return dir;
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/exports"_s;
}

QString ExportController::exportName() const
{
    if (m_scope == u"page")
        return pageTitle();
    if (m_scope == u"folder" && !m_folder.isEmpty())
        return m_folder.section(u'/', -1);
    return m_workspace ? m_workspace->name() : QString();
}

// ---- Producing --------------------------------------------------------------------------------------

void ExportController::prepare()
{
    if (busy())
        return;
    updateSummary();
    if (!m_summary.value(u"valid"_s).toBool()) {
        setState(u"error"_s, m_summary.value(u"error"_s).toString());
        return;
    }
    ++m_generation;
    m_result.clear();
    m_warnings.clear();
    m_savedTo.clear();
    m_readQueue = currentSelection().paths;
    if (m_scope == u"vault" && m_workspace->vault()->exists(u".brulion/config.json"_s))
        m_readQueue.append(u".brulion/config.json"_s);
    m_readIndex = 0;
    m_readFiles.clear();
    m_readWarnings.clear();
    setState(u"preparing"_s);
    setProgress(0, tr("Reading the notes…"));
    m_readTimer.start();
}

void ExportController::readStep()
{
    Vault *vault = m_workspace ? m_workspace->vault() : nullptr;
    if (!vault || m_state != u"preparing") {
        m_readQueue.clear();
        return;
    }
    int read = 0;
    while (m_readIndex < m_readQueue.size() && read < m_readChunk) {
        const VaultPath path = m_readQueue.at(m_readIndex++);
        ++read;
        bool ok = false;
        QByteArray data = vault->readBinary(path, &ok);
        if (!ok) {
            m_readWarnings.append(tr("“%1” could not be read and is missing from the export.").arg(path));
            continue;
        }
        m_readFiles.append(io::FileEntry{ path, std::move(data) });
    }
    const qsizetype total = m_readQueue.size();
    setProgress(total > 0 ? 0.7 * qreal(m_readIndex) / qreal(total) : 0.7,
                tr("Reading %1 of %2 files…").arg(m_readIndex).arg(total));
    if (m_readIndex < total) {
        m_readTimer.start();
        return;
    }
    collectWebPictures();
}

// ---- Pictures from the internet ("include") ---------------------------------------------------------

void ExportController::collectWebPictures()
{
    m_webCopies.clear();
    if (m_webPictures != u"include" || !webPicturesApply() || !m_workspace) {
        runExporter();
        return;
    }
    io::ExportInput input;
    input.files = m_readFiles;
    input.config = m_workspace->vault() ? m_workspace->vault()->config() : VaultConfig();
    setProgress(-1, tr("Looking for pictures from the internet…"));
    const int generation = m_generation;
    // Parsing every note: off the GUI thread.
    runInBackground<QStringList>(
            this, [input] { return io::webPictureTargets(input); },
            [this, generation](QStringList urls) {
                if (generation != m_generation || m_state != u"preparing")
                    return;
                fetchWebPictures(urls);
            });
}

void ExportController::fetchWebPictures(const QStringList &urls)
{
    m_webPending.clear();
    QStringList missing;
    for (const QString &url : urls) {
        QString format;
        const QByteArray data = m_workspace->webPictureData(url, &format);
        if (!data.isEmpty() && !format.isEmpty())
            addWebPicture(url, data, format);
        else
            missing.append(url);
    }
    if (missing.isEmpty()) {
        runExporter();
        return;
    }
    // The user asked for copies: fetched like "Zapisz w notatniku" (also from a private host).
    m_webPending = QSet<QString>(missing.cbegin(), missing.cend());
    m_webTotal = m_webPending.size();
    m_webConnection = connect(m_workspace, &shell::Workspace::webPictureChanged, this,
                              &ExportController::webPictureChanged);
    webPictureProgress();
    for (const QString &url : std::as_const(missing))
        m_workspace->loadWebPicture(url, true);
}

void ExportController::webPictureChanged(const QString &url)
{
    if (!m_webPending.contains(url) || m_state != u"preparing" || !m_workspace)
        return;
    const QVariantMap picture = m_workspace->webPicture(url);
    const QString state = picture.value(u"state"_s).toString();
    if (state == u"loading")
        return;
    QString format;
    const QByteArray data = state == u"ready" ? m_workspace->webPictureData(url, &format) : QByteArray();
    if (!data.isEmpty() && !format.isEmpty()) {
        addWebPicture(url, data, format);
    } else {
        const QString host = webpicture::webUrl(url).host();
        const QString error = picture.value(u"error"_s).toString();
        m_readWarnings.append(error.isEmpty()
                                      ? tr("The picture from %1 was not included – the export keeps its address.").arg(host)
                                      : tr("The picture from %1 was not included (%2) – the export keeps its address.")
                                                .arg(host, error));
    }
    m_webPending.remove(url);
    if (!m_webPending.isEmpty()) {
        webPictureProgress();
        return;
    }
    stopWebPictures();
    runExporter();
}

void ExportController::addWebPicture(const QString &url, const QByteArray &data, const QString &format)
{
    m_webCopies.insert(url, io::WebPictureCopy{data, format, webpicture::attachmentName(webpicture::webUrl(url), format)});
}

void ExportController::webPictureProgress()
{
    const qsizetype done = m_webTotal - m_webPending.size();
    setProgress(0.7 + (m_webTotal > 0 ? 0.2 * qreal(done) / qreal(m_webTotal) : 0.2),
                tr("Fetching pictures from the internet (%1 of %2)…").arg(done + 1).arg(m_webTotal));
}

void ExportController::stopWebPictures()
{
    if (m_webConnection)
        disconnect(m_webConnection);
    m_webConnection = {};
    m_webPending.clear();
}

void ExportController::runExporter()
{
    const io::Exporter *exporter = io::exporterById(m_format);
    if (!exporter || !m_workspace || !m_workspace->vault()) {
        setState(u"error"_s, tr("This format is not available."));
        return;
    }
    io::ExportInput input;
    input.files = std::exchange(m_readFiles, {});
    input.config = m_workspace->vault()->config();
    input.vaultName = exportName();
    input.now = m_clock ? m_clock() : m_workspace->now();
    input.webPictures = std::exchange(m_webCopies, {});
    m_readQueue.clear();
    setProgress(-1, tr("Creating “%1”…").arg(exporter->name()));
    const int generation = m_generation;
    runInBackground<io::ExportResult>(
            this, [exporter, input] { return io::runExport(*exporter, input); },
            [this, generation](io::ExportResult result) {
                if (generation != m_generation || m_state != u"preparing")
                    return;
                m_result = result.files;
                m_warnings = m_readWarnings + result.warnings;
                setProgress(1);
                qint64 bytes = 0;
                for (const io::ExportedFile &f : m_result)
                    bytes += f.data.size();
                qCInfo(logging::lcIo).noquote() << u"Export as %1: %2 file(s), %3, %4 warning(s)"_s.arg(
                        m_format, QString::number(m_result.size()), logging::formatBytes(bytes),
                        QString::number(m_warnings.size()));
                if (m_result.isEmpty()) {
                    setState(u"error"_s, result.warnings.isEmpty() ? tr("Nothing was exported.") : result.warnings.first());
                    return;
                }
                setState(u"ready"_s);
            });
}

// ---- Saving ----------------------------------------------------------------------------------------

bool ExportController::saveTo(const QUrl &url)
{
    if (m_result.isEmpty() || !url.isValid() || url.isEmpty())
        return false;
    QString error;
    if (!writeToUrl(url, m_result.first().data, &error)) {
        setState(u"error"_s, tr("The file could not be saved: %1").arg(error));
        return false;
    }
    // More than one file (not produced by today's exporters): the others go next to it.
    if (m_result.size() > 1 && url.isLocalFile()) {
        const QString dir = QFileInfo(url.toLocalFile()).absolutePath();
        for (qsizetype i = 1; i < m_result.size(); ++i) {
            if (!writeToUrl(QUrl::fromLocalFile(dir + u'/' + m_result.at(i).name), m_result.at(i).data, &error))
                m_warnings.append(tr("“%1” could not be saved: %2").arg(m_result.at(i).name, error));
        }
    }
    m_savedTo = url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : displayNameOf(url);
    setState(u"saved"_s);
    Q_EMIT saved(tr("Saved “%1”.").arg(displayNameOf(url)));
    return true;
}

bool ExportController::saveToExportFolder()
{
    if (m_result.isEmpty())
        return false;
#ifdef Q_OS_ANDROID
    // Android 10+: the Download folder through MediaStore. With scoped storage a plain file
    // written there needs "All files access", which a notebook in the app's storage never asked
    // for. Android 9 writes the file below (storage permission).
    {
        platform::AndroidBridge *bridge = platform::AndroidBridge::instance();
        const QMimeDatabase mimes;
        const io::ExportedFile &first = m_result.first();
        const platform::DownloadResult download = bridge->saveToDownloads(
                first.name, mimes.mimeTypeForFile(first.name, QMimeDatabase::MatchExtension).name(), first.data);
        if (!download.unsupported) {
            if (!download.ok) {
                setState(u"error"_s, tr("The file could not be saved in Downloads: %1").arg(download.error));
                return false;
            }
            for (qsizetype i = 1; i < m_result.size(); ++i) {
                const io::ExportedFile &f = m_result.at(i);
                const platform::DownloadResult more = bridge->saveToDownloads(
                        f.name, mimes.mimeTypeForFile(f.name, QMimeDatabase::MatchExtension).name(), f.data);
                if (!more.ok)
                    m_warnings.append(tr("“%1” could not be saved: %2").arg(f.name, more.error));
            }
            m_savedTo = tr("Downloads/%1").arg(download.name);
            setState(u"saved"_s);
            Q_EMIT saved(tr("Saved “%1” in Downloads.").arg(download.name));
            return true;
        }
    }
#endif
    const QString dir = exportFolder();
    QDir().mkpath(dir);
    QString name = m_result.first().name;
    const QString stem = QFileInfo(name).completeBaseName();
    const QString suffix = QFileInfo(name).suffix();
    for (int n = 2; QFileInfo::exists(dir + u'/' + name); ++n)
        name = u"%1 (%2)%3"_s.arg(stem).arg(n).arg(suffix.isEmpty() ? QString() : u"."_s + suffix);
    return saveTo(QUrl::fromLocalFile(dir + u'/' + name));
}

bool ExportController::saveInBrowser()
{
#ifdef Q_OS_WASM
    if (m_result.isEmpty())
        return false;
    for (const io::ExportedFile &f : std::as_const(m_result))
        QFileDialog::saveFileContent(f.data, f.name);
    m_savedTo = m_result.first().name;
    setState(u"saved"_s);
    Q_EMIT saved(tr("“%1” is being downloaded.").arg(m_result.first().name));
    return true;
#else
    return false;
#endif
}

void ExportController::discard()
{
    if (m_state == u"preparing")
        ++m_generation;
    m_readTimer.stop();
    stopWebPictures();
    m_webCopies.clear();
    m_readQueue.clear();
    m_readFiles.clear();
    m_result.clear();
    m_warnings.clear();
    m_savedTo.clear();
    setState(u"idle"_s);
}

void ExportController::cancel()
{
    if (m_state == u"preparing")
        discard();
}

} // namespace brulion::ioviews
