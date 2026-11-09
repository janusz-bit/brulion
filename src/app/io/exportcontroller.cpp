#include "exportcontroller.h"

#include "log/categories.h"
#include "log/slowscope.h"

#include "fileexchange.h"
#include "iobackground.h"

#include "index/vaultindex.h"
#include "io/paper.h"
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
#include "wasm/wasmfiles.h"
#include "wasm/wasmopen.h"
#endif
#ifdef BRULION_PRINT_DIALOG
#include <QPrintDialog>
#include <QPrinter>
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
    std::ranges::sort(out.paths);
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
    : QObject(parent), m_paperSize(io::paperSizeId(io::paperSizeFor(QLocale::system())))
{
    m_summaryTimer.setSingleShot(true);
    m_summaryTimer.setInterval(150);
    connect(this, &ExportController::formatsChanged, this, &ExportController::chosenFormatChanged);
    connect(this, &ExportController::optionsChanged, this, &ExportController::chosenFormatChanged);
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
    m_webTimer.setSingleShot(true);
    m_webTimer.setInterval(0);
    connect(&m_webTimer, &QTimer::timeout, this, &ExportController::webCacheStep);
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
        if (id == u"pdf")
            return {tr("Pages ready to print or send, the same on every device – no other program needed."),
                    u"printer"_s};
        if (id == u"docx")
            return {tr("A document to edit in Word, LibreOffice Writer or Google Docs."), u"type"_s};
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

QVariantMap ExportController::chosenFormat() const
{
    for (const QVariant &row : formats()) {
        QVariantMap map = row.toMap();
        if (map.value(u"id"_s).toString() == m_format)
            return map;
    }
    return {};
}

void ExportController::setFormat(const QString &format)
{
    // "markdown-file": the page's file as it is (page scope; no exporter of the notebook's).
    if (format == m_format || (!io::exporterById(format) && format != u"markdown-file"))
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
    return m_format == u"html-site" || m_format == u"html-single" || m_format == u"pandoc" || m_format == u"pdf"
           || m_format == u"docx";
}

void ExportController::setPaperSize(const QString &paper)
{
    const QString clean = io::paperSizeId(io::paperSizeFromId(paper));
    if (clean == m_paperSize)
        return;
    m_paperSize = clean;
    discard();
    Q_EMIT optionsChanged();
}

bool ExportController::printAvailable() const
{
#if defined(BRULION_PRINT_DIALOG) || defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    return true;
#else
    return false;
#endif
}

void ExportController::setPrintToFile(const QString &pdfPath)
{
    m_printToFile = pdfPath;
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
    else if (m_format == u"markdown-file" && m_scope != u"page")
        error = tr("A Markdown file holds one page: choose a page or another format.");
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
    m_printing = false;
    startReading();
}

void ExportController::print()
{
    if (busy())
        return;
    if (!printAvailable()) {
        setState(u"error"_s, tr("Printing is not available here."));
        return;
    }
    m_printing = true;
    startReading();
}

void ExportController::startReading()
{
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
    m_webQueue = urls;
    m_webIndex = 0;
    m_webMissing.clear();
    m_webPrivateHosts.clear();
    m_webBytes = 0;
    m_webOverLimit = 0;
    m_webTimer.start();
}

void ExportController::webCacheStep()
{
    if (m_state != u"preparing" || !m_workspace) {
        m_webQueue.clear();
        return;
    }
    // The cached copies, read a few per event loop turn like the notes (each may be megabytes).
    int read = 0;
    while (m_webIndex < m_webQueue.size() && read < m_readChunk) {
        const QString url = m_webQueue.at(m_webIndex++);
        ++read;
        QString format;
        const QByteArray data = m_workspace->webPictureData(url, &format);
        if (!data.isEmpty() && !format.isEmpty()) {
            addWebPicture(url, data, format);
            continue;
        }
        const QString host = webpicture::webUrl(url).host();
        if (m_workspace->isPrivateWebHost(host)) {
            // This device or its network: fetched only when the user asks for that picture, never
            // because a note (synced, imported) names it.
            if (!m_webPrivateHosts.contains(host))
                m_webPrivateHosts.append(host);
            continue;
        }
        if (m_webBytes >= m_webLimit) {
            ++m_webOverLimit;
            continue;
        }
        m_webMissing.append(url);
    }
    if (m_webIndex < m_webQueue.size()) {
        m_webTimer.start();
        return;
    }
    m_webQueue.clear();
    if (!m_webPrivateHosts.isEmpty())
        m_readWarnings.append(tr("Pictures from this device or its network (%1) were not fetched – the export keeps "
                                 "their addresses. Show them in the note first to include them.")
                                      .arg(m_webPrivateHosts.join(u", "_s)));
    if (m_webMissing.isEmpty()) {
        finishWebPictures();
        return;
    }
    // The user asked for copies: the missing ones fetched like "Save to notebook".
    const QStringList missing = std::exchange(m_webMissing, {});
    m_webPending = QSet<QString>(missing.cbegin(), missing.cend());
    m_webTotal = m_webPending.size();
    m_webConnection = connect(m_workspace, &shell::Workspace::webPictureChanged, this,
                              &ExportController::webPictureChanged);
    webPictureProgress();
    for (const QString &url : missing)
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
    finishWebPictures();
}

void ExportController::addWebPicture(const QString &url, const QByteArray &data, const QString &format)
{
    // All copies are held until the exporter has run (and html-single makes text of them): a
    // notebook with hundreds of large pictures keeps the rest as addresses instead of running out
    // of memory.
    if (m_webBytes + data.size() > m_webLimit) {
        ++m_webOverLimit;
        return;
    }
    m_webBytes += data.size();
    m_webCopies.insert(url, io::WebPictureCopy{data, format, webpicture::attachmentName(webpicture::webUrl(url), format)});
}

void ExportController::finishWebPictures()
{
    if (m_webOverLimit > 0)
        m_readWarnings.append(tr("%n picture(s) from the internet were not included – the copies are limited to %1 "
                                 "in all; the export keeps their addresses.",
                                 nullptr, int(m_webOverLimit))
                                      .arg(QLocale().formattedDataSize(m_webLimit)));
    m_webOverLimit = 0;
    runExporter();
}

void ExportController::webPictureProgress()
{
    const qsizetype done = m_webTotal - m_webPending.size();
    setProgress(0.7 + (m_webTotal > 0 ? 0.2 * qreal(done) / qreal(m_webTotal) : 0.2),
                tr("Fetching pictures from the internet (%1 of %2)…").arg(done + 1).arg(m_webTotal));
}

void ExportController::stopWebPictures()
{
    m_webTimer.stop();
    m_webQueue.clear();
    m_webMissing.clear();
    if (m_webConnection)
        disconnect(m_webConnection);
    m_webConnection = {};
    m_webPending.clear();
}

io::ExportInput ExportController::exportInput()
{
    io::ExportInput input;
    input.files = std::exchange(m_readFiles, {});
    input.config = m_workspace->vault()->config();
    input.vaultName = exportName();
    input.now = m_clock ? m_clock() : m_workspace->now();
    input.webPictures = std::exchange(m_webCopies, {});
    input.paperSize = m_paperSize;
    m_readQueue.clear();
    return input;
}

void ExportController::runExporter()
{
    if (!m_workspace || !m_workspace->vault()) {
        setState(u"error"_s, tr("This format is not available."));
        return;
    }
    if (m_printing) {
        runPrint(exportInput());
        return;
    }
    if (m_format == u"markdown-file") {
        // The page's file as it is.
        const io::ExportInput input = exportInput();
        const auto page = std::find_if(input.files.cbegin(), input.files.cend(),
                                       [this](const io::FileEntry &f) { return f.path == m_page; });
        if (m_scope != u"page" || page == input.files.cend()) {
            setState(u"error"_s, m_readWarnings.isEmpty() ? tr("Nothing was exported.") : m_readWarnings.first());
            return;
        }
        m_result = {io::ExportedFile{paths::sanitizeFileName(pageTitle()) + u".md"_s, u"text/markdown"_s, page->data}};
        m_warnings = m_readWarnings;
        setProgress(1);
        setState(u"ready"_s);
        return;
    }
    const io::Exporter *exporter = io::exporterById(m_format);
    if (!exporter) {
        setState(u"error"_s, tr("This format is not available."));
        return;
    }
    const io::ExportInput input = exportInput();
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

// ---- Printing --------------------------------------------------------------------------------------

void ExportController::runPrint(io::ExportInput input)
{
    const QString title = exportName().isEmpty() ? u"Brulion"_s : exportName();
#if defined(BRULION_PRINT_DIALOG)
    // The desktop: the system's print dialog (or a PDF file for tests), then the pages painted on
    // the printer here — a short block of the GUI thread for the page being printed.
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setDocName(title);
    printer.setCreator(u"Brulion"_s);
    printer.setPageLayout(io::paperLayout(io::paperSizeFromId(m_paperSize)));
    if (!m_printToFile.isEmpty()) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(m_printToFile);
    } else {
        setProgress(-1, tr("Choose a printer…"));
        QPrintDialog dialog(&printer);
        dialog.setWindowTitle(tr("Print “%1”").arg(title));
        dialog.setOptions(QAbstractPrintDialog::PrintToFile | QAbstractPrintDialog::PrintShowPageSize
                          | QAbstractPrintDialog::PrintCollateCopies);
        const int generation = m_generation;
        const bool accepted = dialog.exec() == QDialog::Accepted;
        if (generation != m_generation || m_state != u"preparing")
            return;
        if (!accepted) {
            m_printing = false;
            setState(u"idle"_s);
            return;
        }
    }
    setProgress(-1, tr("Printing “%1”…").arg(title));
    QStringList warnings;
    const int pages = io::printNotes(input, &printer, &warnings);
    qCInfo(logging::lcIo).noquote() << u"Printed %1 page(s), %2 warning(s)"_s.arg(pages).arg(warnings.size());
    finishPrint(pages > 0, tr("The printer could not be used."), m_readWarnings + warnings);
#elif defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    // Android: the PDF export for the system's print service; the browser: the HTML document
    // export in its print dialog.
#ifdef Q_OS_ANDROID
    const io::Exporter *exporter = io::exporterById(u"pdf");
#else
    const io::Exporter *exporter = io::exporterById(u"html-single");
#endif
    setProgress(-1, tr("Preparing to print…"));
    const int generation = m_generation;
    runInBackground<io::ExportResult>(
            this, [exporter, input] { return io::runExport(*exporter, input); },
            [this, generation, title](io::ExportResult result) {
                if (generation != m_generation || m_state != u"preparing")
                    return;
                if (result.files.isEmpty()) {
                    finishPrint(false,
                                result.warnings.isEmpty() ? tr("Nothing was exported.") : result.warnings.first(), {});
                    return;
                }
#ifdef Q_OS_ANDROID
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + u"/print"_s;
                QDir().mkpath(dir);
                const QString path = dir + u'/' + paths::sanitizeFileName(title) + u".pdf"_s;
                QString error;
                const bool written = writeToUrl(QUrl::fromLocalFile(path), result.files.first().data, &error);
                const bool ok = written && platform::AndroidBridge::printPdf(path, title);
                finishPrint(ok, written ? tr("The system's printing could not be opened.") : error,
                            m_readWarnings + result.warnings);
#else
                platform::wasm::printHtml(result.files.first().data);
                finishPrint(true, {}, m_readWarnings + result.warnings);
#endif
            });
#else
    Q_UNUSED(input);
    Q_UNUSED(title);
    finishPrint(false, tr("Printing is not available here."), {});
#endif
}

void ExportController::finishPrint(bool ok, const QString &error, const QStringList &warnings)
{
    m_printing = false;
    setProgress(1);
    if (!ok) {
        setState(u"error"_s, error);
        return;
    }
    m_warnings = warnings;
    setState(u"printed"_s);
#if defined(BRULION_PRINT_DIALOG)
    Q_EMIT printed(tr("“%1” went to the printer.").arg(exportName()));
#else
    Q_EMIT printed(QString());
#endif
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
        platform::wasm::saveFile(f.data, f.name);
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
    m_printing = false;
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
